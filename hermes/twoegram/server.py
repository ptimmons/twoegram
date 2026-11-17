"""The /a2 endpoints (docs/PROTOCOL.md): sessions, signatures, lockouts, and
each device's conversations.

A port of plugin/src/server.ts. What answers is behind `Agents`: inside Hermes
the adapter (adapter.py), in the tests the echo agent. Unlike the
TypeScript, an answer does not come back through the delivery call: the
agent hands it over later with `post()`, and says whether it is working with
`set_busy()`, which is how Hermes delivers.
"""

from __future__ import annotations

import asyncio
import re
import secrets
import time
from dataclasses import dataclass, field
from typing import Awaitable, Callable, Optional, Protocol
from urllib.parse import parse_qs, urlsplit

from .protocol import (
    DISPLAY_NAME_MAX, MAX_PAYLOAD, MAX_TARGET, MAX_WAIT_MS, VERSION, Code, Msg, hello_mac, hello_proof,
    mac_equals, msg_record, reply, reply_mac, request_mac, session_key,
)
from .store import LOG_KEEP, ConvStore, DeviceStore
from .text import display_label, for_apple, split

SESSIONS_PER_DEVICE = 4
SESSION_IDLE_MS = 30 * 60_000
DEVICE_FAILS = 5
DEVICE_WINDOW_MS = 5 * 60_000
DEVICE_LOCK_MS = 15 * 60_000
IP_REFUSALS = 20
IP_WINDOW_MS = 60_000
IP_BLOCK_MS = 5 * 60_000
HIST_MAX = 50

_HEX8 = re.compile(r"[0-9a-f]{8}\Z")
_HEX32 = re.compile(r"[0-9a-f]{32}\Z")


class Agents(Protocol):
    def agents(self) -> list[str]: ...
    def display_name(self, agent: str) -> Optional[str]: ...
    async def deliver(self, device: str, agent: str, text: str) -> None: ...


class Refusal(Exception):
    def __init__(self, code: int, why: str):
        super().__init__(why)
        self.code = code
        self.why = why


@dataclass
class _Session:
    sid: str
    device: str
    sk: bytes
    last_seq: int
    last_used: float


@dataclass
class _Conversation:
    msgs: list[Msg] = field(default_factory=list)
    next: int = 1
    busy: bool = False
    seen: int = 0


@dataclass
class Request:
    """What the HTTP layer hands over. body is None when it was over MAX_BODY."""
    method: str
    target: str
    headers: dict[str, str]        # names in lower case
    body: Optional[bytes]
    ip: str


def _ms() -> float:
    return time.time() * 1000


def hhmm(ms: float) -> str:
    return time.strftime("%H:%M", time.localtime(ms / 1000))


