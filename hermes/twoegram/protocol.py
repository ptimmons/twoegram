"""The wire format of docs/PROTOCOL.md: keys, signatures, replies and records.

A port of openclaw/src/protocol.ts. No I/O here, so the tests and the server
share it. tests/vectors.json, made by the TypeScript, checks the two agree.
"""

from __future__ import annotations

import hashlib
import hmac as _hmac
import re
import secrets
import struct
from dataclasses import dataclass
from typing import Optional

VERSION = 1
MAX_PAYLOAD = 1900
MAX_TEXT = 1500
MAX_BODY = 1024
MAX_TARGET = 512
MAX_WAIT_MS = 4000
DISPLAY_NAME_MAX = 16


class Code:
    OK = 0
    BAD_REQUEST = 1
    UNKNOWN_DEVICE = 2
    BAD_SIGNATURE = 3
    NO_SESSION = 4
    REPLAY = 5
    LOCKED = 6
    UNKNOWN_AGENT = 7
    TOO_LARGE = 8
    BUSY = 9


AGENT_NAME = re.compile(r"[a-z0-9_-]{1,16}\Z")

# ---- the device key, written for typing ----

ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
# Crockford's check symbols: the 32 above, then five more for 32..36.
CHECK = ALPHABET + "*~$=U"


def new_key() -> bytes:
    return secrets.token_bytes(16)


def _check_char(values: list[int]) -> int:
    """Weighted sum modulo 37: a prime above every weight and difference, so
    any one wrong character, or two neighbours swapped, changes it."""
    return sum((i + 1) * v for i, v in enumerate(values)) % 37


def format_key(key: bytes) -> str:
    """16 bytes as 26 base-32 characters and a check character, in groups."""
    if len(key) != 16:
        raise ValueError("a device key is 16 bytes")
    n = int.from_bytes(key, "big") << 2          # 128 bits and 2 zero bits: 130 = 26 x 5
    values = [(n >> (5 * (25 - i))) & 31 for i in range(26)]
    s = "".join(ALPHABET[v] for v in values) + CHECK[_check_char(values)]
    return "-".join(s[i:i + 5] for i in range(0, len(s), 5))


def parse_key(text: str) -> Optional[bytes]:
    """The written form back to 16 bytes, or None if it does not check."""
    clean = re.sub(r"[\s-]", "", text.upper()).replace("O", "0").replace("I", "1").replace("L", "1")
    if len(clean) != 27:
        return None
    values = []
    for ch in clean[:26]:
        v = ALPHABET.find(ch)
        if v < 0:
            return None
        values.append(v)
    if CHECK.find(clean[26]) != _check_char(values):
        return None
    if values[25] & 3:
        return None                              # the two padding bits must be zero
    n = 0
    for v in values:
        n = (n << 5) | v
    return (n >> 2).to_bytes(16, "big")


KEY_NAME = re.compile(r"[a-z0-9_-]{1,16}\Z")


def derive_key(master: bytes, name: str) -> bytes:
    """The key the Apple derives for gateway `name` from the card's master
    secret: the first 16 bytes of HMAC(M, "twoegram-gw" || name)
    (docs/RELAY.md, part 1). Only the Apple holds M; a gateway sees an
    ordinary device key. Here for the tests, the vectors and the client."""
    if len(master) != 16:
        raise ValueError("a master secret is 16 bytes")
    if not KEY_NAME.match(name):
        raise ValueError("a key name is 1 to 16 of a-z 0-9 - _")
    return hmac(master, "twoegram-gw", name)[:16]


def device_id(key: bytes) -> str:
    return hashlib.sha256(b"twoegram-id" + key).digest()[:4].hex()


# ---- signatures ----

def _b(p: bytes | str) -> bytes:
    return p if isinstance(p, bytes) else p.encode("latin-1")


