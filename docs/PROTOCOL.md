# The Twoegram protocol, version 1

How the Apple talks to the `a2` channel plugin. Plain HTTP on the LAN;
every request and every reply is signed with HMAC-SHA256. Nothing is
encrypted (PLAN.md section 2).

Shaped by what the Apple can do cheaply:

- The CoGS card computes SHA-256 (`HASH`, at most 2,045 bytes a call).
  HMAC is two such calls; the Apple pads and XORs the key itself.
- Replies are read whole into a 2K buffer, hashed, then parsed. So a
  reply is a header line and simple length-prefixed records, never JSON,
  and its payload is at most **1,900 bytes**.
- The Apple has one request outstanding at a time.

## 1. The device key

16 random bytes, made on the gateway host by `openclaw a2 pair` (the
stand-alone server: `npm run pair`), shown once and typed into the
Apple.

**Written form:** 26 characters of Crockford base 32 (alphabet
`0123456789ABCDEFGHJKMNPQRSTVWXYZ`), most significant bit first, the
last character carrying 3 bits and 2 zero bits, then one **check
character**: the sum over the 26 values of (position + 1) x value,
modulo 37, written with Crockford's check symbols (the alphabet, then
`* ~ $ = U` for 32 to 36). 37 is a prime above every weight and every
difference between two values, so any one wrong character, and any two
neighbours swapped, is caught before anything is sent. Shown in groups
of five with hyphens:

    7M2QK-9XTRB-H04GD-ZC8WE-NP5VA-3$

On entry, hyphens and spaces are ignored, lowercase is uppercase, `O` is
`0`, and `I` and `L` are `1` (`U` appears only as a check character).

**A key made by the Apple** (docs/RELAY.md, part 1). The card may
instead hold a master secret M, 16 random bytes that never leave it.
The Apple then derives a key for each gateway from the gateway's **key
name** *g*, 1 to 16 of `a-z 0-9 - _`, typed in set-up:

    K(g) = the first 16 bytes of HMAC-SHA256(M, "twoegram-gw" || g)

The Apple shows K(g) in the written form above, and `openclaw a2 add`
(or `hermes a2 add`) takes it at a prompt that does not echo. To a
gateway it is an ordinary device key: nothing below changes, and a
gateway cannot tell how a key was made.

**Device id:** the first 4 bytes of SHA-256(`"twoegram-id"` || key), as
8 lowercase hex digits. Both sides derive it; nobody types it.

## 2. Signatures

`HMAC(k, m)` is HMAC-SHA256. `u32(n)` is n as 4 bytes, big-endian.
Signatures travel as 64 lowercase hex digits and are compared in
constant time.

| Name | Key | Message |
|---|---|---|
| hello proof (Apple to plugin) | device key | `"hello"` \|\| client nonce (16 bytes) |
| session key | device key | `"session"` \|\| client nonce \|\| server nonce |
| hello reply | session key | `"h"` \|\| payload |
| request | session key | `"q"` \|\| u32(seq) \|\| method \|\| `" "` \|\| target \|\| `"\n"` \|\| body |
| reply | session key | `"r"` \|\| u32(seq) \|\| payload |

*target* is the request line's path and query exactly as sent, such as
`/a2/poll?a=main&after=12&w=4000`. *payload* is every byte of the reply
after its first line.

The session key is 32 bytes; as an HMAC key it is used as it is.

## 3. Requests

Every request carries, as header lines:

    X-A2-Session: 3f9c01ab
    X-A2-Seq: 17
    X-A2-Mac: <64 hex digits>

except `hello`, which starts a session. `seq` starts at 1 in each
session and must rise with every request; the plugin refuses one not
higher than the last it accepted.

| Request | Body | Payload of a good reply |
|---|---|---|
| `GET /a2/hello?d=<id>&c=<client nonce, 32 hex>&m=<hello proof>` | | `sid`, `sn`, `ver`, `time`, one `agent` line each, then a `name` line for each agent that has a name |
| `POST /a2/send?a=<agent>` | the message, 1 to 1,024 bytes of printable ASCII and `\n` | one `msg` record: the message as stored |
| `GET /a2/poll?a=<agent>&after=<n>&w=<ms>` | | `busy`, `more`, `unread` lines, then `msg` records numbered above *n* |
| `GET /a2/hist?a=<agent>&before=<n>&k=<count>` | | `more`, then up to *k* `msg` records numbered below *n*, oldest first |
| `GET /a2/hist?a=<agent>&after=<n>&k=<count>` | | `more`, then up to *k* `msg` records numbered above *n*, oldest first; `after=0` is the conversation's start (plugin 1.1.0 and later) |

`poll` waits up to *w* milliseconds (at most 4,000, under the card's 8 s
keep-alive cutoff) for something to report, and answers at once if
there is something already. `poll` also tells the plugin that the Apple
has every message up to *after*, which is what clears unread counts.