class A2Server:
    def __init__(self, devices: DeviceStore, agents: Agents, *, convs: Optional[ConvStore] = None,
                 now: Callable[[], float] = _ms, log: Callable[[str], None] = lambda line: None,
                 ident: str = "", seed: int = 0):
        self.devices = devices
        self.agents = agents
        self.convs = convs or ConvStore(None)
        self.now = now
        self.log = log
        self.ident = ident
        self.seed = seed
        self._sessions: dict[str, _Session] = {}
        self._convs: dict[str, _Conversation] = {}
        self._waiters: dict[str, set[asyncio.Future]] = {}        # by device
        self._device_fails: dict[str, tuple[list[float], float]] = {}
        self._ip_refusals: dict[str, list[float]] = {}             # [count, start, until]

    # ---- the handler ----

    async def handle(self, req: Request) -> bytes:
        try:
            return await self._dispatch(req, self.now())
        except Exception as e:
            r = e if isinstance(e, Refusal) else Refusal(Code.BAD_REQUEST, "internal")
            if not isinstance(e, Refusal):
                self.log(f"a2: {e!r}")
            # A refused hello still says which server refused it.
            who = self._server_line() if req.target.startswith("/a2/hello") else ""
            return reply(r.code, None, f"why {r.why}\n{who}".encode("latin-1"))

    async def _dispatch(self, req: Request, now: float) -> bytes:
        self._ip_check(req.ip, now)
        if len(req.target) > MAX_TARGET:
            raise self._refuse(req.ip, Code.TOO_LARGE, "request line too long", False)
        url = urlsplit(req.target)
        q = parse_qs(url.query, keep_blank_values=True)
        if req.body is None:
            raise Refusal(Code.TOO_LARGE, "body too long")

        if url.path == "/a2/hello" and req.method == "GET":
            return self._hello(q, req.ip, now)

        # Everything else: a session, a signature, a fresh sequence number.
        sid = req.headers.get("x-a2-session")
        seq_text = req.headers.get("x-a2-seq", "")
        s = self._sessions.get(sid) if sid else None
        if not s:
            raise Refusal(Code.NO_SESSION, "no such session")
        if not self.devices.get(s.device):
            self._end_sessions(s.device)            # revoked since the hello: its sessions end now
            raise Refusal(Code.NO_SESSION, "device revoked")
        if now - s.last_used > SESSION_IDLE_MS:
            self._sessions.pop(s.sid, None)
            raise Refusal(Code.NO_SESSION, "session expired")
        self._device_check(s.device, now)
        seq = int(seq_text) if seq_text.isdigit() else -1
        if seq < 1 or seq > 0xFFFFFFFF:
            raise Refusal(Code.BAD_REQUEST, "bad seq")
        if not mac_equals(request_mac(s.sk, seq, req.method, req.target, req.body), req.headers.get("x-a2-mac")):
            self._device_fail(s.device, now)
            raise self._refuse(req.ip, Code.BAD_SIGNATURE, "bad signature", True)

        # Genuine from here on, so replies are signed.
        def signed(code: int, payload: bytes) -> bytes:
            return reply(code, reply_mac(s.sk, seq, payload), payload)

        if seq <= s.last_seq:
            return signed(Code.REPLAY, b"why seq not rising\n")
        s.last_seq = seq
        s.last_used = now
        try:
            agent = _one(q, "a") or ""
            if agent not in self.agents.agents():
                raise Refusal(Code.UNKNOWN_AGENT, "no such agent")
            if url.path == "/a2/send" and req.method == "POST":
                payload = await self._send(s.device, agent, req.body)
            elif url.path == "/a2/poll" and req.method == "GET":
                payload = await self._poll(s.device, agent, q)
            elif url.path == "/a2/hist" and req.method == "GET":
                payload = self._hist(s.device, agent, q)
            else:
                raise Refusal(Code.BAD_REQUEST, "no such request")
            return signed(Code.OK, payload)
        except Refusal as e:
            return signed(e.code, f"why {e.why}\n".encode("latin-1"))

    # ---- hello ----

    def _hello(self, q: dict, ip: str, now: float) -> bytes:
        ident = _one(q, "d") or ""
        c_hex = _one(q, "c") or ""
        proof = _one(q, "m") or ""
        if not _HEX8.match(ident) or not _HEX32.match(c_hex):
            raise Refusal(Code.BAD_REQUEST, "bad hello")
        self._device_check(ident, now)
        dev = self.devices.get(ident)
        if not dev:
            self.log(f"a2: hello from unknown device {ident} at {ip}")
            raise self._refuse(ip, Code.UNKNOWN_DEVICE, "unknown device", True)
        cn = bytes.fromhex(c_hex)
        if not mac_equals(hello_proof(dev.key, cn), proof):
            self.log(f"a2: bad hello proof for device {ident} at {ip}")
            self._device_fail(ident, now)
            raise self._refuse(ip, Code.BAD_SIGNATURE, "bad hello proof", True)
        sn = secrets.token_bytes(16)
        sid = secrets.token_hex(4)
        while sid in self._sessions:
            sid = secrets.token_hex(4)
        s = _Session(sid, ident, session_key(dev.key, cn, sn), 0, now)
        self._sessions[sid] = s
        mine = sorted((x for x in self._sessions.values() if x.device == ident), key=lambda x: x.last_used)
        while len(mine) > SESSIONS_PER_DEVICE:
            self._sessions.pop(mine.pop(0).sid, None)

        lines = [f"sid {sid}", f"sn {sn.hex()}", f"ver {VERSION}", f"time {int(now // 1000)}"]
        agents = self.agents.agents()
        lines += [f"agent {a}" for a in agents]
        for a in agents:
            label = display_label(self.agents.display_name(a))
            if label and label != a:
                lines.append(f"name {a} {label}")
        payload = ("\n".join(lines) + "\n" + self._server_line()).encode("latin-1")
        self.log(f"a2: hello from {dev.name} ({ident}) at {ip}")
        return reply(Code.OK, hello_mac(s.sk, payload), payload)

    def _server_line(self) -> str:
        ident = re.sub(r"[^\x20-\x7e]", "?", self.ident)[:60]
        return f"server {ident}\n" if ident else ""

    def _end_sessions(self, device: str) -> None:
        for x in list(self._sessions.values()):
            if x.device == device:
                self._sessions.pop(x.sid, None)

    # ---- the conversation ----

    def _conv(self, device: str, agent: str) -> _Conversation:
        k = f"{device}:{agent}"
        c = self._convs.get(k)
        if c is None:
            c = _Conversation()
            self._convs[k] = c
            saved = None
            try:
                saved = self.convs.load(device, agent)
            except Exception as e:
                self.log(f"a2: could not read the log of {device}.{agent}: {e!r}")
            if saved:
                c.msgs, c.next = saved
                c.seen = c.next - 1
                return c
            for mine, text in _seed_messages(agent, self.seed):
                c.msgs.append(Msg(c.next, mine, hhmm(self.now() - (self.seed - c.next + 1) * 60_000), text))
                c.next += 1
            c.seen = c.next - 1
        return c

    def _append(self, device: str, agent: str, mine: bool, text: str) -> Msg:
        c = self._conv(device, agent)
        m = Msg(c.next, mine, hhmm(self.now()), text)
        c.next += 1
        c.msgs.append(m)
        if len(c.msgs) > LOG_KEEP:
            del c.msgs[:len(c.msgs) - LOG_KEEP]
        # A reply still arriving for a revoked device is not written back.
        if self.devices.get(device):
            try:
                self.convs.save(device, agent, c.msgs, c.next)
            except Exception as e:
                self.log(f"a2: could not save the log of {device}.{agent}: {e!r}")
        self._wake(device)
        return m

    def post(self, device: str, agent: str, text: str) -> int:
        """An answer, or a message the agent sends on its own (a reminder, a
        cron job's report): folded, cut into parts, and added to the
        conversation for the Apple's next poll. Returns its last part's number."""
        if not self.devices.get(device):
            raise LookupError(f"a2: no device {device}")
        if agent not in self.agents.agents():
            raise LookupError(f"a2: no agent {agent}")
        folded = for_apple(text)
        if not folded:
            raise ValueError("a2: nothing to send")
        n = 0
        for part in split(folded):
            n = self._append(device, agent, False, part).n
        return n

    def set_busy(self, device: str, agent: str, busy: bool) -> None:
        """Whether the agent is working on an answer for this conversation."""
        c = self._conv(device, agent)
        if c.busy != busy:
            c.busy = busy
            self._wake(device)

    def _wake(self, device: str) -> None:
        for f in self._waiters.pop(device, set()):
            if not f.done():
                f.set_result(None)

    async def _send(self, device: str, agent: str, body: bytes) -> bytes:
        if not body:
            raise Refusal(Code.BAD_REQUEST, "empty message")
        if any(b != 10 and (b < 32 or b > 126) for b in body):
            raise Refusal(Code.BAD_REQUEST, "not printable ASCII")
        m = self._append(device, agent, True, body.decode("latin-1"))
        self.set_busy(device, agent, True)
        await self.agents.deliver(device, agent, m.text)
        return msg_record(m)

    async def _poll(self, device: str, agent: str, q: dict) -> bytes:
        after = _int(q, "after", 0)
        wait = min(_int(q, "w", 0), MAX_WAIT_MS)
        c = self._conv(device, agent)
        c.seen = max(c.seen, after)
        busy_at_start = c.busy

        def has_news() -> bool:
            return any(m.n > after for m in c.msgs) or c.busy != busy_at_start

        if not has_news() and wait > 0:
            f = asyncio.get_running_loop().create_future()
            self._waiters.setdefault(device, set()).add(f)
            try:
                await asyncio.wait_for(f, wait / 1000)
            except asyncio.TimeoutError:
                pass
            finally:
                w = self._waiters.get(device)
                if w is not None:
                    w.discard(f)

        head = []
        for other in self.agents.agents():
            if other == agent:
                continue
            oc = self._convs.get(f"{device}:{other}")
            n = sum(1 for m in oc.msgs if not m.mine and m.n > oc.seen) if oc else 0
            if n:
                head.append(f"unread {other} {n}")
        fresh = [m for m in c.msgs if m.n > after]
        return _pack([f"busy {1 if c.busy else 0}", *head], fresh, False, len(fresh))

    def _hist(self, device: str, agent: str, q: dict) -> bytes:
        c = self._conv(device, agent)
        k = min(max(_int(q, "k", 20), 1), HIST_MAX)
        if "after" in q:
            # From a point forward, oldest first: after=0 is the conversation's
            # start. `more` says there are newer. Unlike poll, nothing is seen.
            if "before" in q:
                raise Refusal(Code.BAD_REQUEST, "before or after, not both")
            after = _int(q, "after", 0)
            newer = [m for m in c.msgs if m.n > after]
            return _pack([], newer[:k], False, len(newer))
        before = _int(q, "before", c.next)
        older = [m for m in c.msgs if m.n < before]
        return _pack([], older[-k:], True, len(older))

    # ---- lockouts ----

    def _refuse(self, ip: str, code: int, why: str, counts: bool) -> Refusal:
        if counts:
            now = self.now()
            r = self._ip_refusals.get(ip) or [0, now, 0]
            if now - r[1] > IP_WINDOW_MS:
                r[0], r[1] = 0, now
            r[0] += 1
            if r[0] >= IP_REFUSALS:
                r[2] = now + IP_BLOCK_MS
                self.log(f"a2: blocking {ip} for {IP_BLOCK_MS // 60_000} minutes")
            self._ip_refusals[ip] = r
        return Refusal(code, why)

    def _ip_check(self, ip: str, now: float) -> None:
        r = self._ip_refusals.get(ip)
        if r and r[2] > now:
            raise Refusal(Code.LOCKED, "address blocked")

    def _device_fail(self, ident: str, now: float) -> None:
        times, until = self._device_fails.get(ident, ([], 0))
        times = [t for t in times if now - t < DEVICE_WINDOW_MS] + [now]
        if len(times) >= DEVICE_FAILS:
            until = now + DEVICE_LOCK_MS
            times = []
            self.log(f"a2: locking device {ident} for {DEVICE_LOCK_MS // 60_000} minutes")
        self._device_fails[ident] = (times, until)

    def _device_check(self, ident: str, now: float) -> None:
        f = self._device_fails.get(ident)
        if f and f[1] > now:
            raise Refusal(Code.LOCKED, "device locked")


