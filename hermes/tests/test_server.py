"""The endpoints over real HTTP, with a client that does what the Apple will:
verify every reply before believing it. A port of plugin/test/server.test.ts."""

from __future__ import annotations

import asyncio
import os
import secrets
import stat

import pytest

from twoegram.protocol import (
    MAX_PAYLOAD, Code, device_id, hello_mac, hello_proof, mac_equals, parse_key, parse_reply, reply_mac,
    request_mac, session_key,
)
from twoegram.echo import EchoAgent
from twoegram.server import A2Server, HttpListener
from twoegram.store import ConvStore, DeviceStore


class Raw:
    """A keep-alive connection, as the card keeps one."""

    def __init__(self, port):
        self.port = port
        self.r = self.w = None

    async def request(self, method, target, headers=None, body=b""):
        if self.w is None:
            self.r, self.w = await asyncio.open_connection("127.0.0.1", self.port)
        h = {"Host": "a2", "Content-Length": str(len(body)), **(headers or {})}
        self.w.write((f"{method} {target} HTTP/1.1\r\n" + "".join(f"{k}: {v}\r\n" for k, v in h.items())
                      + "\r\n").encode("latin-1") + body)
        await self.w.drain()
        head = (await self.r.readuntil(b"\r\n\r\n")).decode("latin-1")
        status = int(head.split(" ")[1])
        length = int(next(l.split(":")[1] for l in head.split("\r\n") if l.lower().startswith("content-length")))
        return status, await self.r.readexactly(length)

    def close(self):
        if self.w:
            self.w.close()


class Client:
    def __init__(self, port, key):
        self.raw = Raw(port)
        self.key = key
        self.id = device_id(key)
        self.sid, self.sk, self.seq, self.agents = "", b"", 0, []

    async def hello(self):
        cn = secrets.token_bytes(16)
        proof = hello_proof(self.key, cn).hex()
        _, body = await self.raw.request("GET", f"/a2/hello?d={self.id}&c={cn.hex()}&m={proof}")
        r = parse_reply(body)
        if r.code != Code.OK:
            return r
        sk = session_key(self.key, cn, bytes.fromhex(r.line("sn")))
        assert mac_equals(hello_mac(sk, r.payload), r.mac or "")
        self.sk, self.sid, self.seq = sk, r.line("sid"), 0
        self.agents = [v for k, v in r.lines if k == "agent"]
        return r

    async def call(self, method, target, body="", seq=None, tamper=False):
        if seq is None:
            self.seq += 1
            seq = self.seq
        b = body.encode("latin-1")
        mac = request_mac(self.sk, seq, method, target, b).hex()
        if tamper:
            mac = ("1" if mac[0] == "0" else "0") + mac[1:]
        status, raw = await self.raw.request(method, target, {"X-A2-Session": self.sid, "X-A2-Seq": str(seq),
                                                              "X-A2-Mac": mac}, b)
        assert status == 200
        r = parse_reply(raw)
        r.genuine = r.mac is not None and mac_equals(reply_mac(self.sk, seq, r.payload), r.mac)
        return r


class World:
    def __init__(self, tmp_path):
        self.dir = tmp_path
        self.clock = [1_763_215_260_000.0]     # 2025-11-15 14:01 UTC
        self.listener = None
        self.clients = []

    async def start(self, agents=None, convs=False):
        if self.listener:
            await self.stop()
        self.store = DeviceStore(self.dir / "devices.json")
        self.agents = agents or EchoAgent()
        self.a2 = A2Server(self.store, self.agents, now=lambda: self.clock[0],
                           convs=ConvStore(self.dir / "conversations") if convs else None)
        if hasattr(self.agents, "server"):
            self.agents.server = self.a2
        self.listener = HttpListener(self.a2.handle, 1024)
        await self.listener.start("127.0.0.1", 0)
        self.port = self.listener.port

    async def stop(self):
        for c in self.clients:
            c.raw.close()
        self.clients = []
        await self.listener.stop()
        self.listener = None

    def pair(self):
        return parse_key(self.store.pair("test")[1])

    def client(self, key):
        c = Client(self.port, key)
        self.clients.append(c)
        return c

    def tick(self, ms):
        self.clock[0] += ms


