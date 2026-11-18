"""The relay's files (docs/RELAY.md, "Its files"):

    <config>/upstreams.json   the gateways: name, address, the relay's key there,
                              aliases for their agents; only its owner reads it
    <config>/relay.json       where to listen: {"host": "0.0.0.0", "port": 8417}
    <state>/devices.json      the Apples, as the plugins keep them (store.py)

<config> is /etc/twoegram-relay and <state> /var/lib/twoegram-relay unless
TWOEGRAM_RELAY_CONFIG and TWOEGRAM_RELAY_STATE say otherwise.
"""

from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass, field
from pathlib import Path

from twoegram.store import _write_private

NAME = re.compile(r"[a-z0-9_-]{1,16}\Z")
AGENT = re.compile(r"[a-z0-9_-]{1,16}\Z")
DEFAULT_PORT = 8417


def config_dir() -> Path:
    return Path(os.environ.get("TWOEGRAM_RELAY_CONFIG", "/etc/twoegram-relay"))


def state_dir() -> Path:
    return Path(os.environ.get("TWOEGRAM_RELAY_STATE", "/var/lib/twoegram-relay"))


@dataclass
class UpstreamConf:
    name: str
    url: str
    key: bytes
    aliases: dict[str, str] = field(default_factory=dict)


class Config:
    def __init__(self, cdir: Path | None = None, sdir: Path | None = None):
        self.cdir = cdir or config_dir()
        self.sdir = sdir or state_dir()
        self.upstreams: list[UpstreamConf] = []
        self.host = "0.0.0.0"
        self.port = DEFAULT_PORT
        self.load()

    @property
    def devices_path(self) -> Path:
        return self.sdir / "devices.json"

    @property
    def upstreams_path(self) -> Path:
        return self.cdir / "upstreams.json"

    def load(self) -> None:
        p = self.upstreams_path
        self.upstreams = []
        if p.exists():
            data = json.loads(p.read_text(encoding="utf-8"))
            for u in data.get("upstreams", []):
                aliases = {a: x for a, x in (u.get("aliases") or {}).items()
                           if AGENT.match(a) and AGENT.match(x)}
                self.upstreams.append(UpstreamConf(u["name"], u["url"], bytes.fromhex(u["key"]), aliases))
        r = self.cdir / "relay.json"
        if r.exists():
            data = json.loads(r.read_text(encoding="utf-8"))
            self.host = str(data.get("host", self.host))
            self.port = int(data.get("port", self.port))

    def save_upstreams(self) -> None:
        data = {"version": 1, "upstreams": [
            {"name": u.name, "url": u.url, "key": u.key.hex(), "aliases": u.aliases} for u in self.upstreams]}
        _write_private(self.upstreams_path, json.dumps(data, indent=2) + "\n")

    def find(self, name: str) -> UpstreamConf | None:
        return next((u for u in self.upstreams if u.name == name), None)
