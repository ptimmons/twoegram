# Twoegram on Hermes Agent: an investigation

2026-11-17. **Since built**, by Route A: [hermes/](../hermes/README.md), and PLAN.md's "Hermes Agent". What follows is the investigation as it stood. This note covers what Hermes Agent
(Nous Research, `NousResearch/hermes-agent`) offers for a new chat
platform, two ways to bring Twoegram to it, and a recommendation.
Sources are listed at the end. Details were read from the docs and from
`main` on GitHub; they were not tried on a running Hermes.

## The short answer

The Apple side needs almost nothing. It speaks [PROTOCOL.md](PROTOCOL.md)
to whatever answers at the gateway address, and knows nothing of
OpenClaw beyond a few strings. The work is a second gateway-side
implementation of the a2 protocol, as a Hermes platform plugin written
in Python. It is about the size of today's plugin without its OpenClaw
glue: roughly 1,000 lines, plus tests.

## What Hermes offers

- **Platform plugins.** A directory `~/.hermes/plugins/<name>/` with
  `plugin.yaml` (`kind: platform`, `requires_env`, `optional_env`) and
  `adapter.py`. The adapter's module-level `register(ctx)` calls
  `ctx.register_platform(name=..., adapter_factory=..., allowed_users_env=...,
  allow_all_env=..., max_message_length=..., platform_hint=..., ...)`.
  It is installed with `hermes plugins install <git-url> --enable`, then
  set up with `hermes gateway setup` and `hermes gateway restart`. One
  `hermes gateway` process runs every platform.
- **The adapter.** It subclasses `gateway.platforms.base.BasePlatformAdapter`
  and is all asyncio. It must provide three methods:
  - `connect(*, is_reconnect=False) -> bool`
  - `disconnect()`
  - `send(chat_id, content, reply_to=None, metadata=None) -> SendResult`

  Inbound, it calls `self.build_source(chat_id=, chat_type="dm", user_id=,
  user_name=)`, builds `MessageEvent(text=, message_type=MessageType.TEXT,
  source=, message_id=)`, and calls `await self.handle_message(event)`.
  An adapter may run its own aiohttp server: the webhook adapters
  (LINE, WeCom, BlueBubbles) do this.
- **Replies.** For an adapter without editing or draft streaming (the
  base defaults), the runner calls `send()` once per finished reply.
  Hermes's own splitter (`chunk_markdown_message`, which adds `(i/N)`
  marks) applies only when `max_message_length` is set. Typing is a
  heartbeat (`_keep_typing`) while the model works.
- **Sessions.** `build_session_key`: a DM is one session per `chat_id`,
  namespaced by profile (`agent:<profile>:<platform>:dm:<chat_id>`).
- **Who may talk.** Each platform has `<NAME>_ALLOWED_USERS` and
  `<NAME>_ALLOW_ALL_USERS`. Unknown DM senders may instead get a pairing
  request, approved on the host with `hermes pairing approve <platform>
  <request-id>`.
- **Command approvals.** A risky command sends a card: "Hermes wants to
  run a command that needs your OK", the command in a fenced block, the
  reason, and a deadline (300 s by default). Platforms without buttons
  get a plain-text prompt answered by typing `/approve` (or `/deny`);
  these run even while the session is busy. Twoegram can do that as it
  is.
- **One agent per profile.** Hermes has profiles (`hermes -p <name>`),
  each with its own config, `.env`, memory and plugin copy, rather than
  several named agents in one gateway. There is a multiplexed mode
  (`gateway.multiplex_profiles`, `/p/<profile>/` routes), but it is new
  and lightly documented.
- **Also: an OpenAI-compatible API server.** It is turned on with
  `API_SERVER_ENABLED=true`, listens on `127.0.0.1:8642`, and answers
  `POST /v1/chat/completions` with a bearer key.
- **Python 3.11 to 3.14.** aiohttp comes with the `messaging` extra.

## Route A: a native Hermes plugin (recommended)

A Python package, `hermes-twoegram`, with `plugin.yaml` and `adapter.py`,
registering platform `a2`.

