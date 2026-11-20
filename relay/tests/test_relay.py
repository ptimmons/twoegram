"""The relay between a client that checks everything as the Apple does
(hermes/tests/test_server.py's) and two real a2 servers with echo agents, all
over real HTTP on this machine."""

from __future__ import annotations

import asyncio

import pytest

from test_server import Client
from twoegram.echo import EchoAgent
from twoegram.protocol import Code, MAX_PAYLOAD, parse_key
from twoegram.server import A2Server, HttpListener
from twoegram.store import DeviceStore
from twoegram_relay import cli
from twoegram_relay.relay import RelayServer
from twoegram_relay.upstream import Upstream


class Gateway:
    """A gateway: an a2 server with echo agents."""

    def __init__(self, path, names, labels):
        self.store = DeviceStore(path / "devices.json")
        self.agents = EchoAgent(names, labels=labels)
        self.a2 = A2Server(self.store, self.agents)
        self.agents.server = self.a2
        self.listener = None

    async def start(self, port=0):
        self.listener = HttpListener(self.a2.handle, 1024)
        await self.listener.start("127.0.0.1", port)
        self.port = self.listener.port
        self.url = f"http://127.0.0.1:{self.port}"

    async def stop(self):
        await self.listener.stop()


class World:
    def __init__(self, tmp_path):
        self.tmp = tmp_path

    async def start(self, aliases=None, quiet_s=0.05):
        (self.tmp / "claw1").mkdir()
        (self.tmp / "mini").mkdir()
        self.claw1 = Gateway(self.tmp / "claw1", ("main", "coder"), {"main": "Pinchy"})
        self.mini = Gateway(self.tmp / "mini", ("hermes", "main"), {"hermes": "Hermes"})
        await self.claw1.start()
        await self.mini.start()
        # The relay is paired at each gateway like any Apple.
        self.logs = []
        ups = [Upstream("claw1", self.claw1.url, parse_key(self.claw1.store.pair("relay")[1]), log=self.logs.append),
               Upstream("mini", self.mini.url, parse_key(self.mini.store.pair("relay")[1]), log=self.logs.append)]
        self.devices = DeviceStore(self.tmp / "relay-devices.json")
        self.relay = RelayServer(self.devices, ups, aliases=aliases, log=self.logs.append,
                                 ident="twoegram-relay test", quiet_s=quiet_s, retry_s=0.05)
        await self.relay.refresh_all()
        self.listener = HttpListener(self.relay.handle, 1024)
        await self.listener.start("127.0.0.1", 0)
        self.apple = Client(self.listener.port, parse_key(self.devices.pair("Apple IIe")[1]))

    async def stop(self):
        self.apple.raw.close()
        await self.listener.stop()
        await self.relay.stop()
        for g in (self.claw1, self.mini):
            if g.listener:
                await g.stop()


@pytest.fixture
async def w(tmp_path):
    world = World(tmp_path)
    await world.start()
    yield world
    await world.stop()


async def answer(c, agent, after):
    """Poll until the echo's answer has come and the agent is idle."""
    msgs = []
    for _ in range(20):
        r = await c.call("GET", f"/a2/poll?a={agent}&after={after}&w=500")
        assert r.genuine and r.code == Code.OK
        msgs += r.msgs
        if msgs:
            after = msgs[-1].n
        if msgs and r.line("busy") == "0" and r.line("more") == "0":
            return msgs, r
    raise AssertionError("no answer")


async def test_one_list_from_two_gateways(w):
    r = await w.apple.hello()
    assert r.code == Code.OK
    # claw1's first, in order; mini's main clashes, so it takes the gateway's name.
    assert w.apple.agents == ["main", "coder", "hermes", "main-mini"]
    assert ("name", "main Pinchy") in r.lines and ("name", "hermes Hermes") in r.lines
    assert ("name", "main-mini main") in r.lines
    assert r.line("server") == "twoegram-relay test"