# ---- helpers ----

def _one(q: dict, name: str) -> Optional[str]:
    v = q.get(name)
    return v[0] if v else None


def _int(q: dict, name: str, default: int) -> int:
    v = _one(q, name)
    if v is None:
        return default
    if not v.isdigit():
        raise Refusal(Code.BAD_REQUEST, f"bad {name}")
    return int(v)


def _pack(lines: list[str], msgs: list[Msg], from_newest: bool, total: int) -> bytes:
    """A `more` line, the other lines, then as many whole messages as fit
    MAX_PAYLOAD. `more` is 1 when fewer than total went in. History fills from
    the newest, so the most recent older messages are the ones kept; the wire
    is always oldest first."""
    head = "".join(f"{l}\n" for l in lines).encode("latin-1")
    room = MAX_PAYLOAD - len(head) - len(b"more 0\n")
    kept: list[bytes] = []
    for m in (reversed(msgs) if from_newest else msgs):
        r = msg_record(m)
        if len(r) > room:
            break
        kept.append(r)
        room -= len(r)
    if from_newest:
        kept.reverse()
    more = f"more {1 if len(kept) < total else 0}\n".encode("latin-1")
    return more + head + b"".join(kept)


def _seed_messages(agent: str, n: int) -> list[tuple[bool, str]]:
    """Made-up history, for testing the client: as server.ts makes it."""
    out = []
    para = ("This answer is long on purpose, so that it wraps across several lines "
            "of the Apple's screen and the scrolling has something to work through.")
    for i in range(1, n + 1):
        hx = f"{i % 256:02x}"
        if i % 2:
            out.append((True, f"Question {i} for {agent}: what comes next?"))
        elif i % 10 == 0:
            out.append((False, f"Answer {i}, with code:\n\n        ldx #${hx}\nloop:   dex\n        bne loop\n        rts"))
        elif i % 6 == 0:
            out.append((False, f"Answer {i}, a long one.\n\n{para} {para}\n\n{para}"))
        else:
            out.append((False, f"Answer {i}: a short reply."))
    return out


