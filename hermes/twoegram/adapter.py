"""The a2 platform as Hermes sees it: the Apple //e, running Twoegram, over the LAN.

The protocol is in server.py. This file is the part that imports Hermes, and
is only loaded inside it.

Settings, in ~/.hermes/.env (or config.yaml's gateway.platforms.a2.extra):
  A2_HOST         address to listen on (default 0.0.0.0: every interface)
  A2_PORT         default 8417
  A2_AGENT        the agent's id on the Apple (default hermes)
  A2_AGENT_NAME   what the Apple calls it (default Hermes)
  A2_HOME_CHANNEL <device>.<agent> for scheduled messages (hermes a2 list shows devices)

Who may talk: the paired devices, and nobody else. A request only reaches
Hermes once its signature checks, and the device list is handed to Hermes's
own allowlist check as the adapter's `allow_from`.
"""

from __future__ import annotations

import asyncio
import datetime
import logging
import re
import uuid
from pathlib import Path
from typing import Any, Optional

from gateway.config import Platform
from gateway.platforms._shared import get_scoped_secret as _secret
from gateway.platforms.base import BasePlatformAdapter, SendResult
from gateway.platforms.event import MessageEvent, MessageType, ProcessingOutcome

from . import VERSION
from .protocol import AGENT_NAME, MAX_BODY
from .server import A2Server, HttpListener
from .store import ConvStore, DeviceStore

logger = logging.getLogger(__name__)

PLATFORM = "a2"
DEFAULT_PORT = 8417
DEFAULT_AGENT = "hermes"
DEFAULT_NAME = "Hermes"
# How long a message counts as being worked on before Hermes says it has
# started (a command answered at once never does).
PENDING_S = 30.0

_CHAT = re.compile(r"(?:a2:)?([0-9a-f]{8})(?:\.([a-z0-9_-]{1,16}))?\Z")

PLATFORM_HINT = (
    "You are chatting with someone on an Apple //e (Twoegram): an 80-column text screen that shows "
    "plain ASCII only. Markdown is shown as plain text and code blocks are kept line for line; prefer "
    "short paragraphs and short lines. There are no images, files, links to click, buttons or reactions: "
    "everything must be readable as text. Non-ASCII characters and emoji are replaced, so prefer plain ASCII. "
    "To answer a command approval the user types /approve or /deny."
)


def state_dir() -> Path:
    """Where the devices and conversation logs live, for this profile."""
    from hermes_constants import get_hermes_home
    return get_hermes_home() / "platforms" / PLATFORM


def devices_path() -> Path:
    return state_dir() / "devices.json"


def conversations_dir() -> Path:
    return state_dir() / "conversations"


def _setting(extra: dict, env: str, key: str, default: Any) -> Any:
    v = _secret(env, "")
    return v if v not in (None, "") else extra.get(key, default)


def parse_chat(chat_id: str, agent: str) -> Optional[tuple[str, str]]:
    """`<device>.<agent>` (or a bare device, meaning this agent) to its parts."""
    m = _CHAT.match((chat_id or "").strip().lower())
    if not m:
        return None
    return m.group(1), m.group(2) or agent


