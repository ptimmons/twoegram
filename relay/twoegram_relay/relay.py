"""The relay: an a2 server to the Apple whose agents are those of several
upstream gateways (docs/RELAY.md, part 2).

Requests pass through. The relay checks the Apple's signature (A2Session),
makes its own signed request to the agent's gateway, checks that reply's
signature, and signs the payload again for the Apple. Message numbers, history,
busy and the gateway's folded text pass as they are: the relay keeps no
conversations. It changes two things in a poll's payload: the agent ids in
`unread` lines (its own ids, which may differ), and the `unread` counts for
the other gateways' agents, which it learns by polling those gateways quietly.

Every Apple paired with the relay is one device to each gateway: they share
the gateways' conversations.
"""

from __future__ import annotations

import asyncio
import time
from typing import Callable, Optional
from urllib.parse import urlencode

from twoegram.protocol import MAX_PAYLOAD, MAX_WAIT_MS, Code, msg_record
from twoegram.server import A2Session, Refusal, _int, _one
from twoegram.store import DeviceStore

from .upstream import CALL_TIMEOUT_S, Refused, Unreachable, Upstream

AGENTS_MAX = 8                  # what the Apple lists (SES_AGENTS_MAX)
UPSTREAM_WAIT_MS = 3000         # a poll's wait upstream: the Apple's cutoff is 8 s
QUIET_S = 20.0                  # how often the gateways the Apple is not polling are
ACTIVE_S = 10 * 60.0            # ... while an Apple has been heard from this recently
RETRY_S = 60.0                  # a gateway not answering is tried again this often


