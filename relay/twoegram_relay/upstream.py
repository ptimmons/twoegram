"""One upstream gateway, as the relay sees it: an a2 client (docs/PROTOCOL.md)
paired with that gateway like any Apple.

Two sessions, each on its own keep-alive connection, because sequence numbers
must rise within a session and a long poll would hold up everything else:
"poll" carries the Apple's long polls, "call" the rest. Each says hello again
when the gateway forgets it (NO_SESSION, REPLAY) or it has been idle for 25
minutes (the gateway's limit is 30). Every reply's signature is checked
before anything in it is used.
"""

from __future__ import annotations

import asyncio
import secrets
import time
from dataclasses import dataclass
from typing import Callable, Optional
from urllib.parse import urlsplit

from twoegram.protocol import (
    Code, Parsed, device_id, hello_mac, hello_proof, mac_equals, parse_reply, reply_mac, request_mac,
    session_key,
)

IDLE_REHELLO_S = 25 * 60
HELLO_TIMEOUT_S = 4.0
CALL_TIMEOUT_S = 6.0
CONNECT_TIMEOUT_S = 3.0


class Unreachable(Exception):
    """The gateway did not answer, or not as a gateway."""


class Refused(Exception):
    """The gateway refused the relay itself: its key, a lockout. code is the
    gateway's."""

    def __init__(self, code: int, why: str):
        super().__init__(why)
        self.code = code
        self.why = why


@dataclass
class _Chan:
    reader: Optional[asyncio.StreamReader] = None
    writer: Optional[asyncio.StreamWriter] = None
    sid: str = ""
    sk: bytes = b""
    seq: int = 0
    used: float = 0.0
    lock: Optional[asyncio.Lock] = None     # made in the event loop that uses it

    def guard(self) -> asyncio.Lock:
        if self.lock is None:
            self.lock = asyncio.Lock()
        return self.lock