class A2Adapter(BasePlatformAdapter):
    """Listens for the Apple, verifies it, and hands its messages to Hermes;
    Hermes's answers come back through send() for the Apple's next poll."""

    def __init__(self, config, **kwargs):
        super().__init__(config=config, platform=Platform(PLATFORM))
        extra = getattr(config, "extra", None)
        if not isinstance(extra, dict):
            extra = {}
            config.extra = extra
        self.host = str(_setting(extra, "A2_HOST", "host", "0.0.0.0"))
        try:
            self.port = int(_setting(extra, "A2_PORT", "port", DEFAULT_PORT))
        except (TypeError, ValueError):
            self.port = DEFAULT_PORT
        agent = str(_setting(extra, "A2_AGENT", "agent", DEFAULT_AGENT)).strip().lower()
        self.agent = agent if AGENT_NAME.match(agent) else DEFAULT_AGENT
        self.agent_name = str(_setting(extra, "A2_AGENT_NAME", "name", DEFAULT_NAME))
        self.devices = DeviceStore(devices_path())
        self.a2 = A2Server(self.devices, self, convs=ConvStore(conversations_dir()),
                           log=lambda line: logger.info("%s", line), ident=f"hermes-a2 {VERSION}")
        self._listener: Optional[HttpListener] = None
        self._pending: dict[str, asyncio.TimerHandle] = {}
        self._active: dict[str, int] = {}
        self._refresh_allow()

    @property
    def name(self) -> str:
        return "Apple //e"

    # ---- what the server asks of its agent ----

    def agents(self) -> list[str]:
        return [self.agent]

    def display_name(self, agent: str) -> Optional[str]:
        return self.agent_name

    async def deliver(self, device: str, agent: str, text: str) -> None:
        dev = self.devices.get(device)
        name = dev.name if dev else device
        chat = f"{device}.{agent}"
        self._refresh_allow()
        self._mark_pending(chat)
        source = self.build_source(chat_id=chat, chat_name=name, chat_type="dm", user_id=device, user_name=name)
        await self.handle_message(MessageEvent(
            text=text, message_type=MessageType.TEXT, source=source,
            message_id=uuid.uuid4().hex, timestamp=datetime.datetime.now()))

    def _refresh_allow(self) -> None:
        # Hermes's allowlist check reads the live adapter's config.extra
        # (authz_mixin._adapter_extra_allowlist_authorizes): the paired devices.
        try:
            self.config.extra["allow_from"] = [d.id for d in self.devices.list()]
        except Exception as e:                       # an unreadable file: nobody, until it is fixed
            logger.warning("a2: could not read %s: %s", self.devices.path, e)
            self.config.extra["allow_from"] = []

    # ---- busy, for the Apple's poll ----

    def _busy_changed(self, chat: str) -> None:
        parts = parse_chat(chat, self.agent)
        if not parts:
            return
        busy = self._active.get(chat, 0) > 0 or chat in self._pending
        try:
            self.a2.set_busy(parts[0], parts[1], busy)
        except Exception:
            pass

    def _mark_pending(self, chat: str) -> None:
        self._clear_pending(chat, quiet=True)
        loop = asyncio.get_running_loop()
        self._pending[chat] = loop.call_later(PENDING_S, self._clear_pending, chat)
        self._busy_changed(chat)

    def _clear_pending(self, chat: str, quiet: bool = False) -> None:
        h = self._pending.pop(chat, None)
        if h is not None:
            h.cancel()
        if not quiet:
            self._busy_changed(chat)

    async def on_processing_start(self, event: MessageEvent) -> None:
        chat = getattr(event.source, "chat_id", "")
        self._active[chat] = self._active.get(chat, 0) + 1
        self._clear_pending(chat)

    async def on_processing_complete(self, event: MessageEvent, outcome: ProcessingOutcome) -> None:
        chat = getattr(event.source, "chat_id", "")
        n = self._active.get(chat, 0) - 1
        if n > 0:
            self._active[chat] = n
        else:
            self._active.pop(chat, None)
        self._clear_pending(chat)

    # ---- the adapter ----

    async def connect(self, *, is_reconnect: bool = False) -> bool:
        if not self._acquire_platform_lock(PLATFORM, f"{self.host}:{self.port}",
                                           f"the a2 listener on port {self.port}"):
            return False
        listener = HttpListener(self.a2.handle, MAX_BODY)
        try:
            await listener.start(self.host, self.port)
        except OSError as e:
            logger.error("a2: could not listen on %s:%s: %s", self.host, self.port, e)
            self._release_platform_lock()
            self._set_fatal_error("listen_failed", f"could not listen on port {self.port}: {e}", retryable=True)
            return False
        self._listener = listener
        self._refresh_allow()
        self._mark_connected(listener_base=f"http://{self.host}:{self.port}")
        logger.info("a2: listening on http://%s:%s/a2/ for %d device(s); agent %s (%s)",
                    self.host, self.port, len(self.devices.list()), self.agent, self.agent_name)
        self._wire_plugin_handlers(None)
        return True

    async def disconnect(self) -> None:
        for h in self._pending.values():
            h.cancel()
        self._pending.clear()
        if self._listener is not None:
            await self._listener.stop()
            self._listener = None
        try:
            self._release_platform_lock()
        except Exception:
            pass
        self._mark_disconnected()

    async def send(self, chat_id: str, content: str, reply_to: Optional[str] = None,
                   metadata: Optional[dict[str, Any]] = None) -> SendResult:
        parts = parse_chat(chat_id, self.agent)
        if not parts:
            return SendResult(success=False, error=f"not a device and agent: {chat_id}")
        device, agent = parts
        try:
            n = self.a2.post(device, agent, content)
        except ValueError:
            return SendResult(success=True, message_id=None)      # nothing the Apple could show
        except LookupError as e:
            return SendResult(success=False, error=str(e))
        self._clear_pending(f"{device}.{agent}")
        return SendResult(success=True, message_id=f"{device}.{agent}.{n}")

    async def send_typing(self, chat_id: str, metadata=None) -> None:
        """The Apple sees `busy 1` instead."""

    async def get_chat_info(self, chat_id: str) -> dict[str, Any]:
        parts = parse_chat(chat_id, self.agent)
        dev = self.devices.get(parts[0]) if parts else None
        return {"name": dev.name if dev else chat_id, "type": "dm"}


# ---- registration ----

def _check() -> bool:
    return True                                      # standard library only


def _configured(config) -> bool:
    return True                                      # needs nothing but a port; pairing is separate


def _env_enablement() -> Optional[dict]:
    seed = {}
    for env, key in (("A2_HOST", "host"), ("A2_PORT", "port"), ("A2_AGENT", "agent"), ("A2_AGENT_NAME", "name")):
        v = _secret(env, "")
        if v:
            seed[key] = v
    home = _secret("A2_HOME_CHANNEL", "")
    if home:
        seed["home_channel"] = {"chat_id": home, "name": "Apple //e"}
    return seed or None


def _parse_target(ref: str) -> Optional[tuple[str, Optional[str]]]:
    m = _CHAT.match((ref or "").strip().lower())
    if not m:
        return None
    return f"{m.group(1)}.{m.group(2) or DEFAULT_AGENT}", None


def register(ctx) -> None:
    from . import cli
    ctx.register_platform(
        name=PLATFORM,
        label="Apple //e (Twoegram)",
        dashboard_description="Chat with Hermes from an Apple //e running Twoegram, over the LAN, signed.",
        adapter_factory=A2Adapter,
        check_fn=_check,
        validate_config=_configured,
        is_connected=_configured,
        required_env=[],
        install_hint="No extra packages needed (standard library only). Pair an Apple with `hermes a2 pair`.",
        env_enablement_fn=_env_enablement,
        cron_deliver_env_var="A2_HOME_CHANNEL",
        parse_target_ref_fn=_parse_target,
        max_message_length=0,                         # the plugin cuts parts itself
        display_tier="low",                           # the //e cannot edit what it has drawn
        emoji="\U0001F34E",
        pii_safe=False,
        allow_update_command=False,
        platform_hint=PLATFORM_HINT,
    )
    ctx.register_cli_command(
        name="a2", help="Apple //e devices for the a2 platform (Twoegram)",
        setup_fn=cli.setup, handler_fn=cli.dispatch)