@pytest.fixture
async def w(tmp_path):
    world = World(tmp_path)
    await world.start()
    yield world
    if world.listener:
        await world.stop()


@pytest.fixture
async def c(w):
    c = w.client(w.pair())
    await c.hello()
    return c


# ---- pairing and hello ----

async def test_keys_file_private(w):
    w.pair()
    assert stat.S_IMODE(os.stat(w.store.path).st_mode) == 0o600


async def test_hello_session_and_agents(w):
    c = w.client(w.pair())
    r = await c.hello()
    assert r.code == Code.OK
    assert len(c.sid) == 8
    assert c.agents == ["main", "coder"]
    assert [v for k, v in r.lines if k == "name"] == ["main Echo"]


async def test_names_as_the_e_can_show(w):
    await w.start(EchoAgent(["main", "coder", "home", "same"], 0.02, {
        "main": "Pinchy ✨", "coder": "  Señor\nCódigo  the Magnificent", "home": "\U0001F3E0",
        "same": "same"}))
    c = w.client(w.pair())
    r = await c.hello()
    assert [v for k, v in r.lines if k == "name"] == ["main Pinchy", "coder Senor Codigo the"]


async def test_unknown_device_and_wrong_proof_unsigned(w):
    r1 = await w.client(secrets.token_bytes(16)).hello()
    assert (r1.code, r1.mac) == (Code.UNKNOWN_DEVICE, None)
    real = w.pair()
    impostor = w.client(secrets.token_bytes(16))
    impostor.id = device_id(real)
    r2 = await impostor.hello()
    assert (r2.code, r2.mac) == (Code.BAD_SIGNATURE, None)


async def test_lock_after_five_bad_proofs(w):
    real = w.pair()
    impostor = w.client(secrets.token_bytes(16))
    impostor.id = device_id(real)
    for _ in range(5):
        await impostor.hello()
    good = w.client(real)
    assert (await good.hello()).code == Code.LOCKED
    w.tick(16 * 60_000)
    assert (await good.hello()).code == Code.OK


async def test_block_address_after_twenty_refusals(w):
    for _ in range(20):
        await w.client(secrets.token_bytes(16)).hello()
    good = w.client(w.pair())
    assert (await good.hello()).code == Code.LOCKED
    w.tick(6 * 60_000)
    assert (await good.hello()).code == Code.OK


async def test_four_sessions_a_device(w):
    key = w.pair()
    first = w.client(key)
    await first.hello()
    for _ in range(4):
        w.tick(1000)
        await w.client(key).hello()
    assert (await first.call("GET", "/a2/poll?a=main")).code == Code.NO_SESSION


async def test_hello_names_the_server(w):
    w.a2.ident = "hermes-a2 1.0.0"
    r = await w.client(secrets.token_bytes(16)).hello()
    assert r.line("server") == "hermes-a2 1.0.0"


# ---- signed requests ----

async def test_genuine_reply(c):
    r = await c.call("GET", "/a2/poll?a=main&after=0")
    assert (r.code, r.genuine) == (Code.OK, True)


async def test_bad_signature_unsigned(c):
    r = await c.call("GET", "/a2/poll?a=main", tamper=True)
    assert (r.code, r.mac) == (Code.BAD_SIGNATURE, None)


async def test_replay_refused_and_not_acted_on(c):
    assert (await c.call("POST", "/a2/send?a=main", "once")).code == Code.OK
    again = await c.call("POST", "/a2/send?a=main", "once", seq=c.seq)
    assert (again.code, again.genuine) == (Code.REPLAY, True)
    h = await c.call("GET", "/a2/hist?a=main&k=50")
    assert len([m for m in h.msgs if m.mine]) == 1