async def test_aliases(tmp_path):
    world = World(tmp_path)
    await world.start(aliases={"claw1": {"main": "pinchy"}})
    try:
        await world.apple.hello()
        assert world.apple.agents == ["pinchy", "coder", "hermes", "main"]
    finally:
        await world.stop()


async def test_send_and_answer_through_each_gateway(w):
    await w.apple.hello()
    for agent, gw, their in (("main", "claw1", "main"), ("hermes", "mini", "hermes"), ("main-mini", "mini", "main")):
        r = await w.apple.call("POST", f"/a2/send?a={agent}", f"hello {agent}")
        assert r.genuine and r.code == Code.OK and r.msgs[0].mine and r.msgs[0].text == f"hello {agent}"
        n = r.msgs[0].n
        msgs, _ = await answer(w.apple, agent, n)
        # The gateway's own agent answered, its id and text as it folded them.
        assert f"{their} heard you" in msgs[0].text and "hello " + agent in msgs[0].text
        assert "“" not in msgs[0].text            # folded upstream, not twice
    # Each gateway has the conversation under the relay's device, under its own ids.
    relay_on_mini = w.mini.store.list()[0].id
    assert [m.text for m in w.mini.a2._conv(relay_on_mini, "main").msgs][0] == "hello main-mini"


async def test_history_and_numbers_pass_through(w):
    await w.apple.hello()
    r = await w.apple.call("POST", "/a2/send?a=coder", "one long one")
    msgs, _ = await answer(w.apple, "coder", r.msgs[0].n)
    assert len(msgs) > 3                                    # several parts
    h = await w.apple.call("GET", "/a2/hist?a=coder&k=3")
    assert h.genuine and h.code == Code.OK and [m.n for m in h.msgs] == [m.n for m in msgs[-3:]]
    h = await w.apple.call("GET", "/a2/hist?a=coder&after=0&k=2")
    assert [m.n for m in h.msgs] == [1, 2] and h.line("more") == "1"


async def test_unread_from_the_other_gateway(w):
    await w.apple.hello()
    # The Apple talks to Pinchy; Hermes says something by itself meanwhile.
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0")
    relay_on_mini = w.mini.store.list()[0].id
    await asyncio.sleep(0.1)
    await w.relay.quiet_round()                             # learns where mini's agents are
    w.mini.a2.post(relay_on_mini, "hermes", "A reminder from Hermes.")
    w.relay._polled.clear()
    await w.relay.quiet_round()
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0")
    assert r.genuine and ("unread", "hermes 1") in r.lines
    # Opening Hermes: the gateway's own counts from then on, and the message.
    r = await w.apple.call("GET", "/a2/poll?a=hermes&after=0&w=0")
    assert any(m.text == "A reminder from Hermes." for m in r.msgs)
    assert not any(k == "unread" and v.startswith("hermes") for k, v in r.lines)


async def test_an_answer_read_is_not_unread_after_switching(w):
    # The answer comes in the poll that says busy 0. The Apple switches
    # within its 5 seconds to the next idle poll, so never polls again with
    # an `after` past the answer: neither the gateway nor the relay may count
    # it as unread then, through the gateway or the relay's quiet polls.
    await w.apple.hello()
    cur, other = "main", "hermes"
    for _ in range(4):
        r = await w.apple.call("POST", f"/a2/send?a={cur}", f"hi {cur}")
        await answer(w.apple, cur, r.msgs[0].n)
        h = await w.apple.call("GET", f"/a2/hist?a={other}&k=8")
        last = h.msgs[-1].n if h.msgs else 0
        await w.apple.call("GET", f"/a2/poll?a={other}&after={last}&w=0")
        await asyncio.sleep(0.1)
        w.relay._polled.clear()
        await w.relay.quiet_round()
        r = await w.apple.call("GET", f"/a2/poll?a={other}&after={last}&w=0")
        assert not any(k == "unread" for k, _ in r.lines), (cur, r.lines)
        cur, other = other, cur


