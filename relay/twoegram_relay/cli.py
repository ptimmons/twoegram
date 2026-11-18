"""twoegram-relay: the command line.

    twoegram-relay serve                       run the relay
    twoegram-relay pair [name]                 make a key for an Apple; shown once
    twoegram-relay add [name]                  add a key the Apple made (asked for, not shown)
    twoegram-relay list                        the Apples
    twoegram-relay revoke <id>                 remove an Apple
    twoegram-relay upstream add <name> <url>   a gateway: asks for the key from its `a2 pair`
    twoegram-relay upstream list               the gateways, and whether they answer
    twoegram-relay upstream remove <name>

Changes to the Apples take effect at their next request; changes to the
gateways when the relay is restarted. No command prints a stored key.
"""

from __future__ import annotations

import argparse
import asyncio
import getpass
import signal
import sys

from twoegram.protocol import device_id, parse_key
from twoegram.server import HttpListener
from twoegram.protocol import MAX_BODY
from twoegram.store import DeviceStore

from . import VERSION
from .config import NAME, Config, UpstreamConf
from .relay import RelayServer
from .upstream import Refused, Unreachable, Upstream

KEY_PROMPT = "Key (not shown as you type): "


def _parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="twoegram-relay", description="The Twoegram relay (docs/RELAY.md).")
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("serve", help="run the relay")
    p = sub.add_parser("pair", help="make a key for an Apple; it is shown once")
    p.add_argument("name", nargs="?", default="")
    p = sub.add_parser("add", help="add an Apple by the key its set-up shows")
    p.add_argument("name", nargs="?", default="")
    sub.add_parser("list", help="list the Apples")
    p = sub.add_parser("revoke", help="remove an Apple")
    p.add_argument("id")
    up = sub.add_parser("upstream", help="the gateways").add_subparsers(dest="ucmd")
    p = up.add_parser("add", help="add a gateway; asks for the key from its a2 pair")
    p.add_argument("name")
    p.add_argument("url")
    up.add_parser("list", help="list the gateways")
    p = up.add_parser("remove", help="remove a gateway")
    p.add_argument("name")
    return ap


def main(argv: list[str] | None = None, out=print, read_key=getpass.getpass) -> int:
    args = _parser().parse_args(argv)
    cfg = Config()
    if args.cmd == "serve":
        return serve(cfg, out)
    if args.cmd in ("pair", "add", "list", "revoke"):
        return _apples(args, cfg, out, read_key)
    if args.cmd == "upstream":
        return _upstreams(args, cfg, out, read_key)
    _parser().print_help()
    return 1


def _apples(args, cfg: Config, out, read_key) -> int:
    store = DeviceStore(cfg.devices_path)
    if args.cmd == "pair":
        dev, written = store.pair(args.name)
        out(f"Device {dev.id} ({dev.name}).")
        out("")
        out("Type this key into Twoegram's set-up. It is not shown again:")
        out("")
        out(f"    {written}")
        return 0
    if args.cmd == "add":
        key = parse_key(read_key(KEY_PROMPT))
        if key is None:
            out("That is not a key: a character is wrong or missing. Nothing was added.")
            return 1
        dev = store.add(key, args.name)
        if dev is None:
            out("That key is paired already. Nothing was added.")
            return 1
        out(f"Added device {dev.id} ({dev.name}).")
        out("Check that Twoegram's set-up shows the same device id.")
        return 0
    if args.cmd == "list":
        devices = store.list()
        if not devices:
            out("no devices")
        for d in devices:
            out(f"{d.id}  {d.created}  {d.name}")
        return 0
    had = store.revoke(args.id)
    out(f"revoked {args.id}" if had else f"no device {args.id}")
    return 0 if had else 1


def _upstreams(args, cfg: Config, out, read_key) -> int:
    if args.ucmd == "add":
        if not NAME.match(args.name):
            out("A gateway's name is 1 to 16 of a-z 0-9 - _.")
            return 1
        if cfg.find(args.name):
            out(f"There is a gateway called {args.name} already.")
            return 1
        url = args.url if "://" in args.url else f"http://{args.url}"
        if url.count(":") < 2:
            url += ":8417"
        key = parse_key(read_key(KEY_PROMPT))
        if key is None:
            out("That is not a key: a character is wrong or missing. Nothing was added.")
            return 1
        try:
            up = Upstream(args.name, url.rstrip("/"), key)
        except ValueError as e:
            out(str(e))
            return 1
        agents = asyncio.run(_try(up))
        if isinstance(agents, str):
            out(f"Not added: {agents}")
            return 1
        cfg.upstreams.append(UpstreamConf(args.name, up.url, key))
        cfg.save_upstreams()
        out(f"Added {args.name} at {up.url}, as device {device_id(key)} there.")
        out("Its agents: " + (" ".join(f"{a} ({up.labels[a]})" if a in up.labels else a for a in agents)
                              or "none"))
        out("Restart the relay for it to take effect.")
        return 0
    if args.ucmd == "list":
        if not cfg.upstreams:
            out("no gateways")
        for u in cfg.upstreams:
            up = Upstream(u.name, u.url, u.key)
            got = asyncio.run(_try(up))
            state = f"answering: {' '.join(got) or 'no agents'}" if isinstance(got, list) else got
            al = "".join(f"  {a}={x}" for a, x in u.aliases.items())
            out(f"{u.name}  {u.url}  device {device_id(u.key)}  {state}{al}")
        return 0
    if args.ucmd == "remove":
        u = cfg.find(args.name)
        if not u:
            out(f"no gateway {args.name}")
            return 1
        cfg.upstreams.remove(u)
        cfg.save_upstreams()
        out(f"removed {args.name}; revoke device {device_id(u.key)} on it too. Restart the relay.")
        return 0
    out("usage: twoegram-relay upstream add <name> <url> | list | remove <name>")
    return 1


async def _try(up: Upstream) -> list[str] | str:
    try:
        await up.refresh()
        return up.agents
    except (Refused, Unreachable) as e:
        return str(e)
    finally:
        await up.close()


def serve(cfg: Config, out=print) -> int:
    def log(line: str) -> None:
        out(line)
        sys.stdout.flush()

    async def run() -> None:
        ups = [Upstream(u.name, u.url, u.key, log=log) for u in cfg.upstreams]
        relay = RelayServer(DeviceStore(cfg.devices_path), ups,
                            aliases={u.name: u.aliases for u in cfg.upstreams},
                            log=log, ident=f"twoegram-relay {VERSION}")
        await relay.refresh_all()
        listener = HttpListener(relay.handle, MAX_BODY)
        await listener.start(cfg.host, cfg.port)
        log(f"relay: listening on http://{cfg.host}:{listener.port}/a2/ for "
            f"{len(relay.devices.list())} device(s); agents {' '.join(relay.agent_ids()) or 'none yet'}")
        relay.start()
        stop = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, stop.set)
        await stop.wait()
        await listener.stop()
        await relay.stop()

    if not cfg.upstreams:
        out(f"No gateways yet: twoegram-relay upstream add <name> <url> (file {cfg.upstreams_path}).")
    asyncio.run(run())
    return 0