async def test_session_not_usable_by_another_key(w, c):
    other = w.client(w.pair())
    other.sid, other.sk = c.sid, secrets.token_bytes(32)
    assert (await other.call("GET", "/a2/poll?a=main")).code == Code.BAD_SIGNATURE


async def test_idle_session_ends(w, c):
    w.tick(31 * 60_000)
    assert (await c.call("GET", "/a2/poll?a=main")).code == Code.NO_SESSION
    assert (await c.hello()).code == Code.OK
    assert (await c.call("GET", "/a2/poll?a=main")).code == Code.OK


async def test_unknown_agent_signed(c):
    r = await c.call("GET", "/a2/poll?a=nobody")
    assert (r.code, r.genuine) == (Code.UNKNOWN_AGENT, True)


async def test_body_too_long_unsigned(c):
    r = await c.call("POST", "/a2/send?a=main", "x" * 1025)
    assert (r.code, r.mac) == (Code.TOO_LARGE, None)


async def test_text_the_e_could_not_type(c):
    r = await c.call("POST", "/a2/send?a=main", "café")
    assert (r.code, r.genuine) == (Code.BAD_REQUEST, True)


async def test_request_line_too_long(c):
    assert (await c.call("GET", "/a2/poll?a=main&x=" + "y" * 520)).code == Code.TOO_LARGE


async def test_not_a2_is_404(w):
    raw = Raw(w.port)
    status, _ = await raw.request("GET", "/")
    raw.close()
    assert status == 404


# ---- the conversation ----

async def test_send_and_long_poll_the_answer(c):
    sent = await c.call("POST", "/a2/send?a=main", "Hello\nthere")
    assert [(m.n, m.mine, m.text) for m in sent.msgs] == [(1, True, "Hello\nthere")]
    p = await c.call("GET", "/a2/poll?a=main&after=1&w=2000")
    assert p.genuine and len(p.msgs) == 1
    assert (p.msgs[0].n, p.msgs[0].mine) == (2, False)
    assert p.msgs[0].text == 'main heard you -- "Hello\nthere" \x02'


async def test_busy_then_done(w):
    class Slow:
        server = None

        def agents(self):
            return ["main"]

        def display_name(self, a):
            return None

        async def deliver(self, d, a, t):
            self.d = d

    slow = Slow()
    await w.start(slow)
    c = w.client(w.pair())
    await c.hello()
    await c.call("POST", "/a2/send?a=main", "think hard")
    busy = await c.call("GET", "/a2/poll?a=main&after=1")
    assert ("busy", "1") in busy.lines
    waiting = asyncio.create_task(c.call("GET", "/a2/poll?a=main&after=1&w=3000"))
    await asyncio.sleep(0.05)
    w.a2.post(slow.d, "main", "Done thinking.")
    w.a2.set_busy(slow.d, "main", False)
    r = await waiting
    assert [m.text for m in r.msgs] == ["Done thinking."]
    after = await c.call("GET", "/a2/poll?a=main&after=2")
    assert ("busy", "0") in after.lines


async def test_poll_wakes_when_busy_ends_with_nothing_said(w):
    class Silent:
        server = None

        def agents(self):
            return ["main"]

        def display_name(self, a):
            return None

        async def deliver(self, d, a, t):
            self.d = d

    s = Silent()
    await w.start(s)
    c = w.client(w.pair())
    await c.hello()
    await c.call("POST", "/a2/send?a=main", "hm")
    waiting = asyncio.create_task(c.call("GET", "/a2/poll?a=main&after=1&w=3000"))
    await asyncio.sleep(0.05)
    t0 = asyncio.get_running_loop().time()
    w.a2.set_busy(s.d, "main", False)
    r = await waiting
    assert asyncio.get_running_loop().time() - t0 < 1
    assert ("busy", "0") in r.lines and r.msgs == []