| Today's plugin (TypeScript) | In Hermes (Python) |
|---|---|
| `protocol.ts`: framing, HMAC, hello proof, codes | Ported line for line; the Vitest vectors become pytest vectors, so both sides are checked against the same bytes |
| `server.ts`: `/a2/hello`, `send`, `poll`, `hist`, lockouts, replay | An aiohttp app started in `connect()`, on the same port 8417 |
| `devices.ts`: paired keys | The same, in `~/.hermes/platforms/a2/`, mode 0600 |
| `convstore.ts`: each conversation's log for `hist` | The same; Hermes keeps the model's session, and we keep what the Apple scrolls through |
| `text.ts`: folding to ASCII, check mark, parts | Ported; `max_message_length=0` so Hermes does not split, and we cut parts as now |
| `channel.ts`, `openclaw-agent.ts`: OpenClaw glue | `send()` appends the reply to the conversation and wakes the poll; `handle_message()` takes a `/a2/send` |
| `cli.ts`: `openclaw a2 pair`, `list`, `revoke` | `hermes a2 pair`, `list`, `revoke`, if plugins can add subcommands as rply's `hermes rply join` suggests; else a small `hermes-a2` script |

The mapping:

- **Who.** Each paired device is a `user_id` (its 8-hex device id), and
  `chat_id` is `<device>.<agent>`. Our pairing stays the gate: only a
  signed hello opens a session. So the plugin lists paired devices as
  its allowed users and leaves Hermes's own DM pairing out, since a
  device that cannot sign never reaches `handle_message`.
- **Agents.** Hello lists one agent, the profile: `main`, named from
  the profile's identity if Hermes has one. Several profiles can each
  run the plugin on their own port, as several Apples' gateways; listing
  every profile in one hello waits until `multiplex_profiles` settles.
- **Busy.** `busy 1` from `handle_message` until `send()`, or while
  `_keep_typing` runs.
- **Messages the agent starts** (cron, reminders) arrive through
  `send()` like any other reply, and `cron_deliver_env_var` can name a
  device.
- **Approvals** arrive as text. The fenced command folds as code does
  now, and the user types `/approve`. Nothing new is needed on the Apple
  unless the 300 s deadline proves too short at Apple typing speeds.

Strengths:
- It is a real channel: Hermes's sessions, memory, approvals, `/stop`
  and scheduled messages all work.
- There is nothing extra to run beside `hermes gateway`.

Costs:
- A second implementation of the server to keep in step with the first.
  Shared test vectors and PROTOCOL.md are what keep the two honest.
- Python 3.11 or later on the Hermes machine.

Rough size:
- The plugin: about 1,000 lines of Python and 600 of tests.
- An install guide.
- A manual appendix.
- A scratch Hermes on the Mac to test against, as milestone 6 did with
  OpenClaw.

## Route B: today's server, with Hermes behind it

Run the existing TypeScript server on its own (`dev-server.ts` already
does), with a new `AgentPort` (src/agent.ts) that forwards each message
to Hermes's API server and streams the answer back.

Strengths:
- Every tested line of server code is reused.
- The new code is perhaps 150 lines.
- A working Apple could be talking to Hermes in a day.

Weaknesses:
- `/v1/chat/completions` is stateless. Either we send the history each
  time, or we rely on a session header whose behaviour I have not
  confirmed.
- Hermes would not see the Apple as a platform: no approvals (a risky
  command would most likely just be refused), no messages the agent
  starts, no `/stop`.
- It means a second service to keep running, and a bearer key to look
  after.

It is a good prototype for finding out whether Hermes's answers suit the
Apple, but a poor place to stop.

## What changes on the Apple

Only words, all of them OpenClaw by name:
- splash.c: "Talk to your OpenClaw agents"
- setup.c: two help lines, including `openclaw a2 pair`
- convo.c: b034's new "Pair this Apple again (openclaw a2 pair)"

Two ways to handle them:
- **Neutral words**, such as "your agents" and "pair this Apple on the
  gateway". This costs nothing.
