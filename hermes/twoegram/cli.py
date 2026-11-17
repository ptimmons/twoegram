"""`hermes a2 pair | list | revoke`, run on the Hermes host. They edit the
device file; the running gateway notices on its next request, so no restart
is needed. A port of plugin/src/commands.ts."""

from __future__ import annotations

import argparse
import socket

from .store import ConvStore, DeviceStore


def _lan_addresses() -> list[str]:
    """This host's LAN address, for the set-up hint: the one a UDP socket would
    use (nothing is sent)."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("192.0.2.1", 9))
            return [s.getsockname()[0]]
    except OSError:
        return []


def setup(parser: argparse.ArgumentParser) -> None:
    sub = parser.add_subparsers(dest="a2_command")
    p = sub.add_parser("pair", help="Make a key for a new Apple //e; it is shown once")
    p.add_argument("name", nargs="?", default="")
    sub.add_parser("list", help="List the paired devices")
    r = sub.add_parser("revoke", help="Remove a device and its conversations; it can no longer sign in")
    r.add_argument("id")


def _listen() -> tuple[str, int]:
    from gateway.platforms._shared import get_scoped_secret
    from .adapter import DEFAULT_PORT
    host = get_scoped_secret("A2_HOST", "") or "0.0.0.0"
    try:
        port = int(get_scoped_secret("A2_PORT", "") or DEFAULT_PORT)
    except ValueError:
        port = DEFAULT_PORT
    return host, port


def dispatch(args: argparse.Namespace, out=print) -> int:
    from .adapter import conversations_dir, devices_path
    store = DeviceStore(devices_path())
    cmd = getattr(args, "a2_command", None)
    if cmd == "pair":
        dev, written = store.pair(args.name)
        host, port = _listen()
        hosts = _lan_addresses() if host in ("0.0.0.0", "::") else [host]
        out(f"Device {dev.id} ({dev.name}).")
        out("")
        out("Type this key into Twoegram's set-up (Open-Apple-K). It is not shown again:")
        out("")
        out(f"    {written}")
        out("")
        if hosts:
            out("Gateway: " + "  or  ".join(f"http://{h}:{port}" for h in hosts))
        return 0
    if cmd == "list":
        devices = store.list()
        if not devices:
            out("no devices")
        for d in devices:
            out(f"{d.id}  {d.created}  {d.name}")
        return 0
    if cmd == "revoke":
        had = store.revoke(args.id)
        if had:
            ConvStore(conversations_dir()).drop(args.id)
        out(f"revoked {args.id}" if had else f"no device {args.id}")
        return 0 if had else 1
    out("usage: hermes a2 pair [name] | list | revoke <id>")
    return 1
