# Twoegram — implementation plan

A chat client for the Enhanced Apple //e with a CoGS card, and the
OpenClaw channel plugin it talks to. (The name is kept from an earlier
Telegram plan, now dropped.)

Newsiie is the design language (80-column screen, splash, inverse status
bar, MouseText help box) and the source of the screen, keyboard, prompt,
CoGS and aux-memory code.

**Status:** plan only. Nothing is built.

Sources read: `~/repos/newsiie` (PLAN.md, `cogs.h`, `ui.h`, `screen.h`);
the CoGS Technical Reference (firmware 1.39, rev 33) chapters 1, 3, 4, 5,
9.10, 10 and Appendix A; `~/repos/a2chat/a2chat/docs/COGS.md`; and the
OpenClaw documentation at docs.openclaw.ai on 2026-11-15 (channel
plugins, gateway routes, pairing, access control, network exposure).
OpenClaw is not installed on this Mac, so nothing here has been tried
against a gateway.

## 1. Target

| | |
|---|---|
| Machine | Enhanced Apple //e, 128K, 80 columns |
| OS | ProDOS 8, launched as `TWOEGRAM.SYSTEM` |
| Network | CoGS card, firmware 1.39 or later, joined |
| Boot | the CoGS network disk, or any local ProDOS volume |
| Far end | your OpenClaw gateway, on the same LAN, with the `a2` channel plugin installed |

Newsiie's network-disk rules carry over: no ProDOS call while a card
operation is in flight, nothing unsolicited from the card, never the soft
reset, the volume may be read-only.

Not in v1: **encryption** (section 2), use from outside the LAN, pictures
and files, voice, groups, several gateways, 40 columns.

## 2. What "as secure as practical" means here

**What is being protected.** An OpenClaw agent has tools. Whoever can
speak to it as you can run them. So the first goal is that nobody but
this Apple can put words in front of the agent or forge its replies; the
second is that nobody can read the conversation.

**What the card gives us** (from the reference):

| Have | Do not have |
|---|---|
| TLS 1.2 on the card (mbedTLS), verified against a bundled Mozilla root store; refused while the clock is unset | certificate pinning, or adding our own root |
| `TLS_INFO`: was the last session verified, its cipher, the certificate's name | client certificates |
| `HASH`: SHA-256 on the card, up to about 2,000 bytes a call | HMAC, AES or any cipher the Apple can call |
| `RANDOM`: a hardware random source | |
| 17 spare bytes of card flash (`PREFS` bytes 15–31) | a protected key store |

**Decided (2026-11-15): LAN only, in the clear for now, signed.**

**Every request and every reply is signed** with HMAC-SHA256 under a key
only this Apple and the plugin hold. HMAC is two `HASH` calls; the Apple
only pads and XORs the key. The plugin drops anything unsigned before it
reaches OpenClaw. So nobody else on the LAN can speak to the agent,
alter a message or replay one. The Apple never holds the gateway's own
token, which the OpenClaw docs describe as full operator access.

**The conversation is not encrypted.** It crosses the LAN as plain HTTP,
and any device that can capture LAN traffic can read it. The client says
so: the status row shows `SIGNED, NOT ENCRYPTED`.

**Encryption comes later**, by one of two routes, and the protocol
leaves room for either (a version number, and the message text as one
opaque field):

| Route | Needs |
|---|---|
| The card's TLS: the plugin fetches its own Let's Encrypt certificate at onboarding, by DNS challenge | a domain or dynamic-DNS name with a DNS API; trusts the card's whole root store, so signing stays |
| ChaCha20 on the Apple, under a key derived at the handshake | about 1K of 6502 code and a delay on each reply (not measured); no certificate and no outside authority |

### The handshake and the signatures

- **Device key:** 16 random bytes, made on the gateway host by
  `openclaw a2 pair`, shown once as 26 base-32 characters, and typed into
  the Apple. No request can create a device, so there is no pairing
  endpoint to attack. `openclaw a2 revoke` removes it.
- **Where the Apple keeps it:** in the card's flash (`PREFS` bytes
  15–31), not in a file. A network disk is an image on a web server and
  writes to it are HTTP PUTs, so a key must never be saved there.
