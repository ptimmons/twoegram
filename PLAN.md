# Twoegram — implementation plan

A chat client for the Enhanced Apple //e with a CoGS card, and the
OpenClaw channel plugin it talks to. (The name is kept from an earlier
Telegram plan, now dropped.)

Newsiie is the design language (80-column screen, splash, inverse status
bar, MouseText help box) and the source of the screen, keyboard, prompt,
CoGS and aux-memory code.

**Status:** release 1.0 done: milestones 0 to 8 built and passed on the real card and your gateway (2026-11-16; the Apple's disk b019, plugin 1.0.1). Section 9's requests are built (2026-11-17). Also built 2026-11-17: the same channel for Hermes Agent (`hermes/`; see "Hermes Agent" below), installed on the Mac mini the same day. Planned 2026-11-17: several agents on several gateways, by keys made on the Apple and a relay ([docs/RELAY.md](docs/RELAY.md); "Several agents" below).

Sources read: `~/repos/newsiie` (PLAN.md, `cogs.h`, `ui.h`, `screen.h`);
the CoGS Technical Reference (firmware 1.39, rev 33) chapters 1, 3, 4, 5,
9.10, 10 and Appendix A; `~/repos/a2chat/a2chat/docs/COGS.md`; and the
OpenClaw documentation at docs.openclaw.ai on 2026-11-15 (channel
plugins, gateway routes, pairing, access control, network exposure).
At milestone 6 the plugin was written against the SDK declarations and
docs in the OpenClaw 2026.11.3 package and run in a scratch gateway on
the Mac.

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
| Routes | its own LAN listener, started by the channel's `gateway.startAccount` and closed when the account stops (built at milestone 6; no route on the gateway, which stays on loopback) |
| Inbound | a verified message goes to the agent through the SDK's direct-message path (`dispatchInboundDirectDm`); its `deliver` callback brings the reply back |
| Outbound | replies arrive through `deliver` and join the device's conversation for its next poll; a message the agent starts goes through `sendText` to `a2:<device>.<agent>` |
| Who may talk | the device list is the allowlist; `dmPolicy` is `allowlist`, never `open` |
| Sessions | each device and agent pair is one peer, `<device>.<agent>`, routed by a binding the plugin adds per message, carrying `dmScope: "per-channel-peer"`: never the shared main session |
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
| 2 | `A2PROBE.SYSTEM` on the real card, against the stand-alone server on the LAN: `HTTP` with headers and a body, the reply from `DATA` events, keep-alive reuse, HMAC timing, `PREFS` round trip, then a ProDOS read | host tests ✅, Virtual ][ no-card paths ✅, **real card ✅** (b001 and b002, all checks; [docs/PROBE.md](docs/PROBE.md)) |
| 3 ✅ | `hmac.c`, `auth.c`, key entry, handshake | host tests ✅, Virtual ][ ✅, **real card ✅** (b005: set-up, sign-in) |
| 4 ✅ | Conversation view, store, scrolling, history | host tests ✅ (the screen too), Virtual ][ ✅, **real card ✅** (b007) |
| 5 ✅ | Compose, send, type-ahead, polling, replies arriving in parts | host tests ✅ (the screen too), Virtual ][ ✅, real card ✅ (b008) |
| 6 ✅ | The plugin inside OpenClaw: install, routes, inbound, outbound, allowlist, sessions, CLI | Vitest ✅, scratch OpenClaw 2026.11.3 on the Mac ✅ (no model), **your gateway and the real card ✅** (2026-11-16, b010) |
| 7 ✅ | Error states: every failure said plainly, and recovered from where it can be (the agent list and unread marks were done in b010; the lock and passphrase moved to section 9, item 6) | host tests ✅, **real card and your gateway ✅** (b017) |
| 8 ✅ | Manual with a security chapter, release disk, plugin package; `openclaw security audit` clean | scratch gateways ✅, **real card and your gateway ✅** (b019, plugin 1.0.1) |
| later | Encryption, by one of the two routes in section 2 | |

0 and 1 are independent. 6 needs only 1.

## 9. Requests for later builds

Asked for during testing, to fit in after the milestones they would
otherwise interrupt:

1. **One key to the very beginning and end of the conversation**
   (2026-11-15, milestone 4's card run: paging there was a nuisance;
   built 2026-11-16 in b021 with plugin 1.1.0; with the scroll arrows in b022 and the narrower list in b023, not yet on the card).
   Open-Apple-< and -> today reach only the oldest and newest messages
   held. The end is easy (load the newest page). The beginning needs the
   oldest page in one request rather than paging back through every one:
   a `hist` that counts from the start (for example `from=1`) in the
   protocol and the plugin, then the same window logic.
2. **Save the conversation to disk** (same day). A ProDOS text file, as
   Newsiie's Open-Apple-S saves an article: the conversation as text with
   who said what and when, never during a request (the network-disk
   rule), retried on $27. Main memory is the obstacle (milestone 4
   notes), so it likely belongs in an overlay loaded from disk. Built
   2026-11-17 in b033, not yet on the card: see "Saving the
   conversation" below.
3. **Marks instead of words** (2026-11-15, after milestone 6), done in
   build b010: a MouseText down arrow at the heading's right replaces
   "scrolled back"; a diamond beside the agent's name, in the heading and
   the list, replaces "new below" and clears at the newest end; other
   agents' diamonds and counts come from the `unread` lines every poll
   already carried (only the open agent is polled); thumbs-up and check
   marks arrive as byte `$02` and are drawn as MouseText's check mark.
   Glyph bytes 1-4 (screen.h) carry MouseText through strings and
   message text, with plain stand-ins on a //e without it. Not yet seen
   on the card: the check mark ($44) and down arrow ($4A) codes are from
   the MouseText table, not probed like the others.
4. **A user's guide in the style of a 1980s Apple manual, with
   screenshots** (same day), as Newsiie's: [docs/MANUAL.md](docs/MANUAL.md).
   The emulator has no card, so the conversation figures are drawn by
   the real screen code on the Mac (`make convo-shots`:
   tests/host/shots.c, a demo conversation on the model gateway) with
   the glyphs Virtual ][ draws (tools/charset/charset.png, captured from
   CHARSET.SYSTEM by `make charset`), pixel for pixel like its own
   screenshots (`make shots`).

5. **Home automation from the Apple** (2026-11-16, first run on the
   real gateway: "turn out the lights in my office" was refused for want
   of a tool). OpenClaw withholds its owner-only tools (`nodes`,
   `terminal`, `computer`, `screen`, `sessions`, `automations`,
   `gateway`, ...) from a sender not in `commands.ownerAllowFrom`, and
   the Apple is not in it. Listing `a2:<device-id>` there made the
   scratch gateway treat the device as owner (its slash commands, which
   a non-owner loses once an owner list exists, answered again); not yet
   tried on the real gateway.

   **2026-11-17: no owner needed.** Pinchy reaches Home Assistant over
   its REST API with a long-lived token, from its shell tool. `exec` is
   not among OpenClaw's owner-only tools (`GATEWAY_OWNER_ONLY_CORE_TOOLS`,
   2026.11.3), and a test device paired on claw1 asked, as an a2 sender,
   for a light's state: "Candle 12 (light.candle_12) is off", through
   "gateway_exec", with no approval asked and nothing changed. Later
   that day you switched lights from Twoegram itself. Nothing in
   Twoegram or the plugin changed between the first refusal and this;
   the refusal is not explained (perhaps Pinchy had not yet been given
   the token, or had not yet noted how to use it). So the
   owner work (`a2 owner`, `pair --owner`, the owner list in INSTALL.md)
   is dropped. The other side: any a2 sender can already run a shell on
   claw1 and use the token, which is what the passphrase and lock
   (item 6) guard. If that is too much, the next step would be a narrower
   tool policy for a2 senders, not an owner switch.