`hist` takes `before` or `after`, not both (BAD_REQUEST). With neither,
it is the newest page. With `before`, `more 1` means there are older
messages; with `after`, newer ones. Unlike `poll`, `hist` never counts
anything as seen. A plugin older than 1.1.0 ignores `after` and sends the
newest page.

A request line longer than 512 bytes, or a body longer than 1,024, is
refused.

## 4. Replies

Always HTTP 200 with `Content-Type: text/plain`, so the card hands the
body over whatever happened. The first line is

    A2 1 <code> <signature, or ->

then the payload. A reply signed with the session key can be trusted
whatever its code. One with `-` is not signed: the plugin could not yet
tell that the request was genuine, and it never signs a reply to one it
has not verified (else anyone could collect signed refusals for future
sequence numbers). The Apple may use an unsigned code to decide what to
do next, nothing else, and must not show its `why` as fact.

| Code | Name | Signed | The Apple should |
|---|---|---|---|
| 0 | OK | yes | |
| 1 | BAD_REQUEST | when the request was verified | report it |
| 2 | UNKNOWN_DEVICE | no | say the gateway does not know the key |
| 3 | BAD_SIGNATURE | no | say so; never retry automatically |
| 4 | NO_SESSION | no | say hello again |
| 5 | REPLAY | yes | say hello again |
| 6 | LOCKED | no | wait; say so |
| 7 | UNKNOWN_AGENT | yes | refresh the agent list |
| 8 | TOO_LARGE | no | report it |
| 9 | BUSY | yes | try again shortly |

The payload always begins with a `more` line when it carries messages.

An error's payload may hold a line `why <text>` for diagnostics.

### Payload lines

Each line is a word, a space, and its value, ended by `\n`:

| Line | Meaning |
|---|---|
| `sid 3f9c01ab` | the session's id |
| `sn <32 hex>` | the server nonce |
| `ver 1` | protocol version |
| `time 1763215200` | the gateway's clock, Unix seconds |
| `agent main` | an agent the Apple may talk to; its id, which requests use: 1 to 16 of `a-z 0-9 - _` |
| `name main Pinchy` | what an agent listed above calls itself (OpenClaw's identity name), for showing only: 1 to 16 printable ASCII characters, spaces allowed, emoji removed; sent only when it differs from the id. A client that does not know the line ignores it |
| `busy 1` | the agent is working on an answer |
| `more 1` | there were more messages than fit; ask again |
| `unread coder 2` | messages the Apple has not fetched, for another agent |
| `why bad seq` | an error's reason |
| `conn 3` | dev server only, in every reply: the number of the TCP connection the request came on, so a probe can see whether the card reused one |
| `server twoegram-dev 1.0.0 569147e` | in every `hello` reply, refusals too: which server and build answered, for diagnostics only (in an unsigned reply it proves nothing) |

### Message records

    msg <n> <mine> <hh:mm> <len>
    <len bytes of text>

then `\n`. *n* numbers the messages of one device's conversation with
one agent, from 1, rising. *mine* is 1 for the Apple's own, 0 for the
agent's. The time is the gateway's local time. The text is printable
ASCII, `\n`, and the glyph byte `$02` (section 5), at most 1,500 bytes: the plugin folds Unicode and
Markdown to that (section 5) and cuts a longer answer into parts, each
its own message. Every message fits a reply, so a reply always carries
at least one when one is waiting.

## 5. Text

The plugin turns the agent's text into what the //e can draw: Markdown
emphasis, headings and links to plain text (`[text](url)` becomes
`text (url)`), code blocks kept line for line without their fences,
curly quotes, dashes, ellipses and accented letters to their ASCII
neighbours, thumbs-up and check marks to the byte `$02`, other common
emoji to a word in colons (`:thumbs-down:`), other emoji to `*` (they
are mostly line markers), anything else to `?`, tabs to spaces, and CR
LF to LF.

`$02` is the one glyph byte: the Apple draws it as MouseText's check
mark (a `+` on a //e without MouseText). Only the gateway sends it; text
from the Apple is printable ASCII and `\n` only.

## 6. Limits and lockouts

| | |
|---|---|
| Sessions | at most 4 a device; the least recently used goes; 30 minutes idle ends one |
| Bad signatures | 5 for one device in 5 minutes locks it for 15 minutes |
| From one address | 20 refusals a minute blocks that address for 5 minutes |
| Conversation log | the plugin keeps the newest 500 messages per device and agent |

## 7. What it protects, and what not

A device that does not hold the key cannot start a usable session,
send, read, or forge or replay a reply. Anyone who can capture LAN
traffic can **read** everything. A captured `hello` can be replayed to
make sessions nobody can use, which can push out a real one (the Apple
then says hello again). See PLAN.md section 2.