- **Session start:** the Apple sends a random nonce; the plugin answers
  with its own nonce and an HMAC over both, which proves the far end
  holds the key. Both sides derive a session key from the two nonces.
- **Each request:** a rising sequence number and an HMAC over the number,
  method, path and body, sent in a header (nothing secret in a URL). The
  plugin rejects a bad signature, a repeated number or an expired
  session, compares in constant time, and locks a device out for a while
  after repeated failures.
- **Each reply:** signed the same way; the Apple checks before drawing.
- **Optional passphrase:** asked at startup, never stored, hashed into
  the key. It protects against someone sitting down at the //e.

### How the gateway is reached

The gateway stays bound to loopback, as OpenClaw recommends, so the
dashboard, `/v1/*` and the WebSocket are not on the LAN at all. Only the
`/a2` endpoints are, over plain HTTP, by whichever of these the SDK
allows (checked at milestone 6):

1. the plugin opens its own small listener on a LAN port (preferred: no
   extra software); or
2. a reverse proxy on the same host forwards only `/a2/*` to the gateway.

The listener must never be forwarded to the internet. Without TLS, the
signatures would then be the only protection and everything would be
readable in transit.

### What this does not protect against

- **Anyone on the LAN reading the conversation**, until encryption is
  added.
- Anyone who can run a program on the //e can read the key from the card.
- The card and its firmware see everything: the key and the plain text.
- A compromised gateway host. That is OpenClaw's own trust boundary.

## 3. The OpenClaw channel plugin (`a2`)

TypeScript, Node 24, built on the documented channel SDK:
`package.json` with an `openclaw` block, `openclaw.plugin.json` with
`"channels": ["a2"]`, `defineChannelPluginEntry` and
`createChatChannelPlugin`. Installed on the gateway host with
`openclaw plugins install -l <path>` and `openclaw plugins enable a2`.
The docs call the plugin API experimental, so the plugin pins the
OpenClaw version it was tested on.

| Piece | How |
|---|---|
| Routes | its own LAN listener, or `api.registerHttpRoute({ path: "/a2", match: "prefix", auth: "plugin", ... })` behind a proxy (section 2). With `auth: "plugin"` the plugin does its own checks and receives no operator scopes. |
| Inbound | a verified message goes to the agent through the SDK's direct-message path (`dispatchInboundDirectDm`) |
| Outbound | `sendText` puts the reply in that device's queue; the Apple collects it on its next poll. Block replies are queued as they come, so a long answer appears in parts. |
| Who may talk | the device list is the allowlist; `dmPolicy` is `allowlist`, never `open` |
| Sessions | each device is one sender; `session.dmScope: "per-channel-peer"` keeps its conversation out of the shared main session |
| Text | Markdown to plain text, Unicode folded to ASCII, emoji to names, code blocks kept line for line, replies cut into parts of at most 1,500 bytes |
| CLI | `openclaw a2 pair`, `list`, `revoke` |

**Endpoints** (all signed, all answer at once except `poll`); the full
specification is [docs/PROTOCOL.md](docs/PROTOCOL.md):

| Request | Returns |
|---|---|
| `GET /a2/hello` | nonce, proof, agent list, protocol version |
| `POST /a2/send?a=agent`, body = text | the message's number |
| `GET /a2/poll?a=agent&after=n&w=ms` | reply parts after `n`, and whether the agent is still working; waits up to `w`, at most 4 s |
| `GET /a2/hist?a=agent&before=n` | earlier parts, from the plugin's own log |

Replies are a header line carrying the signature, then `word value`
lines and length-prefixed message records, at most 1,900 bytes, never
JSON: the Apple must hash the exact bytes it received, so it reads each
reply whole and parses it itself (milestone 1 notes).

**Why not something built in.** `/v1/chat/completions` needs the gateway
token. `/hooks/agent` never returns the model's words. WebChat is
WebSocket only.

## 4. Which card commands carry it