async def test_poll_returns_at_once_without_wait(c):
    loop = asyncio.get_running_loop()
    t0 = loop.time()
    r = await c.call("GET", "/a2/poll?a=main&after=0")
    assert loop.time() - t0 < 0.5
    assert r.msgs == [] and ("more", "0") in r.lines


async def test_long_answer_in_parts_paged(c):
    await c.call("POST", "/a2/send?a=coder", "say something long")
    await asyncio.sleep(0.1)
    after, texts = 1, []
    for _ in range(20):
        r = await c.call("GET", f"/a2/poll?a=coder&after={after}")
        assert r.genuine and len(r.payload) <= MAX_PAYLOAD
        for m in r.msgs:
            assert len(m.text) <= 1500
            assert all(ch in "\n\x02" or " " <= ch <= "~" for ch in m.text)
            texts.append(m.text)
            after = m.n
        if r.line("more") == "0":
            break
    assert len(texts) > 3
    assert "Part 5" in "\n".join(texts)
    assert "loop:   sta $0200,y" in "\n".join(texts)


async def test_unread_counts(c):
    await c.call("POST", "/a2/send?a=coder", "hi coder")
    await asyncio.sleep(0.1)
    on_main = await c.call("GET", "/a2/poll?a=main&after=0")
    assert ("unread", "coder 1") in on_main.lines
    await c.call("GET", "/a2/poll?a=coder&after=2")
    again = await c.call("GET", "/a2/poll?a=main&after=0")
    assert not any(k == "unread" for k, _ in again.lines)


async def test_what_a_poll_returns_is_seen(c):
    # The answer comes in a poll from before it; the Apple switches agents
    # before polling coder again. coder has nothing unread.
    r = await c.call("POST", "/a2/send?a=coder", "hi coder")
    await asyncio.sleep(0.1)
    got = await c.call("GET", f"/a2/poll?a=coder&after={r.msgs[0].n}")
    assert got.msgs and not got.msgs[-1].mine
    on_main = await c.call("GET", "/a2/poll?a=main&after=0")
    assert not any(k == "unread" for k, _ in on_main.lines)


async def test_history_newest_page_first(c):
    for i in range(1, 7):
        await c.call("POST", "/a2/send?a=main", f"message {i}")
    await asyncio.sleep(0.1)
    h = await c.call("GET", "/a2/hist?a=main&before=12&k=3")
    assert [m.n for m in h.msgs] == [9, 10, 11] and ("more", "1") in h.lines
    a = await c.call("GET", "/a2/hist?a=main&before=4&k=50")
    assert [m.n for m in a.msgs] == [1, 2, 3] and ("more", "0") in a.lines


async def test_history_from_the_start(c):
    for i in range(1, 7):
        await c.call("POST", "/a2/send?a=main", f"message {i}")
    await asyncio.sleep(0.1)
    first = await c.call("GET", "/a2/hist?a=main&after=0&k=3")
    assert first.code == 0 and [m.n for m in first.msgs] == [1, 2, 3] and ("more", "1") in first.lines
    rest = await c.call("GET", "/a2/hist?a=main&after=9&k=50")
    assert [m.n for m in rest.msgs] == [10, 11, 12] and ("more", "0") in rest.lines
    assert (await c.call("GET", "/a2/hist?a=main&after=0&before=5")).code == 1


async def test_history_from_start_not_seen(c):
    await c.call("POST", "/a2/send?a=coder", "hi coder")
    await asyncio.sleep(0.1)
    await c.call("GET", "/a2/hist?a=coder&after=0&k=50")
    on_main = await c.call("GET", "/a2/poll?a=main&after=0")
    assert ("unread", "coder 1") in on_main.lines


async def test_devices_kept_apart(w, c):
    await c.call("POST", "/a2/send?a=main", "mine alone")
    other = w.client(w.pair())
    await other.hello()
    assert (await other.call("GET", "/a2/hist?a=main&k=50")).msgs == []


