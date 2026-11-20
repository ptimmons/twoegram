"""The devices the plugin knows, and each conversation's numbered messages.

Ports of openclaw/src/devices.ts and convstore.ts, with the same JSON files,
readable only by their owner. The device file is read again whenever its
modification time changes, so `hermes a2 pair` and `revoke`, run in another
process, take effect at the running gateway's next request.
"""

from __future__ import annotations

import datetime
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from .protocol import Msg, device_id, format_key, new_key

DEVICE = re.compile(r"[0-9a-f]{8}\Z")
AGENT = re.compile(r"[a-z0-9_-]{1,16}\Z")
LOG_KEEP = 500


def _write_private(path: Path, text: str) -> None:
    """Write a file only its owner can read, all at once."""
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    tmp = path.with_name(path.name + ".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(text)
    os.chmod(tmp, 0o600)
    os.replace(tmp, path)


@dataclass
class Device:
    id: str
    key: bytes
    name: str
    created: str


class DeviceStore:
    def __init__(self, path: Path | str):
        self.path = Path(path)
        self._devices: dict[str, Device] = {}
        self._mtime = -1.0
        self._load()

    def _mtime_now(self) -> float:
        try:
            return self.path.stat().st_mtime_ns
        except OSError:
            return 0

    def _load(self) -> None:
        self._devices.clear()
        self._mtime = self._mtime_now()
        if not self.path.exists():
            return
        data = json.loads(self.path.read_text(encoding="utf-8"))
        for d in data.get("devices", []):
            self._devices[d["id"]] = Device(d["id"], bytes.fromhex(d["key"]), d["name"], d["created"])

    def _save(self) -> None:
        data = {"version": 1, "devices": [
            {"id": d.id, "key": d.key.hex(), "name": d.name, "created": d.created} for d in self._devices.values()]}
        _write_private(self.path, json.dumps(data, indent=2) + "\n")
        self._mtime = self._mtime_now()

    def _fresh(self) -> None:
        if self._mtime_now() != self._mtime:
            self._load()

    def pair(self, name: str = "") -> tuple[Device, str]:
        """Make a device; returns it and its key in written form, shown once."""
        self._fresh()
        key = new_key()
        while device_id(key) in self._devices:
            key = new_key()
        created = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
        dev = Device(device_id(key), key, name or "Apple //e", created)
        self._devices[dev.id] = dev
        self._save()
        return dev, format_key(key)

    def add(self, key: bytes, name: str = "") -> Optional[Device]:
        """A device whose key was made elsewhere (by the Apple, from its master
        secret) and typed here; None if that key is paired already."""
        self._fresh()
        if len(key) != 16:
            raise ValueError("a device key is 16 bytes")
        if device_id(key) in self._devices:
            return None
        created = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
        dev = Device(device_id(key), bytes(key), name or "Apple //e", created)
        self._devices[dev.id] = dev
        self._save()
        return dev

    def revoke(self, ident: str) -> bool:
        self._fresh()
        had = self._devices.pop(ident, None) is not None
        if had:
            self._save()
        return had

    def get(self, ident: str) -> Optional[Device]:
        self._fresh()
        return self._devices.get(ident)

    def list(self) -> list[Device]:
        self._fresh()
        return list(self._devices.values())


class ConvStore:
    """One small JSON file per device and agent: what the Apple scrolls through,
    numbered as the protocol needs. Hermes keeps its own transcript."""

    def __init__(self, directory: Path | str | None):
        self.dir = Path(directory) if directory else None

    def _file(self, device: str, agent: str) -> Path:
        # Both are checked by the protocol before they get here; checked again
        # because they become a file name.
        if not DEVICE.match(device) or not AGENT.match(agent):
            raise ValueError(f"bad conversation name {device}.{agent}")
        assert self.dir is not None
        return self.dir / f"{device}.{agent}.json"

    def load(self, device: str, agent: str) -> Optional[tuple[list[Msg], int]]:
        if self.dir is None:
            return None
        try:
            text = self._file(device, agent).read_text(encoding="utf-8")
        except OSError:
            return None
        data = json.loads(text)
        return [Msg.from_json(m) for m in data["msgs"]], int(data["next"])

    def save(self, device: str, agent: str, msgs: list[Msg], nxt: int) -> None:
        if self.dir is None:
            return
        _write_private(self._file(device, agent),
                       json.dumps({"version": 1, "next": nxt, "msgs": [m.to_json() for m in msgs]},
                                  separators=(",", ":")) + "\n")

    def drop(self, device: str) -> None:
        """Forget every conversation of a device (when it is revoked)."""
        if self.dir is None or not DEVICE.match(device):
            return
        try:
            names = os.listdir(self.dir)
        except OSError:
            return
        for n in names:
            if n.startswith(f"{device}."):
                try:
                    (self.dir / n).unlink()
                except OSError:
                    pass