6. **Lock and passphrase** (planned for milestone 7; moved here
   2026-11-16, since eavesdroppers and interlopers at home are not a
   worry for now; built 2026-11-16 in b028, passed on the card
   2026-11-17: see
   "The passphrase and the lock" below). The design, ready to build: the card's flash holds the
   device key XORed with a hash of the passphrase (a second mark byte says
   so); Twoegram asks for it at start-up and unscrambles the key in
   memory, never storing it. The gateway keeps the same key: no protocol
   change, no re-pairing. A wrong passphrase makes a wrong device id,
   which the gateway refuses as unknown (no bad-signature lockout).
   Open-Apple-L wipes the key and session key from memory and blanks the
   screen until the passphrase is typed again, checked on the Apple by
   re-deriving the device id (no network call). The set-up screen gets an
   optional passphrase field. Perhaps an idle auto-lock. Weakness: with
   the card and a recording of LAN traffic, passphrases can be tried
   offline, so a long one matters.

7. **Agents by name** (2026-11-16, done in b016; seen on the card in b017, "Pinchy" from IDENTITY.md): the list, heading and
   status bar show what OpenClaw calls each agent (`identity.name` in its
   config, else the `Name:` line of the workspace's `IDENTITY.md`, as
   `openclaw agents list` shows it), and requests still go by id. The
   hello reply carries `name <id> <label>` lines after the `agent` lines
   (PROTOCOL.md; older clients ignore them): at most 16 printable ASCII
   characters, emoji dropped, accents folded, sent only when it differs
   from the id. Renaming is done in OpenClaw; the Apple sees it at its
   next hello. On the Apple: `ses_labels`, in LOWBSS since every hello
   clears it before it is read, and `ses_agent_at`, now shared with the
   unread counts; about 300 bytes in all (1,837 bytes of heap left).
   Tests: 5 more in Vitest (66), IDENTITY.md parsing as OpenClaw writes
   it, config before file; the screen test checks the list, heading and
   status bar, the 13-character cut, and that requests still name `main`.
8. **Say when the card's key is the trouble** (2026-11-16, low priority;
   done 2026-11-17 in b034, not yet on the card).
   The key lives in the card's `PREFS` blob, bytes 15-31, which the
   reference does not reserve for us: a firmware upgrade, a factory reset
   or another program could change them. If the mark byte goes, Twoegram
   already opens set-up. If the mark survives and the key bytes change,
   the gateway answers BAD_SIGNATURE and the status bar says only NOT
   CONNECTED. All 17 bytes are used, so there is no room for a checksum;
   instead, a bad signature at sign-in should say "The gateway did not
   accept this Apple's key. Pair it again." (and the manual's Appendix B
   the same). Fix for the user either way: `openclaw a2 pair`, type the
   new key, revoke the old device.

   **As built (b034):** most of this was already so. The device id is a
   hash of the key (`ses_device_id`), so changed key bytes make an id
   the gateway has never seen: UNKNOWN_DEVICE, not BAD_SIGNATURE (that
   needs a 32-bit id collision). At start-up an unknown key already
   opened set-up with "The gateway does not know this key. Pair this
   Apple on the gateway, then type its key." What was missing was the
   middle of a conversation (a revocation, or the bytes changing while
   Twoegram runs): the pane said only THE GATEWAY DOES NOT KNOW THIS
   KEY. Now `link_badkey`, set by `link_trouble` for UNKNOWN_DEVICE or
   BAD_SIGNATURE and cleared by any other trouble or a good hello, adds
   a second line to the offline pane: "Pair this Apple again (openclaw
   a2 pair), then Open-Apple-K." 105 bytes; 2,468 of heap left. The
   screen test checks the line after a revocation and its absence after
   a lock-out. The manual's Appendix B says the same and adds revoking
   the old device.

## 10. Risks to settle at milestone 2

- ~~**`HTTP` + `KEEPALIVE`, with the reply read from `DATA` events, on the
  real card**~~ settled at milestone 2: it works, booted from the network
  disk too. The rest of this list is as it stood before; see the notes.
  while booted from the network disk.
- **`PREFS` bytes 15–31:** free for host software by the reference, but
  another program may use them too. If they are taken, the key goes in a
  file on a local (non-network) volume, or is typed each time.
- **Poll cost** over plain HTTP, with and without keep-alive.

At milestone 6: the SDK pages not yet read (`sdk-channel-inbound`,
`sessions-and-bindings`, `manifest`), whether a plugin may open its own
listener, body limits on plugin routes, and whether the plugin can tell
that a run is still in progress.

## 11. Decisions for you

Decided (2026-11-15): LAN only, for the client and for OpenClaw itself;
signed but not encrypted for now, encryption later; OpenClaw
**2026.11.3** is the version the plugin targets and pins; the name stays
**Twoegram**.

2026.11.4 (2026-11-16, a fixes release): the plugin type-checks against
its SDK and ran in a scratch 2026.11.4 gateway (install, listener,
sign-in, `/whoami`, a message dispatched to the agent, the name from
IDENTITY.md). The plugin's range, `>=2026.11.3 <2026.12.0`, already
admits it.

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

### Milestone 2 notes

- **Built, not yet run on the card.** How to run it, and what each line
  means: [docs/PROBE.md](docs/PROBE.md).
- **Card layer.** `cogs_request` is split into `cogs_send` (a payload in
  pieces, nothing copied to join them), `cogs_next`, `cogs_read`,
  `cogs_drop` and `cogs_reply`, so commands that end in events can read
  their own frames. Newsiie's 719 card checks still pass. New in
  `cogs_http.c`: `HTTP` on socket 0 with `KEEPALIVE`, the body gathered
  from `DATA` events up to `HTTP_DONE` or an `ERROR` on the socket, other
  sockets' events skipped, an over-long body read and dropped
  (`COGS_E_TOOBIG`); and `PREFS_GET`/`PREFS_SET`.
- **HMAC** (`hmac.c`): two card `HASH` calls, the key padded and XORed on
  the Apple, the message sent in up to six pieces. **Keys and replies**
  (`proto.c`): the written key with its mod-37 check, hex, the reply's
  header line, and an iterator over its lines and message records.
- **Tests:** 907 host checks on a model card that really hashes (a C
  SHA-256 in `tests/host`), against known answers from the plugin's
  TypeScript and RFC 4231: five keys and their device ids, every
  one-character slip in a key, the hello proof, session key, a request
  signature and a signed reply byte for byte, the 1,981-byte message
  limit, `HTTP` with others' events interleaved, an over-long body, a
  card error, a 404, and the prefs blob.
- **The probe** (`a2probe.c`, 23K) reads `A2PROBE.CFG` (the server's
  address and a test key, put on the disk by `make disk URL=... KEY=...`)
  before any network use, then: hello with both proofs, a signed send,
  the echo agent's answer by long poll, 20 timed polls, polls after 6 s
  and 10 s idle, a replay and a forgery (both must be refused), 20 HMACs
  of 1,900 bytes, the prefs blob (a write and exact restore of bytes
  15-31 only if you press W), and a ProDOS block read last.