class RelayServer(A2Session):
    def __init__(self, devices: DeviceStore, upstreams: list[Upstream], *,
                 aliases: Optional[dict[str, dict[str, str]]] = None,
                 now: Callable[[], float] = lambda: time.time() * 1000,
                 log: Callable[[str], None] = lambda line: None, ident: str = "",
                 quiet_s: float = QUIET_S, retry_s: float = RETRY_S):
        super().__init__(devices, now=now, log=log, ident=ident)
        self.ups = upstreams
        self.aliases = aliases or {}
        self.quiet_s = quiet_s
        self.retry_s = retry_s
        self._ids: list[str] = []
        self._map: dict[str, tuple[Upstream, str]] = {}     # relay id -> (gateway, its id)
        self._seen: dict[str, int] = {}                     # relay id -> the Apple's last `after`
        self._unread: dict[str, int] = {}                   # relay id -> from the quiet polls
        self._polled: dict[str, float] = {}                 # gateway -> when the Apple last polled it
        self._active_until = 0.0
        self._tasks: list[asyncio.Task] = []
        self._dropped_logged = False

    # ---- the agents ----

    def remap(self) -> None:
        """Relay ids for every gateway's agents, in the order of the
        upstreams file: an alias if the file gives one, else the gateway's
        own id, else that id and the gateway's name. At most AGENTS_MAX."""
        ids: list[str] = []
        m: dict[str, tuple[Upstream, str]] = {}
        dropped = []
        for up in self.ups:
            for a in up.agents:
                want = self.aliases.get(up.name, {}).get(a) or a
                rid = want if want not in m else f"{a}-{up.name}"[:16]
                if rid in m:
                    dropped.append(f"{up.name}:{a}")
                    continue
                if len(ids) == AGENTS_MAX:
                    dropped.append(f"{up.name}:{a}")
                    continue
                ids.append(rid)
                m[rid] = (up, a)
        if dropped and not self._dropped_logged:
            self.log(f"relay: not listed (the Apple shows {AGENTS_MAX} at most, or the id is taken): "
                     + " ".join(dropped))
            self._dropped_logged = True
        self._ids, self._map = ids, m

    def agent_ids(self) -> list[str]:
        return self._ids

    def agent_label(self, agent: str) -> Optional[str]:
        up, a = self._map[agent]
        return up.labels.get(a) or (a if a != agent else None)

    def hello_done(self, device: str) -> None:
        self._active_until = time.monotonic() + ACTIVE_S

    # ---- requests ----

    async def endpoint(self, device: str, agent: str, path: str, method: str, q: dict,
                       body: bytes) -> bytes:
        self._active_until = time.monotonic() + ACTIVE_S
        up, ua = self._map[agent]
        params = {k: v[0] for k, v in q.items()}
        params["a"] = ua
        timeout = CALL_TIMEOUT_S
        kind = "call"
        if path == "/a2/poll":
            kind = "poll"
            w = min(_int(q, "w", 0), MAX_WAIT_MS, UPSTREAM_WAIT_MS)
            params["w"] = str(w)
            timeout = w / 1000 + CALL_TIMEOUT_S
        target = f"{path}?{urlencode(params)}"
        try:
            r = await up.call(kind, method, target, body, timeout)
        except Refused as e:
            raise Refusal(Code.BAD_REQUEST, e.why) from None
        except Unreachable:
            raise Refusal(Code.BAD_REQUEST, f"{up.name} is not answering") from None
        if r.code == Code.UNKNOWN_AGENT:
            # The gateway's agents changed: a fresh list, and the Apple will
            # say hello again for it.
            try:
                await up.refresh()
            except (Refused, Unreachable):
                pass
            self.remap()
            raise Refusal(Code.UNKNOWN_AGENT, r.line("why") or "no such agent")
        if r.code != Code.OK:
            raise Refusal(r.code, r.line("why") or "refused")
        if path != "/a2/poll":
            return r.payload
        self._seen[agent] = _int(q, "after", 0)
        self._polled[up.name] = time.monotonic()
        return self._poll_payload(up, r)

    def _poll_payload(self, up: Upstream, r) -> bytes:
        """The gateway's poll payload, with relay ids in its unread lines and
        the other gateways' unread counts added; trimmed to fit."""
        back = {a: rid for rid, (u, a) in self._map.items() if u is up}
        head: list[str] = []
        busy = r.line("busy")
        if busy is not None:
            head.append(f"busy {busy}")
        for k, v in r.lines:
            if k == "unread":
                a, _, n = v.partition(" ")
                if a in back:
                    head.append(f"unread {back[a]} {n}")
        for rid, (u, a) in self._map.items():
            if u is up:
                self._unread.pop(rid, None)         # the gateway just said
            elif self._unread.get(rid):
                head.append(f"unread {rid} {self._unread[rid]}")
        more = r.line("more") == "1"
        records = [msg_record(m) for m in r.msgs]
        while True:
            payload = (f"more {1 if more else 0}\n" + "".join(f"{h}\n" for h in head)).encode("latin-1") \
                + b"".join(records)
            if len(payload) <= MAX_PAYLOAD or not records:
                return payload
            records.pop()
            more = True

    # ---- in the background ----

    def start(self) -> None:
        self._tasks.append(asyncio.ensure_future(self._keep_up()))
        self._tasks.append(asyncio.ensure_future(self._quietly()))

    async def stop(self) -> None:
        for t in self._tasks:
            t.cancel()
        await asyncio.gather(*self._tasks, return_exceptions=True)
        self._tasks.clear()
        for up in self.ups:
            await up.close()

    async def refresh_all(self) -> None:
        """Hello to every gateway not answering (all of them, at first)."""
        async def one(up: Upstream) -> None:
            try:
                await up.refresh()
            except (Refused, Unreachable):
                pass
        await asyncio.gather(*(one(up) for up in self.ups if not up.ok))
        self.remap()

    async def _keep_up(self) -> None:
        while True:
            await self.refresh_all()
            await asyncio.sleep(self.retry_s)

    async def _quietly(self) -> None:
        while True:
            await asyncio.sleep(self.quiet_s)
            if time.monotonic() < self._active_until:
                await self.quiet_round()

    async def quiet_round(self) -> None:
        """One poll, waiting for nothing, to each gateway the Apple has not
        polled lately, for its unread counts. It asks for the first of that
        gateway's agents after the last message the Apple has seen there, so
        nothing the Apple has not fetched counts as seen."""
        for up in self.ups:
            if not up.ok or time.monotonic() - self._polled.get(up.name, 0) < self.quiet_s:
                continue
            mine = [(rid, a) for rid, (u, a) in self._map.items() if u is up]
            if not mine:
                continue
            rid, ua = mine[0]
            try:
                if rid not in self._seen:
                    # Not yet polled by the Apple: count only what arrives from now on.
                    h = await up.call("call", "GET", f"/a2/hist?{urlencode({'a': ua, 'k': 1})}")
                    self._seen[rid] = h.msgs[-1].n if h.msgs else 0
                after = self._seen[rid]
                r = await up.call("call", "GET", f"/a2/poll?{urlencode({'a': ua, 'after': after, 'w': 0})}")
            except (Refused, Unreachable):
                continue
            if r.code != Code.OK:
                continue
            back = {a: x for x, (u, a) in self._map.items() if u is up}
            for x, _ in mine:
                self._unread.pop(x, None)
            for k, v in r.lines:
                if k == "unread":
                    a, _, n = v.partition(" ")
                    if a in back and n.isdigit():
                        self._unread[back[a]] = int(n)
            fresh = sum(1 for m in r.msgs if not m.mine and m.n > after)
            if fresh:
                self._unread[rid] = fresh