# ---- revoking, restarting, and messages the agent starts ----

async def test_revoked_by_another_process(w, c):
    assert (await c.call("GET", "/a2/poll?a=main")).code == Code.OK
    await asyncio.sleep(0.02)
    assert DeviceStore(w.store.path).revoke(c.id)
    assert (await c.call("GET", "/a2/poll?a=main")).code == Code.NO_SESSION
    assert (await c.hello()).code == Code.UNKNOWN_DEVICE


async def test_paired_by_another_process(w):
    written = DeviceStore(w.store.path).pair("later")[1]
    assert (await w.client(parse_key(written)).hello()).code == Code.OK


async def test_conversations_across_restart(w):
    await w.start(convs=True)
    key = w.pair()
    c = w.client(key)
    await c.hello()
    await c.call("POST", "/a2/send?a=main", "before the restart")
    await asyncio.sleep(0.1)
    f = w.dir / "conversations" / f"{c.id}.main.json"
    assert stat.S_IMODE(os.stat(f).st_mode) == 0o600
    await w.start(convs=True)
    c = w.client(key)
    await c.hello()
    h = await c.call("GET", "/a2/hist?a=main&k=50")
    assert h.msgs[0].text == "before the restart" and "before the restart" in h.msgs[1].text
    assert (await c.call("POST", "/a2/send?a=main", "after")).msgs[0].n == 3


async def test_reads_the_typescript_plugins_files(w):
    # The same files as the OpenClaw plugin: a device paired there works here.
    (w.dir / "devices.json").write_text(
        '{\n  "version": 1,\n  "devices": [\n    {\n      "id": "' + device_id(bytes(16)) +
        '",\n      "key": "' + "00" * 16 + '",\n      "name": "From OpenClaw",\n'
        '      "created": "2026-11-16T10:00:00.000Z"\n    }\n  ]\n}\n')
    os.chmod(w.dir / "devices.json", 0o600)
    (w.dir / "conversations").mkdir()
    (w.dir / "conversations" / f"{device_id(bytes(16))}.main.json").write_text(
        '{"version":1,"next":3,"msgs":[{"n":1,"mine":true,"time":"10:00","text":"hi"},'
        '{"n":2,"mine":false,"time":"10:01","text":"hello"}]}\n')
    await w.start(convs=True)
    c = w.client(bytes(16))
    assert (await c.hello()).code == Code.OK
    h = await c.call("GET", "/a2/hist?a=main&k=50")
    assert [m.text for m in h.msgs] == ["hi", "hello"]


async def test_drop_on_revoke(w):
    await w.start(convs=True)
    c = w.client(w.pair())
    await c.hello()
    await c.call("POST", "/a2/send?a=coder", "x")
    await asyncio.sleep(0.1)
    convs = ConvStore(w.dir / "conversations")
    assert any(n.startswith(c.id) for n in os.listdir(convs.dir))
    convs.drop(c.id)
    assert not (convs.dir / f"{c.id}.coder.json").exists()


async def test_post_folded_and_in_parts(w, c):
    n = w.a2.post(c.id, "main", "**Reminder:** water the plants \U0001F331\n\n" + "More. " * 400)
    assert n > 1
    p = await c.call("GET", "/a2/poll?a=main&after=0")
    assert ("more", "1") in p.lines
    assert p.msgs[0].text.startswith("Reminder: water the plants")
    rest = await c.call("GET", f"/a2/poll?a=main&after={p.msgs[-1].n}")
    assert rest.msgs[-1].n == n
    assert all(not m.mine for m in p.msgs + rest.msgs)
    with pytest.raises(LookupError, match="no device"):
        w.a2.post("00000000", "main", "hi")
    with pytest.raises(LookupError, match="no agent"):
        w.a2.post(c.id, "nobody", "hi")