| Path | Verdict |
|---|---|
| **`HTTP` ($20) with `KEEPALIVE`, the body read from its `DATA` events** | **Yes.** One connection is reused while requests come inside the card's 8 s cutoff. Extra header lines carry the signature. A reply is at most about 1.9K, two `DATA` events, then `HTTP_DONE`: all asked for, nothing near the 16 KB FIFO, and no disk call until it is drained. |
| `HTTP` with `RETAIN`, then `JSON_GET` (the reference's chapter 10 pattern) | No, since milestone 1. The signature covers the exact reply bytes, and `JSON_GET` hands back decoded values, not those bytes. |
| `STREAM_OPEN` per request | Fallback for GETs. Proven in Newsiie, but no headers and no body, so the signature would have to ride in the URL. |
| WebSocket through `CONNECT` (firmware 1.36) | No. Pushed `DATA` events are discarded by the slot ROM during disk calls, and a2chat saw socket data dropped. |

None of our projects has run `HTTP` or `KEEPALIVE` on the real card.
Milestone 2 does.

Polling: every 5 s when idle (inside the keep-alive cutoff, and the
agent may speak first, from a cron job or heartbeat); after a send, back
to back with `w=4000` until the agent is done. The //e has a one-key
latch, so the wait loop collects keys into a type-ahead buffer.

## 5. Screens

Rows 0–22 content, row 23 the inverse status row.

```
AGENTS              | main                                             thinking
--------------------|-----------------------------------------------------------
>main               |
 *coder             |                        What's on my calendar tomorrow?
 home               |                                                  14:01
                    |
                    |  Two things:
                    |  - 09:30 Senior seminar, room 214
                    |  - 15:00 Dentist
                    |  Nothing else until Thursday.
                    |  14:01
                    |
                    |-----------------------------------------------------------
                    | > Move the dentist to Friday_
 TWOEGRAM  MAIN   SIGNED, NOT ENCRYPTED   COGS 192.168.1.42   OA-? HELP
```

1. **Splash** — finds the card, waits for the link, says hello.
2. **Agent list** — 20 columns at the left: the gateway's agents, one
   conversation each, a diamond where there is something unread.
   `OA-B` hides it and the conversation takes all 80 columns; the status
   row then says when another agent has spoken.
3. **Conversation** — **your messages at the right, the agent's at the
   left.** Yours wrap to two thirds of the pane. The agent's may use
   nearly the full width, since its answers are long and may hold code.
   Text is stored unwrapped and re-wrapped when the list is hidden or
   shown. The compose line grows to three rows.
4. **Key entry** — first run: type the 26-character key, with a check
   character so a slip is caught before anything is sent.
5. **Help** — the MouseText box.

Keys: `Tab` list or conversation; arrows; `Return` send; `OA-up` and
`OA-down` scroll; `OA-B` list; `OA-N` new conversation (sends `/new`);
`OA-L` lock (clear the screen, ask the passphrase); `OA-?` help; `OA-Q`
quit. OpenClaw's own slash commands are just typed. The status row shows
`SIGNED, NOT ENCRYPTED` or the reason there is no session: `NO COGS
CARD`, `NO NETWORK`, `GATEWAY NOT ANSWERING`, `GATEWAY DOES NOT KNOW THE
KEY`, `BAD SIGNATURE`.

## 6. Code and memory

cc65 C with ca65 for the hot paths, Newsiie's build checks included.

| Module | Origin |
|---|---|
| `cogs_io.s`, `cogs.c`, `slot.c`, `net.c`, `tick.c`, `fmt.c`, `auxmem.s`, `wrap.c` | Newsiie, as is |
| `blit.s`, `screen.c`, `kbd.c`, `status.c`, `prompt.c`, `splash.c`, `help.c` | Newsiie, adapted |
| `cogs_http.c` — `HTTP`, `PREFS`, `HASH`, `RANDOM` | new |
| `hmac.c`, `auth.c` — HMAC, handshake, sequence numbers, base-32 | new, portable C |
| `proto.c`, `store.c`, `agents.c`, `convo.c`, `compose.c`, `poll.c` | new |
| `plugin/` — the OpenClaw channel, with a stand-alone dev server and an echo agent | new, TypeScript |

Main memory holds the program; aux memory (47K) holds the open
conversation. Newsiie's 18K of parser, cleaner and buffers is not
needed, which is the room for everything above. The key lives in main
memory only while the program runs and is overwritten at quit. Nothing
is saved to disk: no key, no transcript, only `TWOEGRAM.CFG` with the
gateway's address and port.

## 7. Testing

1. **`make host-test`** — HMAC against published test vectors and
   against Node's; the handshake against the plugin's own code; `proto.c`
   and the store; `cogs_http.c` against the card model.
2. **Plugin (Vitest)** — signatures, replay, lockout, size caps, text
   folding, against the echo agent. A Python script plays the Apple.
3. **Virtual ][** — no card there; canned replies from disk drive every
   screen.
4. **Real card** — milestones 2, 6 and 8.

## 8. Milestones

| # | Deliverable | Verified by |
|---|---|---|
| 0 ✅ | Scaffold from Newsiie; splash, help, status bar; the two-pane screen with made-up text, list hide and show, re-wrap | host tests ✅, Virtual ][ screenshots ✅ |
| 1 ✅ | `docs/PROTOCOL.md`; the plugin's HTTP, signing and queue code running stand-alone with the echo agent; the Python client | Vitest ✅, Python client against the dev server ✅ |
| 2 | `A2PROBE.SYSTEM` on the real card, against the stand-alone server on the LAN: `HTTP` with headers and a body, the reply from `DATA` events, keep-alive reuse, HMAC timing, `PREFS` round trip, then a ProDOS read | **real card**: settles section 9 |
| 3 | `hmac.c`, `auth.c`, key entry, handshake | host tests, real card |
| 4 | Conversation view, store, scrolling, history | host tests, Virtual ][ |
| 5 | Compose, send, type-ahead, polling, replies arriving in parts | Virtual ][, real card with the echo agent |
| 6 | The plugin inside OpenClaw: install, routes, inbound, outbound, allowlist, sessions, CLI | **your gateway**, Python client, then the real card |
| 7 | Agent list, unread marks, lock and passphrase, error states | real card |
| 8 | Manual with a security chapter, release disk, plugin package; `openclaw security audit` clean | real card, your gateway |
| later | Encryption, by one of the two routes in section 2 | |

0 and 1 are independent. 6 needs only 1.

## 9. Risks to settle at milestone 2

- **`HTTP` + `KEEPALIVE`, with the reply read from `DATA` events, on the
  real card**, and
  while booted from the network disk.
- **`PREFS` bytes 15–31:** free for host software by the reference, but
  another program may use them too. If they are taken, the key goes in a
  file on a local (non-network) volume, or is typed each time.
- **Poll cost** over plain HTTP, with and without keep-alive.

At milestone 6: the SDK pages not yet read (`sdk-channel-inbound`,
`sessions-and-bindings`, `manifest`), whether a plugin may open its own
listener, body limits on plugin routes, and whether the plugin can tell
that a run is still in progress.

## 10. Decisions for you

Decided (2026-11-15): LAN only, for the client and for OpenClaw itself;
signed but not encrypted for now, encryption later; OpenClaw
**2026.11.3** is the version the plugin targets and pins; the name stays
**Twoegram**.

Still open, neither blocks milestones 0 to 5:

1. **Key in the card's flash** (recommended) or typed at every start.
2. **Which agents** the Apple may talk to. A dedicated agent with fewer
   tools is the safest, but that is your call.

### Milestone 0 notes

- **Scaffold.** From Newsiie, unchanged: `blit.s`, `screen.c`, `kbd.c`,
  `tick.c`, `fmt.c`, `cogs_io.s`, `cogs.c`, `slot.c`, `net.c`, the build
  checks (`cc65check.py`, `memcheck.py`), `mkdisk.sh`, `vii.sh` (which
  gained `tab`), `bootstrap.sh`, the linker config and the card-model host
  test. `auxmem.s`, `cogs_extra.c` and `fmt_extra.c` are copied but not
  linked into the app yet. New: `layout.c`, `convo.c`, `demo.c`,
  `status.c`, `splash.c`, `help.c`, `main.c`.
- **Layout** (`layout.c`): messages in, screen lines out. Lines hold a
  message number and offset, not a pointer, so the text can move to aux
  memory at milestone 4 without touching the screen code. Yours wrap to
  two thirds of the text area and sit as one block against its right
  edge, text still reading from the left; the agent's wrap to the width
  less three. Leading spaces (code) are kept; a line never breaks inside
  its indent; a word wider than the bubble is split. The time sits on its
  own line under each message. The table holds 200 lines; when full, the
  oldest whole messages are dropped and the heading says "older not
  shown". 350 host checks.
- **Screen** (`convo.c`): headings on row 0; row 1 is `_` along its foot,
  so the divider (column 20, a stroke down the right edge of its cell)
  starts exactly where the rule runs and the two meet; the compose rule,
  from column 21, meets the divider the same way. Checked in the
  screenshots. The compose area grows to three rows and keeps the end in
  view. The conversation sits at the top until it fills the pane, then
  the newest is at the foot.
- **Your messages: plain and right-aligned** (decided 2026-11-15 from
  the screenshots). The inverse version built for comparison is removed,
  with its Open-Apple-I key.
- **Demo data** (`demo.c`, behind `msgs.h`): three made-up agents, a code
  block, an address longer than a bubble. A sent message gets a canned
  reply saying nothing was sent. The status bar says `DEMO, NOT
  CONNECTED`.
- **Size:** 14,180 bytes; BSS ends at `$6927`, 21K of heap left.
- **Screenshots** (`make shots`, `tools/shots.sh`): splash, chat, list
  focus, code, list hidden, compose, sent, help, in
  `docs/images/`.
- Not checked: an unenhanced //e (no MouseText), and real hardware.

### Milestone 1 notes

- **The protocol** is [docs/PROTOCOL.md](docs/PROTOCOL.md). Choices made
  while writing it:
  - **No JSON.** The Apple verifies each reply's HMAC over the exact
    bytes, so it must hold the whole reply; `JSON_GET` gives back decoded
    values, not bytes. A reply is a header line (`A2 1 <code> <mac>`),
    `word value` lines and length-prefixed `msg` records, at most 1,900
    bytes, so one fits a 2K buffer and its HMAC fits two card `HASH`
    calls (2,045 bytes each). Section 4 is changed to match.
  - **The key's check character is modulo 37** with Crockford's check
    symbols (`* ~ $ = U`), not modulo 32: the first version missed some
    single-character slips, which a test found. 37 is prime and above
    every weight and difference, so every single slip and every swap of
    neighbours is caught (tested exhaustively on 20 keys).
  - **The device id is derived from the key**, so nothing extra is typed.
  - **Unverified requests never get signed replies,** or anyone could
    collect signed refusals for future sequence numbers and replay them.
- **The plugin** (`plugin/`, TypeScript run directly by Node with type
  stripping, no build step, no runtime dependencies): `protocol.ts`
  (keys, HMACs, records), `text.ts` (Markdown and Unicode to ASCII,
  splitting), `devices.ts` (keys in a JSON file, mode 0600), `server.ts`
  (sessions, sequence numbers, lockouts, conversations, long poll),
  `agent.ts` (the interface OpenClaw will fill, and the echo agent),
  `dev-server.ts` and `cli.ts`. Conversations are in memory for now;
  keeping them across restarts is milestone 6's question.
- **Tests:** 43 Vitest tests over real HTTP, with a client that verifies
  every reply as the Apple will: RFC 4231 HMAC vector, key round trips
  and typo detection, unknown device, wrong proof, lockouts by device
  and by address, stolen session id, replay (refused and not acted on),
  idle expiry, size limits, non-ASCII input, busy flag, long poll, an
  answer cut into parts that each fit, unread counts, history paging,
  devices kept apart. `npm test`, `npm run typecheck`.
- **`tools/a2client.py`** plays the Apple (standard library only, written
  separately from the TypeScript so each checks the other): `hello`,
  `send`, `poll`, `hist`, `chat`. Against the dev server: a session, a
  signed send and its answer by long poll, a long answer in seven parts
  with the code block's indent kept (a bug this found and fixed), a
  mistyped key caught before sending, an unknown key refused.
- **Running it:** in `plugin/`, `npm install`, then `npm run pair` to make
  a key and `npm run dev` for the server (loopback; `--host 0.0.0.0` for
  the LAN). Node here is v25.6.1; OpenClaw asks for 24 or 26.
- **Dev dependencies** moved to Vitest 4 after `npm audit` flagged
  Vitest 3's worker pool and mocker; test-only, but no reason to keep
  them. Audit now clean.