def hmac(key: bytes, *parts: bytes | str) -> bytes:
    h = _hmac.new(key, digestmod=hashlib.sha256)
    for p in parts:
        h.update(_b(p))
    return h.digest()


def u32(n: int) -> bytes:
    return struct.pack(">I", n & 0xFFFFFFFF)


def hello_proof(key: bytes, client_nonce: bytes) -> bytes:
    return hmac(key, "hello", client_nonce)


def session_key(key: bytes, client_nonce: bytes, server_nonce: bytes) -> bytes:
    return hmac(key, "session", client_nonce, server_nonce)


def request_mac(sk: bytes, seq: int, method: str, target: str, body: bytes) -> bytes:
    return hmac(sk, "q", u32(seq), method, " ", target, "\n", body)


def reply_mac(sk: bytes, seq: int, payload: bytes) -> bytes:
    return hmac(sk, "r", u32(seq), payload)


def hello_mac(sk: bytes, payload: bytes) -> bytes:
    return hmac(sk, "h", payload)


_HEX64 = re.compile(r"[0-9a-f]{64}\Z")


def mac_equals(mac: bytes, hexed: Optional[str]) -> bool:
    """Constant-time comparison of a signature with its 64-hex-digit form."""
    if not hexed or not _HEX64.match(hexed):
        return False
    return _hmac.compare_digest(mac, bytes.fromhex(hexed))


# ---- replies ----

def reply(code: int, mac: Optional[bytes], payload: bytes) -> bytes:
    """The bytes of a reply: the header line, then the payload."""
    head = f"A2 {VERSION} {code} {mac.hex() if mac else '-'}\n"
    return head.encode("latin-1") + payload


@dataclass
class Msg:
    n: int
    mine: bool
    time: str
    text: str

    def to_json(self) -> dict:
        return {"n": self.n, "mine": self.mine, "time": self.time, "text": self.text}

    @staticmethod
    def from_json(d: dict) -> "Msg":
        return Msg(int(d["n"]), bool(d["mine"]), str(d["time"]), str(d["text"]))


def msg_record(m: Msg) -> bytes:
    text = m.text.encode("latin-1")
    return f"msg {m.n} {1 if m.mine else 0} {m.time} {len(text)}\n".encode("latin-1") + text + b"\n"


@dataclass
class Parsed:
    version: int
    code: int
    mac: Optional[str]
    payload: bytes
    lines: list[tuple[str, str]]
    msgs: list[Msg]

    def line(self, word: str) -> Optional[str]:
        return next((v for k, v in self.lines if k == word), None)


def parse_reply(body: bytes) -> Parsed:
    """Parse a reply as the Apple will. Raises ValueError on anything malformed."""
    nl = body.find(b"\n")
    if nl < 0:
        raise ValueError("no header line")
    head = body[:nl].decode("latin-1").split(" ")
    if len(head) != 4 or head[0] != "A2":
        raise ValueError("bad header line")
    payload = body[nl + 1:]
    lines: list[tuple[str, str]] = []
    msgs: list[Msg] = []
    i = 0
    while i < len(payload):
        e = payload.find(b"\n", i)
        if e < 0:
            raise ValueError("unterminated line")
        line = payload[i:e].decode("latin-1")
        i = e + 1
        word, _, value = line.partition(" ")
        if word == "msg":
            f = value.split(" ")
            if len(f) != 4:
                raise ValueError("bad msg line")
            try:
                ln = int(f[3])
            except ValueError:
                raise ValueError("bad msg length") from None
            if ln < 0 or i + ln + 1 > len(payload) or payload[i + ln] != 10:
                raise ValueError("bad msg length")
            msgs.append(Msg(int(f[0]), f[1] == "1", f[2], payload[i:i + ln].decode("latin-1")))
            i += ln + 1
        else:
            lines.append((word, value))
    return Parsed(int(head[1]), int(head[2]), None if head[3] == "-" else head[3], payload, lines, msgs)
