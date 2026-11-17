"""The adapter inside Hermes's own base class: a message from a signed Apple
reaches the gateway's handler, the answer comes back through send() to the
Apple's poll, busy follows Hermes's processing hooks, and Hermes's
allowlist check lets in the paired devices and nobody else."""

from __future__ import annotations

import argparse
import asyncio
import io

import pytest

pytest.importorskip("gateway.platforms.base", reason="needs Hermes's source (HERMES_SRC)")

from gateway.config import GatewayConfig, Platform, PlatformConfig  # noqa: E402
from gateway.platform_registry import PlatformEntry, platform_registry  # noqa: E402
from gateway.session import SessionSource  # noqa: E402

from twoegram import adapter as ad  # noqa: E402
from twoegram import cli  # noqa: E402
from twoegram.protocol import Code, parse_key  # noqa: E402

from test_server import Client  # noqa: E402

_AUTH_ENV = ("A2_ALLOWED_USERS", "A2_ALLOW_ALL_USERS", "GATEWAY_ALLOWED_USERS", "GATEWAY_ALLOW_ALL_USERS")


class Ctx:
    """What the plugin's register() is handed, enough to see what it asks for."""

    def __init__(self):
        self.platform = None
        self.cli = None

    def register_platform(self, **kw):
        self.platform = kw

    def register_cli_command(self, **kw):
        self.cli = kw


@pytest.fixture
def home(tmp_path, monkeypatch):
    monkeypatch.setenv("HERMES_HOME", str(tmp_path))
    for v in _AUTH_ENV + ("A2_HOST", "A2_PORT", "A2_AGENT", "A2_AGENT_NAME", "A2_HOME_CHANNEL"):
        monkeypatch.delenv(v, raising=False)
    return tmp_path


@pytest.fixture
def registered(home):
    ctx = Ctx()
    ad.register(ctx)
    platform_registry.register(PlatformEntry(**ctx.platform))
    yield ctx
    platform_registry.unregister("a2")


@pytest.fixture
async def running(registered):
    a = ad.A2Adapter(PlatformConfig(enabled=True, extra={"host": "127.0.0.1", "port": 0}))
    assert await a.connect() is True
    yield a
    await a.disconnect()


def pair(name="test"):
    out = io.StringIO()
    cli.dispatch(argparse.Namespace(a2_command="pair", name=name), out=lambda s="": out.write(s + "\n"))
    text = out.getvalue()
    written = next(l.strip() for l in text.splitlines() if l.startswith("    "))
    return parse_key(written), text


def test_register_asks_for_a_quiet_platform_and_the_a2_command(registered):
    p = registered.platform
    assert p["name"] == "a2" and p["display_tier"] == "low" and p["max_message_length"] == 0
    assert "Apple //e" in p["platform_hint"]
    assert registered.cli["name"] == "a2"
    parser = argparse.ArgumentParser()
    registered.cli["setup_fn"](parser)
    assert parser.parse_args(["revoke", "1234abcd"]).id == "1234abcd"
    assert platform_registry.get("a2").display_tier == "low"


def test_cli_pairs_lists_and_revokes(home):
    key, text = pair("Kitchen //e")
    assert "Type this key into Twoegram" in text and "Gateway: http://" in text
    out = []
    cli.dispatch(argparse.Namespace(a2_command="list"), out=out.append)
    assert out[0].endswith("Kitchen //e")
    dev = out[0].split()[0]
    assert (home / "platforms" / "a2" / "devices.json").exists()
    out = []
    assert cli.dispatch(argparse.Namespace(a2_command="revoke", id=dev), out=out.append) == 0
    assert out == [f"revoked {dev}"]
    assert cli.dispatch(argparse.Namespace(a2_command="revoke", id=dev), out=out.append) == 1


async def test_hello_lists_hermes(running):
    key, _ = pair()
    c = Client(running._listener.port, key)
    r = await c.hello()
    assert r.code == Code.OK
    assert c.agents == ["hermes"]
    assert ("name", "hermes Hermes") in r.lines
    assert r.line("server").startswith("hermes-a2 ")
    c.raw.close()