async def test_a_quiet_polls_news_kept_on_its_own_gateway(w):
    # A quiet poll takes claw1's main's new message, so claw1 counts it as
    # seen. Opening coder, on claw1 too, still shows main's diamond.
    await w.apple.hello()
    relay_on_claw1 = w.claw1.store.list()[0].id
    await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0")
    await w.apple.call("GET", "/a2/poll?a=hermes&after=0&w=0")
    w.claw1.a2.post(relay_on_claw1, "main", "News from Pinchy.")
    w.relay._polled.clear()
    await w.relay.quiet_round()
    r = await w.apple.call("GET", "/a2/poll?a=hermes&after=0&w=0")
    assert ("unread", "main 1") in r.lines
    r = await w.apple.call("GET", "/a2/poll?a=coder&after=0&w=0")
    assert ("unread", "main 1") in r.lines
    # Opening main: fetched, and no longer counted.
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0")
    assert any(m.text == "News from Pinchy." for m in r.msgs)
    r = await w.apple.call("GET", "/a2/poll?a=coder&after=0&w=0")
    assert not any(k == "unread" for k, _ in r.lines)


async def test_unread_ids_on_the_same_gateway_are_the_relays(w):
    await w.apple.hello()
    relay_on_mini = w.mini.store.list()[0].id
    await w.apple.call("GET", "/a2/poll?a=hermes&after=0&w=0")
    w.mini.a2.post(relay_on_mini, "main", "From mini's main.")
    r = await w.apple.call("GET", "/a2/poll?a=hermes&after=0&w=0")
    assert ("unread", "main-mini 1") in r.lines


async def test_a_gateway_down_and_back(w):
    await w.apple.hello()
    port = w.mini.port
    await w.mini.stop()
    w.mini.listener = None
    r = await w.apple.call("GET", "/a2/poll?a=hermes&after=0&w=0")
    assert r.genuine and r.code == Code.BAD_REQUEST and r.line("why") == "mini is not answering"
    # claw1 is unaffected.
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0")
    assert r.genuine and r.code == Code.OK
    # The agents stay listed while it is down.
    await w.relay.refresh_all()
    c2 = Client(w.listener.port, w.apple.key)
    await c2.hello()
    assert "hermes" in c2.agents
    c2.raw.close()
    await w.mini.start(port)
    await w.relay.refresh_all()
    r = await w.apple.call("GET", "/a2/poll?a=hermes&after=0&w=0")
    assert r.genuine and r.code == Code.OK


async def test_the_relay_revoked_at_a_gateway(w):
    await w.apple.hello()
    w.mini.store.revoke(w.mini.store.list()[0].id)
    r = await w.apple.call("GET", "/a2/poll?a=hermes&after=0&w=0")
    assert r.genuine and r.code == Code.BAD_REQUEST and r.line("why") == "mini refused the relay's key"
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0")
    assert r.code == Code.OK
    assert any("mini refused the relay's key" in l for l in w.logs)


async def test_a_gateway_restarted_forgets_the_session(w):
    await w.apple.hello()
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0")
    w.claw1.a2._sessions.clear()                            # as a restart would
    r = await w.apple.call("POST", "/a2/send?a=main", "still there?")
    assert r.genuine and r.code == Code.OK


async def test_an_agent_gone_from_a_gateway(w):
    await w.apple.hello()
    w.claw1.agents.names.remove("coder")
    r = await w.apple.call("GET", "/a2/poll?a=coder&after=0&w=0")
    assert r.genuine and r.code == Code.UNKNOWN_AGENT
    await w.apple.hello()
    assert "coder" not in w.apple.agents


async def test_the_apples_own_checks_still_apply(w):
    stranger = Client(w.listener.port, bytes(16))
    r = await stranger.hello()
    assert r.code == Code.UNKNOWN_DEVICE
    stranger.raw.close()
    await w.apple.hello()
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0", tamper=True)
    assert r.code == Code.BAD_SIGNATURE
    r = await w.apple.call("GET", "/a2/poll?a=nobody&after=0&w=0")
    assert r.genuine and r.code == Code.UNKNOWN_AGENT