- **Checked in Virtual ][** (no card there): no config file, a config
  file with no card, and a mistyped key each give their own message, and
  quitting returns to ProDOS.
- **Assumed, for the card to settle:** extra header lines separated by
  CR LF with none after the last (the reference says only "CRLF or LF
  separated"); that `KEEPALIVE` reuses the connection across requests
  inside 8 s (the dev server now logs each connection opened and
  closed); that socket 0 is free (the card had one socket open before
  Newsiie touched it).
- **Size:** the app grew to 14,467 bytes with the split card layer.

### Milestone 2: the first card runs

- **Run 1 and run 2 (2026-11-15), card in slot 5, firmware 1.42, booted
  from the network disk.** The card's `HTTP` command reached the dev
  server on the Mac, the reply came back through `DATA` events and
  `HTTP_DONE`, and the probe parsed it: the largest unknown of this
  milestone works. The prefs blob holds 15 bytes, so bytes 15-31 are free
  for the key; W wrote them, read them back and restored the original
  exactly. HMAC: 50 of 1,900 bytes in 2 s by the card's clock, about 40
  ms each (run 1's frame count was meaningless and is gone). The boot
  volume read fine after.
- **The hello was refused as an unknown device** both times. The disk's
  key was in the server's store (the Python client got a session with
  it), and the probe's new key fingerprint showed the 6502 parsed it the
  same as the Mac (2D5F in Virtual ][ and on the card). So the device id,
  `SHA-256("twoegram-id" || key)`, came out wrong on the Apple: 8c82f485
  for 151a92be, and no truncation, padding or double hash of the right
  input explains it.
- **The cause: cc65 2.18 miscompiles a function returning a 4-byte
  struct.** The probe's `part()` returned a `cogs_part_t` (pointer and
  length). cc65 built it in memory but returned only the first two bytes
  in A/X, never loading `sreg`, and every caller took a stale `sreg` as
  the length, so the card hashed the right bytes at wrong lengths. The
  Mac's compiler gets it right, so the host tests could not see it.
  Fixed with `COGS_PART()`, a macro; `tests/cc65/struct_return.c`
  reproduces the bug; **`tools/cc65check.py` now fails the build on any
  function returning a struct or union by value** (it found only this
  one).
- **Build numbers** so runs cannot be confused: `BUILDNUM` holds the next
  number; each `make disk` takes it and makes `build/TWOEGRAM-bNNN.po`
  with volume `/TWOEGRAM.BNNN`, and the number shows on the splash, in
  help and in the probe. A new file name per build also means the card's
  disk cache can never serve an older image under it. The dev server
  names its version and git commit at startup and in every hello reply
  (refusals too), and the probe shows it ("Server is"), so each photo
  records both sides. It also logs the device id of any refused hello.
  The probe's status bar now says when it is waiting for W.

### Milestone 2: run 3, all checks passed (2026-11-15, build b001)

Probe build b001 against `twoegram-dev 0.1.0 569147e+` on the Mac, card
in slot 5, firmware 1.42, booted from the network disk.

- **Every check passed:** a hello proved both ways (device 151a92be, so
  the cc65 fix holds), a signed POST stored as message 1, the echo
  agent's answer by long poll, 20 polls with every reply genuine, polls
  after 6 s and 10 s idle, the replay refused (code 5, signed) and the
  forgery refused (code 3, unsigned) as they should be, the prefs write
  and exact restore, and the boot volume read after all of it.
- **Settled:**
  - The card's `HTTP` command with a body and extra header lines works.
    The lines are separated by CR LF with none after the last, and the
    server received all three intact.
  - Replies arrive whole through `DATA` events and `HTTP_DONE`, socket
    0 was free, and the network disk kept working throughout.
  - The prefs blob holds 15 bytes, so **bytes 15-31 are free for the
    device key**, and a write and exact restore work.
- **Measured:**
  - **A poll round trip is about 300 ms:** 20 in 6 s by the card's
    clock. That includes signing the request and verifying the reply.
    Polling every 5 s when idle is cheap, and an answer shows within
    a second of the server having it.
  - **An HMAC of 1,900 bytes takes about 140 ms:** 50 in 7 s. Run 2's
    "50 in 2 s" was wrong: the struct-return bug also gave those HMACs
    a stale length. Verifying a full reply costs about that, and a
    short one much less, since the time is mostly bytes moved to the
    card. Moving `cogs_putn`'s loop is the lever if it ever matters.
- **Keep-alive, half settled.** The server log, complete as pasted,
  shows one connection from the hello until 10 s after the last request,
  when the server's own keep-alive timeout closed it. So **the card
  reused one connection for every request of the run**, two dozen of them.
  But the two idle polls answered in 1 frame each: the probe's idle waits
  ended at a keypress, and the W pressed early for the prefs prompt
  latched and cut both waits to nothing. So reuse after a 6 s pause, and
  a fresh connection after 10 s, are still untested.
- **Fixed for the next run (build b002):** the idle waits ignore the
  keyboard; the W prompt clears any key pressed before it; and the dev
  server puts the connection's number in every reply (`conn N`), which the
  probe shows on the Hello, 20-polls and Idle lines, marking "(the same)"
  or "(a NEW one)", so keep-alive can be read off the photo.
  `make disk` now keeps the probe's address and key in `probe.cfg`
  (gitignored, mode 600), so a disk can no longer be built without them
  by accident, as b002 first was.

### Milestone 2: run 4, keep-alive settled (2026-11-15, build b002)

All checks passed again, against the same server (`569147e+`).

- **The card keeps one connection across idle gaps.** The probe waited
  7 s (by the card's clock) and then 10 s, and both polls came back on
  connection 1, "(the same)". The server log shows one connection for
  the whole run, opened at the hello and closed 35 s later, 10 s after the
  last request, by the server's own keep-alive timeout. The card's "8 s
  keep-alive stall cutoff" (reference chapter 10) did not close it, so
  for this protocol the server's idle timeout sets the limit. Polling
  every 5 s when idle keeps the connection open for good.
- **Measured again:** 20 polls in 5 s (run 3 said 6), so about 250-300 ms
  a round trip on a reused connection. 50 HMACs of 1,900 bytes in 7 s.
- **Still untested:** a request after the server has closed the parked
  connection (an idle gap longer than the server's timeout, or a gateway
  restart). The card must notice the dead connection and open a new one.
  The app must survive this, so it will be tested at milestone 5. The
  probe could check it sooner with an idle gap of 15 s.
- The Hello line's connection number runs past column 80 (cosmetic).

### Milestone 3 notes

- **Decided: the key lives on the card** (the plan's recommendation;
  milestone 2 showed bytes 15-31 free). `keystore.c`: byte 15 is a mark
  ($A2), bytes 16-31 the key, bytes 0-14 (the card's own settings) always
  written back exactly as read, and every write read back. It refuses to
  write when the card has none of its own settings stored (it would have
  to invent them) or when bytes 15-31 hold another program's data. Its
  clear goes back to the card's own fifteen bytes.
- **The gateway's address** is `TWOEGRAM.CFG` (`GATEWAY=http://...`),
  read once at startup before any network use and written only by the
  set-up screen, retried on the network disk's $27. `config.c` adds
  `http://` and port 8417 when missing, lowercases the host, and refuses
  https and anything with a path or spaces.
- **`session.c`** is the protocol: device id, hello with both proofs,
  signed requests, every reply verified (an OK must be signed), and one
  quiet hello again when the gateway has lost the session (a restart).
  The probe now runs on it too, so its next card run tests the app's own
  code. `link.c` ties start-up together: address, key, set-up when
  either is missing or the gateway does not know the key, then hello,
  with up to 20 s for the card's network first.
- **The set-up screen** (`setup.c`, Open-Apple-K): gateway and key
  fields, the key grouped in fives as typed with dashes optional, typing
  slips caught before anything is sent, the key never shown back and
  wiped from memory once parsed. Open-Apple-X there removes the key from
  the card. Problems that do not stop the session (a write-protected
  disk, a card that cannot keep the key) are said, then it connects
  anyway. The key and session key are wiped at quit.
- **The app** shows the gateway's agents in the list and says it is
  signed in; messages remain milestones 4 and 5. With no card (the
  emulator) it shows milestone 0's made-up text, labelled `DEMO, NO COGS
  CARD`.
- **A hazard fixed:** with no card found, `cogs_slot` is 0, and slot 0's
  I/O addresses ($C080-$C08F) are the language card's switches. Every
  card command now goes through `cogs_send`, which refuses when there is
  no card, so nothing can touch them; a host test checks no byte is sent.
- **Memory:** the app grew to 30.7K with the protocol linked in and ran
  out of heap (794 bytes; memcheck stopped the build). The reply buffer
  (2K), the demo pool (2K) and the layout table (1.6K) moved to LOWBSS
  ($0800-$1FFF, never loaded, never zeroed, each written before it is
  read), as Newsiie did: 6.4K of heap now.
- **Tests:** 967 host checks in `test_a2`, now with a model gateway in C
  (the server's side of the protocol): a hello proved both ways, signed
  GET and POST checked by the gateway, a reply changed after signing
  caught, replay and forgery refused, a lost session recovered by one
  hello, an unknown key, a gateway that cannot prove itself, a non-
  protocol answer, a card error; the key store on 0, 15 and 32 stored
  bytes and with another program's data; address parsing.
- **Checked in Virtual ][** (b003, b004): the made-up conversation with
  `DEMO, NO COGS CARD`; the set-up screen; a mistyped key caught ("THE
  KEY DOES NOT CHECK"); the right key accepted, `TWOEGRAM.CFG` written as
  TXT, the key's failure to reach a card said, and back to the screen.

### Milestone 3 on the card (2026-11-15, build b005)

- **Set-up and sign-in work.** On a disk with no `TWOEGRAM.CFG` and a
  card with no key, the set-up screen came up; the gateway and the test
  key were typed, saved, and the hello proved both ways: "Signed in as
  device 151a92be", the agent list `main` and `coder` from the dev
  server, `SIGNED, NOT ENCRYPTED` and the card's address on the status
  bar. The server logged the hello on a new connection, closed 11 s
  later by its own timeout since nothing else was asked.
- **A restart signs in with no questions:** the address came from
  `TWOEGRAM.CFG` (so the network disk's share accepted the write) and the
  key from the card's prefs blob.
- Not yet tried on the card: Open-Apple-X, and the error paths (a
  mistyped key, the server stopped, an unknown key); all are covered by
  host tests, and the mistyped key also in Virtual ][.

### Milestone 4 notes

- **The store** (`store.c`): the open conversation's messages, their
  text in aux $0800-$BEFF and an index of up to 64 in main memory; always
  contiguous in the gateway's numbering. A window that slides: adding
  older messages when full drops the newest, and the other way round;
  `st_older` and `st_newest` say whether the gateway has more beyond each
  end. Text is compacted in aux when an addition does not fit. `/RAM` is
  disconnected at startup, as in Newsiie, and 128K is required.
- **History** (`history.c`): the newest page (`hist`, 20) when an agent
  is opened, older pages (`hist before=`) at the top, newer ones (`poll
  after=`) at the bottom; each reply verified before anything in it is
  stored.
- **The screen** (`convo.c`): the layout is a window on the held
  messages, ending at a chosen message and reaching back up to 200 lines.
  Up past the top brings older messages in (from the store, or fetched),
  with the window ending at the screen's last row so everything shown
  stays, and the top line moved down exactly the step; Down past the
  bottom does the reverse, moving the window on by fewer messages when
  long ones would push the screen's top row out of the layout.
  Open-Apple-< and -> jump to the oldest held and the newest.
- **A test of the screen itself** (`tests/host/test_convo.c`): the real
  `convo.c` on the Mac, with a text screen in place of the Apple's,
  scripted keys, and the model gateway serving 100 messages (every
  seventh at 1,500 bytes). It goes up one line at a time from message 100
  to message 1 and back (1,254 steps each way, 24 and 9 fetches), and
  checks that every single step moves the pane by exactly one row. It
  found three bugs that the emulator could not have shown, all fixed: the
  window first ended at the screen's top message, leaving nothing to fill
  the rows below it; a message's first line was taken to be the blank
  before it; and `convo_run()` reloaded the conversation on each entry
  (harmless in the app, which enters once).
- **The dev server** takes `--seed N`: each conversation starts with N
  made-up messages. `make disk` now writes `TWOEGRAM.CFG` with the
  address from `probe.cfg` too, so each new build signs in with no
  questions.
- **Memory is full again.** The app is 35.4K; BSS reaches $B73D, leaving
  1,475 bytes of heap against the 1,295 a file open needs. The language
  card holds the layout, help and history code (2.8K of its 3K). Before
  milestone 5 adds sending and polling, room has to be made: the
  candidates are milestone 0's made-up conversations (1.2K of text and a
  2K pool in LOWBSS, there only for the emulator), and the set-up screen
  (2.8K, used rarely) as an overlay loaded from disk.
- **Tests:** 719 card, 350 layout, 1,107 protocol/session/history (with
  a 100-message model conversation paged to the start and back), 756
  store (index and aux full, compaction, empty texts), 1,268 screen.
- **Checked in Virtual ][** (b006): start-up claims aux memory, the
  made-up conversation scrolls to its first message, quitting returns to
  ProDOS.

### Milestone 4 on the card (2026-11-15, build b007)

- **Everything worked**, against the dev server seeded with 100 messages
  an agent: the newest page on opening, scrolling up through fetches to
  the start and back down, long answers wrapped, a code block kept line
  for line, "scrolled back" in the heading, and `coder`'s own history on
  opening it. Fetches took "a few seconds at most".
- Two requests came out of it, now section 9: one key to the very
  beginning and end, and saving the conversation to disk.

### Milestone 5 notes

- **Memory first.** Milestone 0's made-up conversations went
  (`demo.c`: 1.2K of text, 1K of code and a 2K pool), replaced by
  `msgs.c`, which serves only the gateway's agents and the store. With no
  card the screen now says so ("No CoGS card was found in any slot") and
  offers Open-Apple-K. The text buffer moved into the freed LOWBSS. The
  history code grew past the language card and went back to main
  memory. Now: 34.4K, 3.2K of heap, 1.2K of the language card free.
- **Sending** (`hist_send`): Return posts the compose text; the message
  as the gateway stored it comes back and joins the conversation at its
  newest end (loading the newest page first if scrolled far back). On a
  failure the text stays in the compose area to send again.
- **Polling** (`hist_poll`, `convo.c`'s `wait_key`): while no key is
  pressed, a poll every 5 s, or every second while the agent is
  answering. New messages are added when the window holds the newest;
  at the foot the screen follows them; scrolled back, the screen stays
  exactly still and the heading says `new below` until the foot is
  reached. `busy` from the gateway shows as `thinking...`. Short polls,
  not long ones: the keyboard is never left for more than a round trip.
- **Type-ahead** (`kbd.c`): the //e latches only one key, so while a
  request waits on the card, its idle hook moves keys into a 32-key ring
  that `key_get()` drains first.
- **The model gateway** (host tests) accepts sends and answers each one
  poll later, `echo:` and the text, with `busy 1` meanwhile; a test hook
  adds messages as if the agent spoke unprompted.
- **Tests:** the screen test adds sending (the message at the foot, the
  compose line emptied, `thinking...`), the answer arriving by polling
  and `thinking...` ending, a message arriving while scrolled back (the
  pane identical, `new below`, then found by scrolling down), sending from
  the oldest held, and a 200-character message on three compose rows.
  It caught one bug: a message added below the screen while scrolled back
  was not announced.
- **Screenshots:** `make shots` now captures what the emulator can show
  (splash, the no-card screen, help, set-up); the milestone 0 pictures of
  made-up conversations are retired.
- **Not testable off the card:** type-ahead (the host test stubs the
  keyboard) and the timing of polls.

### Milestone 6 notes

- **Read from the real thing.** The OpenClaw 2026.11.3 npm package ships
  its docs and the SDK's type declarations; the plugin was written against
  those, type-checks against them (`npm run typecheck:openclaw`, after
  `npm run sdk` puts the SDK in `../.sdk`, outside the plugin), and was
  run inside a scratch gateway on the Mac (isolated home, loopback, Node
  24 from nodejs.org in the scratch folder: OpenClaw refuses Node 25 for
  some commands).
- **Shape:** `index.ts` (`defineChannelPluginEntry`, the CLI),
  `setup-entry.ts`, `openclaw.plugin.json` (channel `a2`, its config
  schema), `src/channel.ts` (the only file importing the SDK),
  `src/openclaw-agent.ts` (the bridge, SDK-free so it can be tested with a
  fake dispatcher), `src/commands.ts` (`openclaw a2 pair | list |
  revoke`), `src/convstore.ts`.
- **Choosing the agent.** OpenClaw picks an agent by bindings, so each
  message's config gets one more binding, first, sending peer
  `<device>.<agent>` to that agent with `dmScope: "per-channel-peer"`.
  Seen working: `agent:main:a2:direct:d13361f8.main` and
  `agent:coder:a2:direct:d13361f8.coder`. Agents are listed by the SDK's
  own `listAgentIds` (2026.11 keeps them in `agents.entries`; `agents.list`
  is the legacy form), read from the live config, so agents added later
  appear without a restart.
- **Devices.** The device file re-reads itself when another process
  changes it, so `openclaw a2 pair` and `revoke` work while the gateway
  runs. A revoked device's sessions end at its next request, the SDK's
  `assertAuthority` check stops a turn already under way, and its saved
  conversations are deleted.
- **Conversations survive restarts:** one JSON file per device and agent
  under `~/.openclaw/a2/conversations/`, mode 0600.
- **Errors reach the Apple.** If the gateway cannot answer (in the
  scratch gateway: no model), its notice arrives as the agent's reply.
- **Install findings:** the install scan refuses a `node_modules` symlink
  that leaves the plugin, so the SDK used for type-checking lives outside
  it; the manifest category is `channels`; installing needs `--force`
  (not from ClawHub) and `--accept-capabilities`.
- **Folding:** emoji without a name now become `*`, since OpenClaw's
  `/status` uses them as line markers; other unknown characters are
  still `?`.
- **Tests:** 61 in Vitest (43 before): the bridge with a fake dispatcher
  (routing, ids, failures, revocation mid-turn), revocation by another
  process, pairing without a restart, history across a restart, messages
  the agent starts. In the scratch gateway: install, start-up, the CLI,
  sign-in, sends to two agents, replies, `/status`, revoking while
  running, history after a restart, and `openclaw security audit` (no
  finding against `a2` once a device is paired).
- **Not yet seen:** a real model's answer, and replies arriving in
  several parts from a long answer; both need your gateway.
  [docs/INSTALL.md](docs/INSTALL.md) has the steps.

### Milestone 7 notes

- **Scope.** The agent list and unread marks were done in b010, and the
  lock and passphrase moved to section 9 (item 6), so this milestone is
  what happens when a request fails.
- **One place decides** (`link_trouble`, link.c): every failed request
  is one of four kinds, as PROTOCOL.md section 4 asks:
  - *passing* (no network, the gateway down or restarting, `BUSY`, a
    session lost again right after a new hello): the session and the
    conversation stay; the status bar's middle field says `NO NETWORK`,
    `GATEWAY NOT ANSWERING` or `GATEWAY BUSY`; polls back off to 5, 10,
    20, 40, then 60 s; the first good reply puts the bar right. Not
    signed in at all (started while the gateway was down) works the
    same way: hello is retried on the same back-off, without the 20 s
    wait for a network, and the conversation opens when it answers.
  - *this request only* (`BAD_REQUEST`, `TOO_LARGE`, a reply too long):
    said, the compose text kept.
  - *the agent has gone* (`UNKNOWN_AGENT`): hello again for the new list.
  - *needs you* (`UNKNOWN_DEVICE` mid-conversation, i.e. revoked;
    `BAD_SIGNATURE`; `LOCKED`; a reply that does not check; not a
    gateway; no card): offline, never retried by itself, the reason
    from `ses_strerror`, and Open-Apple-K or -R as before.
- **The open agent is followed by name.** Any hello may bring a new
  agent list (the gateway restarted with agents added, removed or
  reordered), so the conversation screen keeps the open agent's name and
  finds it again, else opens the first. Open-Apple-K and -R, start-up and
  every failure share one `restart()` in convo.c.
- **Memory.** About 520 bytes: 2,133 bytes of heap left (2,657 in b010).
  A first version with its own sentence for each failure cost twice
  that; reusing `ses_strerror`'s messages saved half.
- **Tests:** the screen test makes the model gateway time out, refuse,
  go busy, restart with its agents reordered, drop an agent, revoke the
  key, lock it out and forge a reply, and checks for each what is on the
  screen, whether the conversation survives, how often it retries (6 or
  so requests in 200 s while down, against 40 when well), and that it
  recovers (or stays stopped). The model gateway gained a refusal switch
  and a request counter.
- **For the card** (docs/PROBE.md, b017): the gateway stopped and
  started, sending while it is down, starting while it is down, a
  restart mid-conversation, and (optionally) revoking. Not knowable on
  the Mac: how long the card takes to give up on a stopped gateway, which
  is how long the keyboard waits (type-ahead keeps what is typed).

### Milestone 7 on the card (2026-11-16, build b017)

All passed, on the real gateway (OpenClaw 2026.11.3, agent "Pinchy"):

- Gateway stopped mid-conversation: `GATEWAY NOT ANSWERING` in the
  status bar, the conversation kept; started again, the bar put itself
  right with nothing pressed.
- Sent while it was down: `CONNECTION REFUSED` said, the text kept on the
  compose line, and sent once the gateway was back.
- Twoegram started while the gateway was stopped: the trouble shown,
  then signed in by itself when the gateway started, every message
  loaded.
- `openclaw gateway restart` mid-conversation: a moment's trouble, then
  on by itself.
- No pause in typing was noticed while the tries failed (a stopped
  gateway on a running host refuses at once).
- From earlier builds, now seen on hardware: MouseText's check mark
  ($44) for a thumbs-up, and the down arrow ($4A) when scrolled back.
- Not yet seen: a long answer from a real model arriving in parts, and
  revoking (optional). During the tests the agent said its replies had
  been "interrupted before they finish": most likely its turns cut off
  by the gateway stops and restarts. Worth watching for in ordinary use.

### Milestone 8 notes

- **Version 1.0:** the Apple program (`TWOEGRAM_VERSION`, on the title
  and help screens), the manual, and the plugin package (1.0.0). The
  protocol stays version 1.
- **`make release`** makes `build/release/`: `TWOEGRAM-1.0.po` (volume
  /TWOEGRAM; TWOEGRAM.SYSTEM and A2PROBE.SYSTEM; no .CFG file, which the
  target refuses, so no test key and no address: the set-up screen asks),
  `openclaw-twoegram-1.0.0.tgz`, and `SHA256SUMS`. The disk image carries
  ProDOS 2.4.3, as every disk here has.
- **Packages need compiled JavaScript.** OpenClaw runs a plugin's
  TypeScript only from a linked development folder; from a package it
  wants `dist/index.js` beside `./index.ts` ("TypeScript source fallback
  is only supported for source checkouts and local development paths").
  `npm run build` (run by `prepack`) compiles to `dist/` with
  `rewriteRelativeImportExtensions`; the package's files are listed one
  by one, so the dev server and its pairing tool stay out. The plugin
  finds its package.json from either `src/` or `dist/src/`.
- **Checked in scratch gateways:** the package installed into a fresh
  2026.11.3, loaded from `global:a2/dist/index.js`, listening, sign-in,
  `/whoami`; switching a linked install to the package (dropping the load
  path first, then installing, is the clean order; the other way round
  works but complains); settings and paired devices survive.
- **`openclaw security audit`:** nothing about the a2 channel. A plugin
  installed from a package (unlike a linked one) appears in two general
  findings that every installed plugin raises without `plugins.allow`:
  `plugins.extensions_no_allowlist` and
  `plugins.tools_reachable_permissive_policy`. a2 registers no agent
  tools. Setting `plugins.allow` is the gateway owner's choice, since it
  must list every plugin they use (INSTALL.md step 5).
- **Manual:** Chapter 6 is now the security chapter: what is protected;
  what is not (eavesdroppers on the LAN, and anyone at the Apple, as
  there is no passphrase yet); what agents can do from the Apple (not an
  owner: no owner-only tools); what the gateway keeps; the key; and a
  checklist for whoever looks after the gateway.
- **Also:** a top-level README.md; docs/INSTALL.md leads with the package
  and keeps the linked folder as the development install (its `rsync`
  now leaves out `dist/`, which would otherwise be loaded in place of
  newer source); plugin/README.md and plugin/LICENSE go in the package.
- **For the card and your gateway** (docs/PROBE.md, "Release 1.0"):
  switch the gateway to the package, boot the release disk as a newcomer
  would, run the audit, and see a long answer arrive in parts.
- **Plugin 1.0.1** (same day, the first install on your gateway):
  - **The install message.** Installing the package while the linked
    folder was still in `plugins.load.paths` ended with `Plugin "a2" has
    no authoritative package-owner metadata`. The package was saved, but
    the folder's copy shadowed it. Dropping the load path, refreshing and
    installing again fixes it (reproduced in a scratch gateway).
    INSTALL.md now puts that step before the install.
  - **A retiring import.** `openclaw plugins doctor` said a2 "Uses
    OpenClaw APIs that retire on Oct 1, 2026": `resolveAgentWorkspaceDir`
    from `openclaw/plugin-sdk/agent-runtime`, one of the seven broad
    barrels OpenClaw means to remove. The same function comes from
    `openclaw/plugin-sdk/health`, which is not on that list, in 2026.11.3
    and 2026.11.4; 1.0.1 imports it from there.
  - **Checked:** 66 Vitest tests; types against 2026.11.3 and 2026.11.4;
    a scratch 2026.11.3 upgraded from 1.0.0 in place, with the doctor
    note gone, sign-in, and the agent's name read from IDENTITY.md.
    The Apple's disk is unchanged, rebuilt as b020.

### Release 1.0 on the card (2026-11-16, b019, plugin 1.0.1)

- **Plugin from the package:** your gateway switched from the linked
  folder to 1.0.1 smoothly (load path dropped first).
- **The release disk as a newcomer:** b019 booted, set-up asked for the
  address, the key left empty kept the card's, and the conversation
  opened. (b020, from the 1.0.1 release, differs only in its build
  number.)
- **A long answer** arrived in several messages, in order: the last
  open item from milestone 6.
- **`openclaw security audit`:** a2 listed as installed, no finding
  about it.



### Straight to the start (b021, plugin 1.1.0)

- **Protocol:** `hist` takes `after=<n>` as well as `before=<n>`: up to
  *k* messages numbered above *n*, oldest first, filled from the oldest;
  `more 1` means newer ones exist. `after=0` is the start. Both at once
  is BAD_REQUEST. Unlike `poll`, it marks nothing seen, so other agents'
  unread counts and this one's diamond are untouched. A plugin before
  1.1.0 ignores `after` and sends the newest page: Twoegram b021 then
  shows the newest as if it were the start, so the plugin goes first.
- **Apple:** `hist_load(agent, first)`; with `first` the store starts
  empty, `st_older` is 0, `st_newest` is the gateway's `more` turned
  round, and `hist_latest` is kept for the poll to count new messages
  from. Open-Apple-< fetches it only when older messages exist beyond
  those held. Open-Apple-> already fetched the newest page. Down from
  the start pages forward through `poll`, as after paging up. 105 bytes:
  1,705 bytes of heap left.
- **Tests:** two more in Vitest (68: from the start and forward, both at
  once refused, nothing counted seen); the host history test goes to
  the start in one request, down from it, and back; the screen test
  presses Open-Apple-< from the newest (one `after=0` request, message 1
  at the top), pages down, jumps back, and at the start presses it
  again with no request. The model gateway and `a2client.py hist main
  +0` know `after`. 1.1.0 ran in a scratch 2026.11.3.
- **For the card:** docs/PROBE.md, "Build b023".

### The scroll arrows (b022)

Asked for on seeing b021 (2026-11-16): `THE START OF THE CONVERSATION`
stayed in the status bar after scrolling away from the start. In its
place, a scroll bar of sorts in the 80th column, which the text area
(to column 79, counting from 1) never uses: an up arrow (MouseText $4B,
glyph byte 5, `^` without MouseText) on the pane's top row when there
is more above what is shown, whether on the screen's lines, held, or at
the gateway (`st_older`); a down arrow ($4A) on its bottom row when
there is more below (scrolled back, the layout short of the newest
held, or the gateway has newer: `!st_newest`). The heading's down arrow
and the start message are gone; Up at the start does nothing, and a
fetch that finds nothing older redraws. 49 bytes: 1,656 bytes of heap
left. $4B is from the MouseText table, not yet seen on the card. The
arrows sat right beside right-aligned text; b023 leaves a column
between.

### A narrower list, and a gap before the arrows (b023)

Asked for on seeing the b022 screenshots (2026-11-16): text touched the
arrows, and the list was wider than it needed to be. You chose the
diamond alone over a count of new messages, so the list is 18 columns
(the `>`, names in full up to 16 characters, the diamond in column 17,
counting from 0) and the divider is column 18. The text area ends at
column 77; 78 stays blank; 79 has the arrows. With the list shown the
text area is 58 columns (57 before); hidden, 77 (78 before). The
heading's note ("thinking...", "2 new elsewhere") ends at column 77 too.
Dropping the count saved more than the change cost: 1,766 bytes of heap
left. The manual: the count and the 13-character cut are gone, and the
screenshots are retaken.

### The passphrase and the lock (b028)

Started 2026-11-16 as the first half of home automation from the Apple
(section 9, items 5 and 6): an owner Apple must be lockable first.
(Item 5 turned out to need no owner, 2026-11-17, but an a2 sender can
reach a shell on the gateway all the same, so the lock still matters.)

- **On the card:** byte 15 is `$A3` instead of `$A2` when bytes 16-31
  hold the key XORed with a pad: `H("twoegram-pass" || passphrase)`,
  then `H(previous || passphrase)`, 64 hashes in all, by the card
  (`ks_scramble`). The gateway keeps the same key: no protocol change, no
  pairing again. The passphrase is never stored.
- **At the start:** a locked key asks for the passphrase (`lock_open`)
  before anything else uses the network. A wrong one makes a key whose
  device id the gateway does not know: UNKNOWN_DEVICE, said as "The
  gateway does not know the key that passphrase makes: is it right?",
  and asked again. Those refusals count toward the gateway's per-address
  limit (20 a minute, then 5 minutes), which bounds guessing at the
  gateway. Esc goes to set-up, to type a new key.
- **Open-Apple-L, or 10 minutes without a key** (`LOCK_IDLE_MIN`), with a
  passphrase only: the key and session key are wiped, the held messages
  dropped (`st_reset`; their text stays in aux memory until overwritten),
  polling stops, and the screen shows only the prompt. The passphrase is
  checked on the Apple against the device id the key made, so a wrong
  one sends nothing; the right one says hello again and reloads. The
  draft being typed is kept.
- **Set-up:** a Passphrase line (8 to 32 characters, shown as stars,
  typed twice). Empty: no change to a kept key; a new key typed without
  one is saved plain. Open-Apple-P saves the unlocked key plain again.
  A key locked and not unlocked can be kept, but not given a new
  passphrase without typing it.
- **Weakness, as designed:** with the card and a recording of the LAN
  (the device id goes in clear in every hello), passphrases can be tried
  offline; 64 hashes a guess are nothing to a PC. Length is the defence,
  and the manual says so. The gateway cannot tell whether an Apple has a
  passphrase.
- **Memory: the set-up screen became an overlay.** The passphrase work
  first overflowed main memory by 388 bytes (2K more, against 1,766 of
  heap). setup.c (code and text, 3,856 bytes) is now its own file,
  `TWOEGRAM.SETUP` (type BIN), loaded at $0800 each time set-up opens,
  over the layout table, the text buffer and the reply buffer, which are
  idle then and rebuilt after (`overlay.h`). `ses_labels` moved to main
  memory, since it must survive; `memcheck.py` now fails the build if
  the overlay reaches anything else in LOWBSS. The lock screen lives in
  the language card. Heap now: 2,991 bytes. Tried in Virtual ][: the
  overlay loads from the disk, set-up works, Esc redraws the
  conversation screen, and set-up loads again on the next Open-Apple-K.
  The disk and the release carry `TWOEGRAM.SETUP`; mkdisk.sh adds plain
  files as BIN at $0800. Upgrading by copying files now means both
  `TWOEGRAM.SYSTEM` and `TWOEGRAM.SETUP`.
- **Tests:** the key store's second mark; on the screen, Open-Apple-L
  without a passphrase; set-up with one too short, two that differ, then
  two the same (the card's own 15 bytes untouched, the stored key
  unscrambled only by the right passphrase, never shown); the lock (the
  messages and the key gone while locked, a wrong passphrase refused with
  no request, the right one signing in again); the idle lock at ten
  minutes and not at nine, and its count starting again after; a fresh
  start with a wrong then a right passphrase; Open-Apple-P. The help
  screen gains Open-Apple-L.
- **On the card, 2026-11-17:** setting a passphrase, the lock and
  unlocking passed, and the pause for 64 hashes is negligible.
  `PASS_ROUNDS` stays at 64 for good, since changing it would change
  what a passphrase makes. Open-Apple-P passed too. Not yet reported:
  the idle lock, a fresh start.

### Saving the conversation (b033)

Started 2026-11-17: section 9, item 2.

- **Open-Apple-S** asks `SAVE AS:` on the status bar, offering the
  agent's name (letters and digits, a letter first, 11 at most, then
  `.TXT`; `TWOEGRAM.TXT` if none). Up to 40 characters, so a path such
  as `/LETTERS/PINCHY.TXT` fits; typed lowercase becomes uppercase. An
  existing file is replaced only on Y.
- **The whole conversation**, not only what is held: the first page
  (`hist ... after=0`), then `hist_newer` (a `poll` from the newest
  held) until the newest, each page written before the next request,
  so the disk and the card take turns. What is written is tracked by
  the gateway's numbers, since the store drops its oldest as it fills
  (a page is at most 20 messages and 1,900 bytes, far inside the
  store). An empty conversation makes no file. Esc between pages stops
  it; a failure partway keeps the file with what came, and says how
  many. Afterwards the screen is the newest end, fetched again if it
  stopped partway. The polls mark the conversation fetched, as reading
  it would.
- **The file:** ProDOS TXT, CR line ends; "Twoegram: the conversation
  with <label>", then for each message "hh:mm You" or "hh:mm <label>",
  its text (its `\n` as CR, the check mark `$02` as `+`), and a blank
  line. No dates: the protocol carries none.
- **Not retried on $27**, unlike TWOEGRAM.CFG: that would mean fetching
  it all again. The message says to try again.
- **Memory: a second overlay**, `TWOEGRAM.SAVE` (segment OVERLAY2,
  2,684 bytes), loaded at $0800 like set-up but over the layout table
  and the text buffer only ($0800-$141B), since it needs the reply
  buffer for fetching. Its working variables load with it (a bss in
  the overlay); only `save_note`, the result, is in main memory, for
  the status bar after the screen is drawn again. `memcheck.py` checks
  each overlay against what it may cover (saving ends at $127B). Heap
  now 2,573 bytes (from 2,991).
- **A fix on the way, in `hist_load`:** it empties the store before
  asking, and when the request failed it left the empty store marked
  the newest, so neither Open-Apple-> nor a send fetched it again (a
  poll found nothing newer than it knew). It now marks it not the
  newest. Opening an agent while the gateway is down benefits too.
- **Tests:** the default names; Esc at the name; a save of the whole
  conversation (every message, the 1,500-byte one whole, CR line ends
  only, the screen at the newest after); replace declined and accepted;
  a typed name; Esc partway; the gateway failing partway; the gateway
  gone before the first page (no file, and Open-Apple-> brings the
  conversation back once it answers); a name ProDOS cannot take. In Virtual ][ (no card): the help row, "NOT CONNECTED:
  NOTHING TO SAVE", and, with a throwaway build that skipped that
  check (b030), the overlay loading from the disk, the prompt, the
  replace question and the screen drawn again after. Not seen until the
  card: a file actually written, a long conversation's time, a network
  disk.

### Hermes Agent (2026-11-17, plugin 1.0.0, Apple b036)

The investigation is [docs/HERMES.md](docs/HERMES.md); the plugin is
[hermes/](hermes/README.md). It is a second gateway-side implementation
of PROTOCOL.md, a Hermes platform plugin in Python (standard library
only), so the Apple's disk needs no change beyond words.

- **Port, not rewrite.**
  - `protocol.py`, `text.py`, `store.py` and `server.py` follow the
    TypeScript line for line.
  - `hermes/tests/vectors.json`, made by the TypeScript
    (`make hermes-vectors`), checks keys, signatures, records, folding,
    splitting and names against the same bytes. 87 vectors, including a
    sweep of 2,300 code points, matched on the first run.
  - The folding tables are generated from `text.ts`.
  - The device and conversation files have the same format as
    OpenClaw's.
- **Agents.**
  - The server asks the agent to `deliver` and returns at once. The
    answer comes back later through `post()`, and `set_busy()` says
    whether the agent is working. That is Hermes's shape: the adapter's
    `send()` is called once per finished answer.
  - The HTTP listener is asyncio streams, about 80 lines (Content-Length,
    keep-alive for 10 s, 404 outside `/a2/`), so no aiohttp is needed.
- **Hermes glue (`adapter.py`).**
  - One agent per profile, id `hermes`, name `Hermes`; both can be set.
  - Chat id `<device>.<agent>`, user id the device id, DM.
  - Busy runs from the message arriving until
    `on_processing_complete`, with a 30 s hold if Hermes never starts
    (a command answered at once).
  - `display_tier="low"`: no tool progress, streaming or interim
    messages.
  - `max_message_length=0`: we cut parts ourselves.
  - Authorization: Hermes's allowlist check reads the live adapter's
    `config.extra["allow_from"]`, refreshed with the paired devices at
    every message. So only signed, paired devices reach Hermes, and
    neither Hermes's DM pairing nor `trusted_inbound` is involved.
  - `hermes a2 pair | list | revoke` through
    `ctx.register_cli_command`.
- **Tests.**
  - 130 in all: 34 ported from server.test.ts; 9 with the adapter in
    Hermes's real base class, including the gateway's own
    `_is_user_authorized`.
  - `tools/a2client.py` against the Python dev server.
  - A scratch Hermes gateway (v0.23.0, the Mac mini's commit) on this
    Mac:
    - the plugin was discovered, enabled and listed;
    - `hermes a2 pair` and `list` worked;
    - the listener started inside `hermes gateway run`;
    - a signed hello listed `hermes (Hermes)`;
    - a message reached Hermes as session `agent:main:a2:dm:<id>.hermes`,
      and its replies came back to the poll, folded.
  - That gateway had no model, so the replies were Hermes's own notices
    (no home channel set; the provider refused). It took
    `OPENAI_API_KEY` from this Mac's environment, which OpenAI rejected;
    later runs keep provider keys out of the environment.
- **On the Apple** (b036): the splash says "Talk to your OpenClaw or
  Hermes agents", set-up's help names both pair commands, and b034's
  bad-key line says "Pair this Apple again on the gateway". Screenshots
  retaken.
- **Hermes updated** (same day): the Mac mini went from v0.23.0 to
  v0.23.2, the newest stable release, with `hermes update`. Its model,
  settings and gateway carried over. The local clone followed
  (`HERMES_COMMIT`). Every Hermes call the plugin makes is unchanged.
  The allowlist check was restructured but still reads a DM's
  `allow_from`. All 130 tests pass again. A scratch v0.23.2 gateway,
  started with no provider keys in its environment, passed the same
  hello and message run, and contacted no provider.
- **Installed on the Mac mini** (same day, after commit 70cd427):
  - The plugin was copied to `~/.hermes/plugins/twoegram` and enabled.
    `hermes config set gateway.platforms.a2.enabled true`, which Hermes
    saved as top-level `platforms.a2`. The old config is in
    `config.yaml.bak-before-twoegram`.
  - `hermes gateway restart` brought up the listener on 8417. That
    command runs the gateway in the foreground of the shell that ran it,
    though it survived the SSH session ending.
  - Making the gateway a launchd service
    (`hermes gateway install --start-now --start-on-login`) was refused
    by this session's permission rules, so that is left to you. Until
    then, nothing starts the gateway after a reboot.
  - A temporary device paired, signed in from the Mac Studio over the
    LAN, saw `hermes (Hermes)` with an empty conversation, and was
    revoked.
- **Not yet done:**
  - the real card against it;
  - a real model's answers and an approval typed from the Apple.

### Several agents (planned 2026-11-17)

Asked for after the Hermes install: more than one agent at a time, at
different addresses with different keys. The card holds one key, so the
plan is [docs/RELAY.md](docs/RELAY.md), in two parts. Both keep the
direct mode and leave the protocol unchanged.

- **Keys made on the Apple** (K1 to K3):
  - The card holds a master M (marks `$A4`/`$A5`).
  - Each gateway's key is HMAC(M, "twoegram-gw" || name), cut to 16
    bytes. Set-up shows it, and `a2 add` on the gateway takes it at a
    prompt that does not echo.
  - TWOEGRAM.CFG holds the gateway's key name with its address, so
    switching is typing a name and address, not a key. (A list of 4 was
    planned, and dropped in K2 for room.)
  - True public-key auth stays out: a minute or more of 6502 time for
    each operation. X25519 pairing is noted under "Later".
- **The relay** (R1 to R4):
  - `twoegram-relay`, Python standard library, on its own Proxmox LXC.
  - Facing the Apple, it is an a2 server. Facing each gateway, it is a
    paired a2 client.
  - It passes send, poll and hist through to the agent's upstream,
    keeping the upstream's message numbers and folded text, and keeps no
    conversations of its own.
  - It merges the agent lists, and polls quietly for the other
    upstreams' unread counts.
  - It shares the session code of `hermes/twoegram/server.py`, which is
    split first.
- **Decided 2026-11-18:** key names typed in set-up; Ubuntu LTS on
  the LXC; a suffix for clashing agent ids; and set-up no longer takes a typed
  key, to save room (RELAY.md, "Decisions").

### K1: keys derived from a master (2026-11-18)

The servers' half of RELAY.md part 1, and the Apple's derivation. The
Apple's set-up does not use it yet (K2).

- **`deriveKey` / `derive_key` / `ks_derive`** in TypeScript, Python and
  C: the first 16 bytes of HMAC(M, "twoegram-gw" || name), names
  1 to 16 of `a-z 0-9 - _`. `hermes/tests/vectors.json` gains 15 derived
  keys made by the TypeScript, which the Python, `tools/a2client.py`
  (written separately) and the Apple's host tests all match.
- **`a2 add [name]`** in the OpenClaw plugin, the Hermes plugin, and both
  dev servers. The key is read at a prompt that does not echo (from
  stdin when piped), never from an argument. A key that does not check,
  or is paired already, adds nothing. The device id is printed so it can
  be compared with the Apple's screen. What it adds is exactly what
  `pair` makes, so `list` and `revoke` are unchanged.
- **`tools/a2client.py --master <hex> --name <g>`** acts as an Apple
  with a master. Its `key` command prints the derived key, for piping
  into `a2 add`.
- **On the Apple:** `ks_derive` is in the language card beside the lock.
  It uses the prefs buffer, which is idle then, for the HMAC, and wipes
  it afterwards. In main memory it would have cost 178 bytes of heap.
  There it costs 21: heap 2,441 bytes, from 2,462. Nothing calls it yet.
- **Versions unchanged.** The OpenClaw plugin stays 1.1.0 and the Hermes
  plugin 1.0.0 until K2, since nothing can use `add` before the Apple
  makes keys. They go out together as 1.2.0 and 1.1.0.
- **Tests:** 75 TypeScript (a new `commands.test.ts`: add, a mistyped key,
  a key paired twice, list and revoke after add; the derivation against
  an independent HMAC), 147 Python (the vectors, names refused, `hermes
  a2 add` in Hermes's own CLI registration), and host tests all passing
  (a2: 1,131 checks).
- **A round trip with no Apple.** A random test master made a `claw1` key
  and a `mini` key. Each was piped into `add` at one dev server: the
  TypeScript one and the Python one, started with no provider keys. Each
  key then signed in at its own server, and only there: the `mini` key
  at the TypeScript server got UNKNOWN_DEVICE. A message went through
  and came back.

### K2: the Apple makes its key (2026-11-18, b037, plugins 1.2.0 and 1.1.0)

The Apple's half of RELAY.md part 1. Not yet on the card (K3: PROBE.md,
"Build b037").

- **The card.** Marks `$A4` (a master) and `$A5` (a master under a
  passphrase) join `$A2`/`$A3`. `ks_load` sets `ks_master`. The secret
  in memory is `ks_secret`, with `ks_have`, kept apart from `ses_key`,
  the key in use. `lock_use()`, in the language card, makes `ses_key`
  from it: the secret itself for a gateway's key, else
  `ks_derive(M, cfg_name)`. The lock unscrambles into `ks_secret`, and
  quitting and locking wipe it.
- **TWOEGRAM.CFG:** `GATEWAY=<key name> http://...`. The old form still
  reads, with no name. `cfg_save` moved into the overlay, its only
  user.
- **Set-up:** Key name, Address and Passphrase lines; Open-Apple-M
  makes the master (two RANDOM draws and the tick count, hashed by the
  card), Open-Apple-V shows the gateway's key and device id until a key
  is pressed, and typing a key is gone (RELAY.md, decision 4). With a
  master and no key name, start-up goes to set-up first.
- **Wording:** the bad-key line is now "Add this Apple's key on the
  gateway again: Open-Apple-K, Open-Apple-V." The set-up prompts in
  link.c name Open-Apple-V and -M.
- **The gateway list was dropped.** With a list of 4 and a screen to
  pick from it, the overlay was 9K against its 6K. Without it, 5,615
  bytes.
- **A crash, found in Virtual ][.** Set-up's variables moved into the
  overlay with `#pragma bss-name ("OVERLAY1")`, the same name as its
  code. cc65 then emits a function's static locals (`-Cl`) at the
  function's own label, so `JSR _setup_main` ran into a zero byte, a
  BRK. The variables now have their own segment, `OVL1DATA`, in the
  same file (cfg/twoegram.cfg). The loader reads both, and memcheck.py
  checks both.
- **Memory:** heap 2,685 bytes, up from 2,462. Set-up's 252 bytes of
  variables and `cfg_save` left main memory.
- **The plugins:** OpenClaw 1.2.0 and Hermes 1.1.0, with K1's `a2 add`.
  `src/secret.ts` was missing from the package's `files`, and OpenClaw
  loads the TypeScript, so 1.2.0 would not have loaded. It's added, and
  every local import in the packed tarball was checked.
- **Tests:**
  - Host: 1,146 checks in a2 (the new marks, key names in the config
    file) and 1,477 in convo. The new convo test covers:
    - a fresh card: Return refused, Open-Apple-M, no key name refused,
      Open-Apple-V, with the model gateway taking the key off the screen
      as `a2 add` would, then Return and connected;
    - moving to `mini` (refused, since that gateway has not been given
      that key) and back to `claw1` with nothing typed but the name;
    - a passphrase on the master, the lock, and a fresh start;
    - a second Open-Apple-M declined.
  - Virtual ][ (no card): set-up opens and draws, key names are
    lowercased, and Open-Apple-M and -V say there is no card or no key.
    Keys pasted quickly are dropped, since the //e holds one key at a
    time; shots.sh now types one at a time. Screenshots retaken.