- **The hello names the gateway** in a new `server` line (older clients
  ignore unknown lines, as with `name`), and the Apple says "Hermes" or
  "OpenClaw" from that. This costs a few dozen bytes and a protocol
  addition.

Neutral words are simpler. The manual and INSTALL.md would gain a
Hermes chapter.

## Your Hermes: the Mac mini

Checked read-only over SSH on 2026-11-17. Nothing was changed.

- **The machine.** `mini.local`, 192.168.1.30, Apple M4, user
  `you`.
- **The install.** Hermes Agent v0.23.0 (2026.9.24), installed from git
  at `~/.hermes/hermes-agent` (commit `2bd378ed1b`, 2026-11-05). Its own
  Python is 3.14.9, under `~/.hermes/tools/`.
- **What's running.** The gateway is up as `hermes_cli.main --profile
  default gateway run`, with one profile (`default`), no user plugins
  yet, and the model provider `openai-codex`.
- **Ports.** Nothing listens on 8400 to 8499, or on 8642, so the plugin
  can use 8417 there, as on claw1.

That source settles the open questions:

- **CLI commands.** A plugin can add `hermes <name> ...` subcommands:
  `ctx.register_cli_command(name, help, setup_fn, handler_fn)` in
  `hermes_cli/plugins.py`. So `hermes a2 pair`, `list` and `revoke`
  work as they do in OpenClaw.
- **Tool progress.** `display.tool_progress` defaults to `all`, but a
  plugin platform can declare `display_tier="low"` (in
  `gateway/display_config.py`). That turns off tool progress, streaming,
  interim messages and long-running notices: right for a screen that
  cannot edit what it has drawn. With that set, the Apple gets only
  replies.
- **Approvals as text.** The prompt is (from `locales/en.yaml` and
  `gateway/run.py`):

  ~~~
  ⚠️ **Hermes wants to run a command that needs your OK**
  ```
  <the command>
  ```
  Why it was flagged: <reason>

  Reply `/approve` to run it once, `/approve session` to allow this
  pattern for the rest of this session, `/approve always` to allow it
  permanently, or `/deny` to cancel.
  If you don't answer within <window> it will NOT run.
  ~~~

  Pending approvals are answered only by `/approve` and `/deny`, never
  by bare text. Our folding turns the emoji into nothing and the fence
  into a code block; the user types `/approve`.

So Route A has no unknowns left that need a scratch Hermes. The plugin
would be developed on this Mac against Hermes's own source (a clone of
the same commit, for its test helpers), then installed on the Mac mini
with `hermes plugins install`.

## Sources

- [Adding platform adapters](https://hermes-agent.nousresearch.com/docs/developer-guide/adding-platform-adapters)
- [ADDING_A_PLATFORM.md](https://raw.githubusercontent.com/NousResearch/hermes-agent/main/gateway/platforms/ADDING_A_PLATFORM.md)
- [base.py](https://raw.githubusercontent.com/NousResearch/hermes-agent/main/gateway/platforms/base.py)
- [event.py](https://raw.githubusercontent.com/NousResearch/hermes-agent/main/gateway/platforms/event.py)
- [session.py](https://raw.githubusercontent.com/NousResearch/hermes-agent/main/gateway/session.py)
- [pairing.py](https://raw.githubusercontent.com/NousResearch/hermes-agent/main/gateway/pairing.py)
- [base_exec_approval.py](https://raw.githubusercontent.com/NousResearch/hermes-agent/main/gateway/platforms/base_exec_approval.py)
- [IRC adapter](https://raw.githubusercontent.com/NousResearch/hermes-agent/main/plugins/platforms/irc/adapter.py)
- [pyproject.toml](https://raw.githubusercontent.com/NousResearch/hermes-agent/main/pyproject.toml)
- [rply plugin](https://hermes-agent.nousresearch.com/docs/plugins/rply-platform)
- [chatto plugin](https://hermes-agent.nousresearch.com/docs/plugins/chatto-platform)
- [API server](https://hermes-agent.nousresearch.com/docs/user-guide/features/api-server)
- [Open WebUI with Hermes](https://docs.openwebui.com/getting-started/quick-start/connect-an-agent/hermes-agent)