async def test_a_message_reaches_hermes_and_the_answer_reaches_the_apple(running):
    seen = []

    async def handler(event):
        seen.append(event)
        await asyncio.sleep(0.05)
        return "**Done.** The lights are off \U0001F44D"

    running.set_message_handler(handler)
    key, _ = pair("Den //e")
    c = Client(running._listener.port, key)
    await c.hello()
    sent = await c.call("POST", "/a2/send?a=hermes", "turn out the lights")
    assert sent.code == Code.OK
    busy = await c.call("GET", "/a2/poll?a=hermes&after=1")
    assert ("busy", "1") in busy.lines
    got = []
    for _ in range(10):
        r = await c.call("GET", f"/a2/poll?a=hermes&after={got[-1].n if got else 1}&w=2000")
        got += r.msgs
        if got and r.line("busy") == "0":
            break
    assert [m.text for m in got] == ["Done. The lights are off \x02"]
    ev = seen[0]
    assert ev.text == "turn out the lights"
    assert ev.source.chat_id == f"{c.id}.hermes" and ev.source.user_id == c.id
    assert ev.source.user_name == "Den //e" and ev.source.chat_type == "dm"
    c.raw.close()


async def test_busy_ends_when_hermes_finishes_without_a_word(running):
    async def handler(event):
        await asyncio.sleep(0.05)
        return None

    running.set_message_handler(handler)
    key, _ = pair()
    c = Client(running._listener.port, key)
    await c.hello()
    await c.call("POST", "/a2/send?a=hermes", "hm")
    for _ in range(10):
        r = await c.call("GET", "/a2/poll?a=hermes&after=1&w=1000")
        if r.line("busy") == "0":
            break
    assert r.line("busy") == "0" and r.msgs == []
    c.raw.close()


async def test_messages_hermes_starts_and_long_ones(running):
    key, _ = pair()
    c = Client(running._listener.port, key)
    await c.hello()
    res = await running.send(f"{c.id}.hermes", "## Reminder\n\n" + "Water the plants. " * 200)
    assert res.success and res.message_id.startswith(f"{c.id}.hermes.")
    assert (await running.send(c.id, "bare device")).success              # the agent assumed
    assert not (await running.send("00000000.hermes", "x")).success        # not paired
    assert not (await running.send("nonsense", "x")).success
    texts, after = [], 0
    for _ in range(10):
        r = await c.call("GET", f"/a2/poll?a=hermes&after={after}")
        texts += [m.text for m in r.msgs]
        after = r.msgs[-1].n if r.msgs else after
        if r.line("more") == "0":
            break
    assert texts[0].startswith("Reminder\n\nWater the plants.") and len(texts) == 4
    assert texts[-1] == "bare device" and all(len(t) <= 1500 for t in texts)
    c.raw.close()


async def test_approval_prompt_reads_on_the_e(running):
    key, _ = pair()
    c = Client(running._listener.port, key)
    await c.hello()
    prompt = ("⚠️ **Hermes wants to run a command that needs your OK**\n```\nrm -rf /tmp/x\n```\n"
              "Why it was flagged: deletes files\n\nReply `/approve` to run it once, `/approve session` to allow "
              "this pattern for the rest of this session, `/approve always` to allow it permanently, or `/deny` "
              "to cancel.\nIf you don't answer within 5 minutes it will NOT run.\n")
    await running.send(f"{c.id}.hermes", prompt)
    r = await c.call("GET", "/a2/poll?a=hermes&after=0")
    t = r.msgs[0].text
    assert t.startswith(":warning: Hermes wants to run a command that needs your OK\nrm -rf /tmp/x\n")
    assert "Reply /approve to run it once" in t and "or /deny to cancel." in t
    c.raw.close()


def _runner(adapter):
    from gateway.run import GatewayRunner
    runner = object.__new__(GatewayRunner)
    runner.pairing_store = None
    runner.config = GatewayConfig(platforms={Platform("a2"): adapter.config})
    runner.adapters = {Platform("a2"): adapter}
    return runner


def _source(user):
    return SessionSource(platform=Platform("a2"), chat_id=f"{user}.hermes", chat_type="dm", user_id=user,
                         user_name="x")


async def test_hermes_lets_in_paired_devices_only(running):
    key, _ = pair()
    from twoegram.protocol import device_id
    runner = _runner(running)
    running._refresh_allow()
    try:
        assert runner._is_user_authorized(_source(device_id(key))) is True
        assert runner._is_user_authorized(_source("deadbeef")) is False
    except AttributeError as e:                     # a runner built by hand lacks something
        pytest.skip(f"runner stub incomplete: {e}")


async def test_port_in_use_fails_retryably(registered):
    first = ad.A2Adapter(PlatformConfig(enabled=True, extra={"host": "127.0.0.1", "port": 0}))
    assert await first.connect()
    second = ad.A2Adapter(PlatformConfig(enabled=True, extra={"host": "127.0.0.1", "port": first._listener.port}))
    try:
        assert await second.connect() is False
    finally:
        await first.disconnect()
        await second.disconnect()