async def test_long_polls_wait_upstream_and_wake(w):
    await w.apple.hello()
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0")
    relay_on_claw1 = w.claw1.store.list()[0].id

    async def later():
        await asyncio.sleep(0.2)
        w.claw1.a2.post(relay_on_claw1, "main", "Woken.")
    t = asyncio.ensure_future(later())
    loop = asyncio.get_running_loop()
    t0 = loop.time()
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=4000")
    assert r.genuine and [m.text for m in r.msgs] == ["Woken."] and loop.time() - t0 < 2
    await t


async def test_payload_trimmed_to_fit(w):
    await w.apple.hello()
    relay_on_claw1 = w.claw1.store.list()[0].id
    for i in range(3):
        w.claw1.a2.post(relay_on_claw1, "main", f"{i} " + "x" * 600)
    for rid in ("hermes", "main-mini", "coder"):
        w.relay._unread[rid] = 99
    up = await w.relay.ups[0].call("call", "GET", "/a2/poll?a=main&after=0&w=0")
    assert len(up.msgs) == 3 and up.line("more") == "0"       # all three fit, as the gateway sends them
    r = await w.apple.call("GET", "/a2/poll?a=main&after=0&w=0")
    assert r.genuine and len(r.payload) <= MAX_PAYLOAD
    assert r.line("more") == "1" and len(r.msgs) == 2         # not with the relay's unread lines too
    r = await w.apple.call("GET", "/a2/poll?a=main&after=2&w=0")
    assert [m.n for m in r.msgs] == [3]


# ---- the command line ----

def test_cli_apples(tmp_path, monkeypatch):
    monkeypatch.setenv("TWOEGRAM_RELAY_CONFIG", str(tmp_path / "etc"))
    monkeypatch.setenv("TWOEGRAM_RELAY_STATE", str(tmp_path / "var"))
    out = []
    assert cli.main(["pair", "Apple IIe"], out=out.append) == 0
    written = out[-1].strip()
    key = parse_key(written)
    out = []
    assert cli.main(["add", "again"], out=out.append, read_key=lambda p: written) == 1
    assert "paired already" in out[0]
    out = []
    assert cli.main(["list"], out=out.append) == 0 and out[0].endswith("Apple IIe")
    assert (tmp_path / "var" / "devices.json").stat().st_mode & 0o777 == 0o600
    dev = out[0].split()[0]
    assert cli.main(["revoke", dev], out=out.append) == 0
    assert key


async def test_cli_upstreams(tmp_path, monkeypatch):
    monkeypatch.setenv("TWOEGRAM_RELAY_CONFIG", str(tmp_path / "etc"))
    monkeypatch.setenv("TWOEGRAM_RELAY_STATE", str(tmp_path / "var"))
    (tmp_path / "gw").mkdir()
    gw = Gateway(tmp_path / "gw", ("main",), {"main": "Pinchy"})
    await gw.start()
    written = gw.store.pair("relay")[1]
    out = []
    # The CLI runs its own event loop: off this one.
    rc = await asyncio.to_thread(cli.main, ["upstream", "add", "claw1", gw.url], out.append, lambda p: written)
    assert rc == 0 and "main (Pinchy)" in "\n".join(out)
    assert written not in "\n".join(out)
    assert (tmp_path / "etc" / "upstreams.json").stat().st_mode & 0o777 == 0o600
    out = []
    rc = await asyncio.to_thread(cli.main, ["upstream", "add", "claw2", gw.url], out.append, lambda p: "0" * 27)
    assert rc == 1
    out = []
    rc = await asyncio.to_thread(cli.main, ["upstream", "list"], out.append)
    assert rc == 0 and "answering: main" in out[0]
    await gw.stop()
    out = []
    rc = await asyncio.to_thread(cli.main, ["upstream", "list"], out.append)
    assert "connection refused" in out[0]
    rc = await asyncio.to_thread(cli.main, ["upstream", "remove", "claw1"], out.append)
    assert rc == 0