class Upstream:
    def __init__(self, name: str, url: str, key: bytes, *,
                 log: Callable[[str], None] = lambda line: None):
        u = urlsplit(url)
        if u.scheme != "http" or not u.hostname:
            raise ValueError(f"{name}: the address is http://host:port")
        self.name = name
        self.url = url
        self.host = u.hostname
        self.port = u.port or 8417
        self.key = key
        self.id = device_id(key)
        self.log = log
        self.agents: list[str] = []                 # as the gateway names them
        self.labels: dict[str, str] = {}
        self.ok = False                             # the last exchange worked
        self.trouble = ""                           # why not, for the Apple and the log
        self._chans = {"poll": _Chan(), "call": _Chan()}

    # ---- HTTP ----

    async def _close(self, ch: _Chan) -> None:
        if ch.writer is not None:
            ch.writer.close()
        ch.reader = ch.writer = None

    async def _http(self, ch: _Chan, method: str, target: str, headers: dict[str, str],
                    body: bytes, timeout: float) -> bytes:
        """One request on the channel's connection, opened again if it has
        gone. The reply's body, whatever its status (the protocol always
        says 200)."""
        head = (f"{method} {target} HTTP/1.1\r\nHost: {self.host}:{self.port}\r\n"
                f"Content-Length: {len(body)}\r\nConnection: keep-alive\r\n"
                + "".join(f"{k}: {v}\r\n" for k, v in headers.items()) + "\r\n").encode("latin-1")
        for attempt in (0, 1):
            fresh = ch.writer is None
            try:
                if fresh:
                    ch.reader, ch.writer = await asyncio.wait_for(
                        asyncio.open_connection(self.host, self.port), CONNECT_TIMEOUT_S)
                ch.writer.write(head + body)
                await ch.writer.drain()
                return await asyncio.wait_for(self._read_reply(ch.reader), timeout)
            except (OSError, asyncio.IncompleteReadError, asyncio.TimeoutError, ValueError) as e:
                await self._close(ch)
                # A kept connection the gateway had closed: once more, fresh.
                if fresh or attempt or isinstance(e, asyncio.TimeoutError):
                    raise Unreachable(_why(e)) from None
        raise Unreachable("no answer")

    @staticmethod
    async def _read_reply(reader: asyncio.StreamReader) -> bytes:
        head = await reader.readuntil(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        if not lines[0].startswith("HTTP/1."):
            raise ValueError("not HTTP")
        length = None
        for line in lines[1:]:
            k, _, v = line.partition(":")
            if k.strip().lower() == "content-length":
                length = int(v.strip())
        if length is None or length < 0 or length > 8192:
            raise ValueError("no usable Content-Length")
        return await reader.readexactly(length)

    # ---- the protocol ----

    async def _hello(self, ch: _Chan) -> None:
        cn = secrets.token_bytes(16)
        target = f"/a2/hello?d={self.id}&c={cn.hex()}&m={hello_proof(self.key, cn).hex()}"
        body = await self._http(ch, "GET", target, {}, b"", HELLO_TIMEOUT_S)
        r = _parse(body)
        if r.code != Code.OK:
            why = r.line("why") or f"code {r.code}"
            if r.code in (Code.UNKNOWN_DEVICE, Code.BAD_SIGNATURE):
                raise Refused(r.code, f"{self.name} refused the relay's key")
            if r.code == Code.LOCKED:
                raise Refused(r.code, f"{self.name} has locked the relay out for now")
            raise Unreachable(f"refused the hello: {why}")
        sn = r.line("sn") or ""
        try:
            sk = session_key(self.key, cn, bytes.fromhex(sn))
        except ValueError:
            raise Unreachable("a hello with no server nonce") from None
        if not mac_equals(hello_mac(sk, r.payload), r.mac):
            raise Unreachable("a hello whose signature did not check")
        ch.sid, ch.sk, ch.seq, ch.used = r.line("sid") or "", sk, 0, time.monotonic()
        self.agents = [v for k, v in r.lines if k == "agent"]
        self.labels = {}
        for k, v in r.lines:
            if k == "name":
                a, _, label = v.partition(" ")
                if a in self.agents and label:
                    self.labels[a] = label

    async def call(self, kind: str, method: str, target: str, body: bytes = b"",
                   timeout: float = CALL_TIMEOUT_S) -> Parsed:
        """A signed request on channel kind ("poll" or "call"); its reply,
        signature checked, whatever its code. Raises Unreachable or Refused
        for trouble with the gateway itself, and keeps ok and trouble."""
        ch = self._chans[kind]
        async with ch.guard():
            try:
                r = await self._call(ch, method, target, body, timeout)
            except (Unreachable, Refused) as e:
                self._trouble(e)
                raise
            if not self.ok:
                self.log(f"relay: {self.name}: answering")
            self.ok, self.trouble = True, ""
            return r

    async def _call(self, ch: _Chan, method: str, target: str, body: bytes, timeout: float) -> Parsed:
        for attempt in (0, 1):
            if not ch.sid or time.monotonic() - ch.used > IDLE_REHELLO_S:
                await self._hello(ch)
            ch.seq += 1
            seq = ch.seq
            headers = {"X-A2-Session": ch.sid, "X-A2-Seq": str(seq),
                       "X-A2-Mac": request_mac(ch.sk, seq, method, target, body).hex()}
            raw = await self._http(ch, method, target, headers, body, timeout)
            ch.used = time.monotonic()
            r = _parse(raw)
            if r.mac is None:
                # Unsigned: the gateway could not tell the request was ours.
                if r.code == Code.NO_SESSION and not attempt:
                    ch.sid = ""
                    continue
                if r.code in (Code.UNKNOWN_DEVICE, Code.BAD_SIGNATURE):
                    ch.sid = ""
                    raise Refused(r.code, f"{self.name} refused the relay's key")
                if r.code == Code.LOCKED:
                    raise Refused(r.code, f"{self.name} has locked the relay out for now")
                raise Unreachable(r.line("why") or "an unsigned refusal")
            if not mac_equals(reply_mac(ch.sk, seq, r.payload), r.mac):
                raise Unreachable("a reply whose signature did not check")
            if r.code == Code.REPLAY and not attempt:
                ch.sid = ""
                continue
            return r
        raise Unreachable("the session would not start again")

    async def refresh(self) -> None:
        """Say hello again on the call channel, for a fresh agent list."""
        ch = self._chans["call"]
        async with ch.guard():
            try:
                await self._hello(ch)
            except (Unreachable, Refused) as e:
                self._trouble(e)
                raise
            if not self.ok:
                self.log(f"relay: {self.name}: answering, agents {' '.join(self.agents) or 'none'}")
            self.ok, self.trouble = True, ""

    def _trouble(self, e: Exception) -> None:
        """Note trouble, logging it when it is new. A Refused names the
        gateway already (it is what the Apple is told)."""
        what = str(e) if isinstance(e, Refused) else f"{self.name}: {e}"
        if self.ok or self.trouble != what:
            self.log(f"relay: {what}")
        self.ok, self.trouble = False, what

    async def close(self) -> None:
        for ch in self._chans.values():
            await self._close(ch)


def _parse(body: bytes) -> Parsed:
    try:
        return parse_reply(body)
    except (ValueError, UnicodeDecodeError):
        raise Unreachable("not an a2 gateway") from None


def _why(e: BaseException) -> str:
    if isinstance(e, asyncio.TimeoutError):
        return "no answer in time"
    if isinstance(e, ConnectionRefusedError):
        return "connection refused"
    return str(e) or type(e).__name__