# ---- a small HTTP/1.1 server, all the card needs ----

KEEP_ALIVE_S = 10.0          # longer than the card's 8 s cutoff
MAX_HEADER = 8192
DISCARD_MAX = 64 * 1024      # a longer body than this closes the connection


class HttpListener:
    """Requests in, replies out: Content-Length bodies, keep-alive, nothing
    else. Paths outside /a2/ get 404. Standard library only, like the IRC
    adapter, so the plugin needs nothing installed."""

    def __init__(self, handler: Callable[[Request], Awaitable[bytes]], max_body: int):
        self.handler = handler
        self.max_body = max_body
        self._server: Optional[asyncio.base_events.Server] = None
        self._conns: set[asyncio.StreamWriter] = set()

    async def start(self, host: str, port: int) -> None:
        self._server = await asyncio.start_server(self._serve, host, port, reuse_address=True)

    @property
    def port(self) -> int:
        assert self._server is not None
        return self._server.sockets[0].getsockname()[1]

    async def stop(self) -> None:
        if self._server is not None:
            self._server.close()
            for w in list(self._conns):
                w.close()
            try:
                await asyncio.wait_for(self._server.wait_closed(), 5)
            except asyncio.TimeoutError:
                pass
            self._server = None

    async def _serve(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        self._conns.add(writer)
        peer = writer.get_extra_info("peername")
        ip = peer[0] if peer else "?"
        try:
            while True:
                try:
                    head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), KEEP_ALIVE_S)
                except (asyncio.TimeoutError, asyncio.IncompleteReadError, asyncio.LimitOverrunError,
                        ConnectionError):
                    return
                if len(head) > MAX_HEADER:
                    return
                lines = head.decode("latin-1").split("\r\n")
                parts = lines[0].split(" ")
                if len(parts) != 3:
                    return
                method, target, version = parts
                headers: dict[str, str] = {}
                for line in lines[1:]:
                    if ":" in line:
                        k, _, v = line.partition(":")
                        headers[k.strip().lower()] = v.strip()
                try:
                    length = int(headers.get("content-length", "0"))
                except ValueError:
                    return
                if length < 0 or length > DISCARD_MAX or "chunked" in headers.get("transfer-encoding", ""):
                    return
                body: Optional[bytes] = await reader.readexactly(length) if length else b""
                if len(body) > self.max_body:
                    body = None
                close = headers.get("connection", "").lower() == "close" or version == "HTTP/1.0"
                if target.startswith("/a2/"):
                    out = await self.handler(Request(method, target, headers, body, ip))
                    status = "200 OK"
                else:
                    out, status = b"", "404 Not Found"
                writer.write((f"HTTP/1.1 {status}\r\nContent-Type: text/plain; charset=us-ascii\r\n"
                              f"Content-Length: {len(out)}\r\n"
                              f"Connection: {'close' if close else 'keep-alive'}\r\n\r\n").encode("latin-1") + out)
                await writer.drain()
                if close:
                    return
        except (ConnectionError, asyncio.IncompleteReadError):
            return
        finally:
            self._conns.discard(writer)
            writer.close()
