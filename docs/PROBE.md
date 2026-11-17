# Testing on the card

## Build b028: the passphrase and the lock

Plugin 1.1.0 as for b023 (no plugin change). Boot `build/TWOEGRAM-b028.po`.
It also carries everything in the b023 list below, so one disk covers
both. The disk now has `TWOEGRAM.SETUP`, the set-up screen, read from the
disk when it opens.

1. **Set-up still works** (it is now loaded from the disk): Open-Apple-K
   opens it at once; Esc returns to the conversation, drawn whole; a
   second Open-Apple-K opens it again.
2. **Give the key a passphrase.** Open-Apple-K, Tab to Passphrase, type
   one (8 or more characters; stars appear), Return; type it again,
   Return. **Time the pause after the second Return** (the card hashes
   64 times): please say how long.
3. **Open-Apple-L.** The screen clears to the prompt. A wrong passphrase:
   "That is not the passphrase.", at once. The right one: it signs in
   again and the conversation comes back.
4. **The idle lock.** Leave it 10 minutes untouched: it locks by itself.
5. **A fresh start.** Quit and start Twoegram: it asks for the
   passphrase first. Type it wrong once: the gateway does not know the
   key it makes, and it asks again. Then right.
6. **Open-Apple-P** on the set-up screen, Y: the next start asks for no
   passphrase. (Give it one again afterwards if you want to keep it.)

## Build b023: straight to the start, the scroll arrows, a narrower list

Needs plugin 1.1.0 on the gateway (`build/openclaw-twoegram-1.1.0.tgz`):
copy it over and install it as docs/INSTALL.md step 2 says (gateway
stopped, `plugins install ... --force --accept-capabilities`, start).
`openclaw plugins list` shows `a2` at 1.1.0. Then boot
`build/TWOEGRAM-b023.po`.

1. **The start, in one go.** In Pinchy's conversation (a long one is
   best), press Open-Apple-<. The status bar says `FETCHING MESSAGES` for
   a moment and the conversation's first message is at the top: no
   paging back. There is no up arrow in the 80th column at the pane's
   top, and Up arrow does nothing and says nothing.
2. **Down from the start.** Down arrow and Open-Apple-Down read on,
   fetching newer messages as they go (`FETCHING NEWER MESSAGES`). One
   line down, an up arrow appears at the pane's top right (row 3,
   column 80); a down arrow sits at its bottom right, just above the
   compose line, all the while there is more below.
3. **Back to the end.** Open-Apple-> brings the newest back at the foot.
   The down arrow goes; the up arrow stays. The heading no longer has
   an arrow of its own. Hide the list (Open-Apple-B): the arrows stay
   in column 80.
4. **Nothing lost on the way.** While at the start, ask Pinchy something
   from another channel (Telegram), or wait for an answer that is still
   coming: the diamond appears beside its name; Open-Apple-> shows it.
5. **Sending from the start** goes to the newest end first, as before.
6. **The narrower list.** The agent list is 18 columns: the `>`, names
   in full up to 16 characters, and a diamond in the last column when an
   agent has something new (no count any more). Rename an agent to a
   16-character name in OpenClaw and press Open-Apple-R to see it whole.
7. **The gap.** Text, your right-aligned messages and their times
   included, stops one column short of the arrows, with list shown and
   hidden.

## Release 1.0: milestone 8

`make release` has made `build/release/`: `TWOEGRAM-1.0.po`, the plugin
package `openclaw-twoegram-1.0.1.tgz`, and `SHA256SUMS`.

1. **The plugin from the package.** Your gateway runs the plugin from a
   linked folder today. Switch it to the package as docs/INSTALL.md
   says ("Coming from a linked folder", then steps 1 to 3): with the
   gateway stopped, drop the folder's load path, refresh, install the
   package, start. `openclaw plugins list` shows `a2` at version 1.0.1,
   from `dist/index.js`, with no warning; the log has the `[a2]
   listening` line.
2. **The release disk, as a newcomer sees it.** Boot `TWOEGRAM-1.0.po`.
   The title says 1.0. With no address on the disk, the set-up screen
   comes first: type the gateway's address, leave the key empty (the
   card has it), Return. Pinchy's conversation opens, as before.
3. **The audit.** On the gateway host:

   ```bash
   openclaw security audit
   ```

   Nothing about the a2 channel is expected. After step 1, two general
   findings name the plugin (`plugins.extensions_no_allowlist` and
   `plugins.tools_reachable_permissive_policy`); docs/INSTALL.md step 5
   says what they mean. Please paste the output.
4. **Still open from milestone 6:** ask for a long answer ("explain the
   6502's addressing modes in detail") and see it arrive as several
   messages, in order.

## Build b017: milestone 7, when things go wrong, and agents by name

First update the plugin on the gateway (its `rsync` from INSTALL.md
step 1, then `openclaw gateway restart`), so it sends the names.

0. **Names.** Boot `build/TWOEGRAM-b017.po`: the list, the heading and
   the status bar say `Pinchy` (or whatever `openclaw agents list` shows
   as the agent's identity), not `main`, and messages still reach it. An
   agent with no name of its own still shows its id.

Then, on your gateway with a conversation open:
The commands are on the gateway host. Steps 1 to 4 matter most; 5 needs
re-pairing, so it is optional.

1. **The gateway stops and comes back.** `openclaw gateway stop`. Within
   five seconds the status bar's middle says `GATEWAY NOT ANSWERING`,
   and the conversation stays on the screen. Note roughly how long the
   Apple is unresponsive to keys while each try waits (type-ahead should
   still catch what you type). Leave it a minute or two, then
   `openclaw gateway start`: within about a minute of the gateway being
   up, the bar says `SIGNED, NOT ENCRYPTED` again by itself, and a new
   message sends and is answered.
2. **Sending while it's down.** Stop the gateway again and send
   something: the reason takes over the status bar (`CONNECTION
   REFUSED` or `TIMED OUT`) and your text stays on the compose line.
   Start the gateway; once the bar is back to normal, Return sends it.
3. **Starting while it's down.** Quit Twoegram, stop the gateway, start
   Twoegram: it says `...: TRYING AGAIN`. Start the gateway: Twoegram
   signs in and opens the conversation by itself.
4. **A restart in the middle.** `openclaw gateway restart` while a
   conversation is open: at most a moment of `GATEWAY NOT ANSWERING`,
   then on as before; nothing to press.
5. **Revoked** (optional: you'll need to pair again, and revoking also
   deletes the Apple's conversations on the gateway).
   `openclaw a2 revoke <id>`: at the next poll Twoegram says `THE
   GATEWAY DOES NOT KNOW THIS KEY` and stops trying. Pair again
   (`openclaw a2 pair "Apple //e"`), Open-Apple-K, type the new key.

A photo of anything unexpected, and a note of how long step 1's tries
freeze the keyboard, is plenty.

## Build b010: the new marks

Against the dev server (or your gateway, once milestone 6 is
installed), boot `build/TWOEGRAM-b010.po` and look for:

1. **The check mark.** Send a message: the echo agent's answer ends with
   a check mark (it sends a thumbs-up). If it shows as some other
   symbol, the MouseText code is wrong; a photo says which.
2. **The down arrow.** Press UP ARROW a few times: a down arrow appears
   at the top right, and goes when you come back to the bottom.
3. **The diamond for news below.** While scrolled back, send a message
   from the Mac as in step 5 of milestone 5 below: a diamond appears
   beside `main` at the top and in the list, and goes when you scroll
   down to it.
4. **Another agent's news.** Open `coder`, send it something, then
   switch back to `main` before the answer comes: within five seconds
   `coder` gets a diamond and a 1 in the list. Open-Apple-N goes to it.
5. **The help box** (Open-Apple-?) explains the diamond and the arrow
   on its last lines.

## Milestone 5: sending and replies

The dev server's echo agent answers every message after 0.8 s, and a
message containing the word "long" gets a long answer in several parts.
Start the server as for milestone 4 (`--seed 100` is optional now):

```bash
npm run dev -- --host 0.0.0.0 --seed 100
```

1. Boot the newest `TWOEGRAM-bNNN.po`.
2. Type a message and press Return. It appears at the right, the compose
   line empties, and the heading says `thinking...`. Within a second or
   two the answer appears at the left, and `thinking...` goes.
3. Send one with "long" in it: the answer arrives as several messages
   (parts of at most 1,500 characters), with a code block at the end.
4. **Type-ahead:** start typing the next message straight after pressing
   Return, while it is sending; no key should be lost.
5. **New messages while scrolled back:** scroll up a little, then use the
   Mac to send a message as the Apple (the test key is in `probe.cfg`):

   ```bash
   A2_KEY=$(sed -n 's/^KEY=//p' probe.cfg) python3 tools/a2client.py --url http://127.0.0.1:8417 send main "Hello from the Mac"
   ```

   Within five seconds the heading should say `new below` without the
   screen moving; scrolling down shows the message and the echo's answer.
6. **Sending from far back:** Open-Apple-< to the oldest held, then send:
   it should jump to the newest end and add the message there.

Worth a photo: a sent message with its answer, a long answer's parts,
and `new below`. Worth noticing: any lost keystroke, and how long an
answer takes to appear.

## Milestone 4: messages and scrolling

The dev server can start every conversation with made-up history, so
there is something to scroll through before sending exists. Restart it
with a seed:

```bash
npm run dev -- --host 0.0.0.0 --seed 100
```

Each agent's conversation then has 100 messages: short questions, short
and long answers, and a code block every tenth; each begins with its
number (`Question 37 ...`, `Answer 40 ...`).

1. Boot the newest `TWOEGRAM-bNNN.po`. It signs in with no questions
   (`make disk` now puts the gateway's address on the disk too).
2. The conversation with `main` shows its newest messages, 100 at the
   foot.
3. **Up and Down** scroll a line; **Open-Apple-Up and -Down** a page.
   Keep going up: older messages are fetched as you reach the top
   (`FETCHING OLDER MESSAGES` on the status bar), and the line you were on
   stays where it was. At message 1 it says `THE START OF THE
   CONVERSATION`. Down goes back, fetching newer ones when needed.
4. **Open-Apple-<** jumps to the oldest message held, **Open-Apple->** to
   the newest. **Tab**, an arrow and Return open `coder`, whose history
   loads the same way.

Worth a photo: the newest messages, somewhere in the middle, the start,
and any status message in inverse. Worth noticing: how long a fetch
takes, and whether a scroll ever jumps or repeats a line.

## Milestone 3: Twoegram itself

From milestone 3, Twoegram signs in to the gateway itself. With the dev
server running as in step 1 below:

1. Boot the newest `TWOEGRAM-bNNN.po` and press a key at the splash.
2. **The set-up screen** appears, since the disk has no `TWOEGRAM.CFG`
   and the card holds no key yet. Type the gateway (`192.168.1.10`; the
   port 8417 is added), press Tab, and type the key from `npm run pair`
   (the same test key as the probe's is fine: it is in `probe.cfg`).
   Return saves the address to `TWOEGRAM.CFG` and the key to the card's
   spare settings bytes, then says hello.
3. **Signed in:** the agent list shows the dev server's agents (`main`,
   `coder`), the pane says "Signed in as device ...", and the status bar
   says `SIGNED, NOT ENCRYPTED`. Messages come in milestones 4 and 5.
4. **Quit and start again** (Open-Apple-Q, then `TWOEGRAM.SYSTEM` in the
   selector). It should sign in with no questions: the address from the
   file, the key from the card.
5. Open-Apple-K reopens set-up; there, Open-Apple-X removes the key from
   the card (it asks first). Open-Apple-R says hello again.

Worth a photo: the set-up screen, the signed-in screen, and any message
in inverse on the status bar. Things to try: a key with one character
wrong (caught before sending), stopping the dev server and pressing
Open-Apple-R (an error, not a hang), and a key the server does not know
(the set-up screen comes back saying so).

## Milestone 2: the probe

`A2PROBE.SYSTEM` checks the Twoegram protocol from end to end on the real
card, against the plugin's dev server on the Mac, and reports on one
screen. Nothing is sent to OpenClaw: the dev server's echo agent answers.

## 1. On the Mac: a test key and the server

```bash
cd plugin && npm install
```

```bash
npm run pair -- probe
```

This prints a device id and a key such as `VA6DR-ZTWSS-VGA5A-Y3BCF-CTCYV-8=`.

```bash
npm run dev -- --host 0.0.0.0
```

The server listens on port 8417 on every interface, so the Apple can
reach it. macOS may ask whether `node` may accept incoming connections:
allow it. The Mac's LAN address:

```bash
ipconfig getifaddr en0
```

Leave the server running; it logs every hello, and every TCP connection
opened and closed. Its first line names its version and git commit (a `+`
means files changed since that commit); the probe shows the same on its
"Server is" line. **Restart it whenever its code has changed**, or the
probe talks to the old code.

## 2. The disk, with the address and key on it

From the repository's top level, with your address and key:

```bash
make disk URL=http://192.168.1.20:8417 KEY=VA6DR-ZTWSS-VGA5A-Y3BCF-CTCYV-8=
```

This makes `build/TWOEGRAM-b<build>.po`, for example `TWOEGRAM-b001.po`.
Every `make disk` takes the next build number (from the file `BUILDNUM`),
so every disk has its own file name and the card can never serve an older
image's cached blocks under it. The number is also the volume name
(`/TWOEGRAM.B001`), on Twoegram's splash and help, and in the probe's
heading and last line, so every photo shows which build it was.

The disk carries `A2PROBE.CFG`, a text file holding the address and key.
`make disk` also keeps them in `probe.cfg` at the top of the repository
(gitignored, readable only by you), so later disks carry them without
`URL=` and `KEY=`. **This is a test key for the dev server only.** The real
client will never keep a key on a disk, since a network disk is an image
on a web server. Revoke it when you are done (step 5).

## 3. On the Apple

Copy the numbered disk to your share under its own name and mount it the
way you mounted Newsiie's image, then boot it. Twoegram starts; quit it with Open-Apple-Q, and pick
`A2PROBE.SYSTEM` in the ProDOS selector.

The probe takes about 30 seconds. Near the end it asks about the prefs
test: **W** writes a pattern into the card's spare settings bytes
(15-31), reads it back, and puts back exactly what was there. Any other
key skips it. The card's own settings (bytes 0-14: timezone, clock sync,
TLS, equaliser) are written back unchanged either way. If the card has
no settings stored yet, the probe does not offer the write.

## 4. What it reports

| Line | Good result | What it settles |
|---|---|---|
| Card | slot, firmware | |
| Network | up, address, sockets open | |
| Server | the address from the file, and "key check" | the 6502 read the key as the Mac does (the Mac's value: see below) |
| Hello | session, device id, agents, frames | the card's `HTTP` command; the server proved it holds the key |
| Server is | the dev server's version and commit | which server code answered |
| Send | signed reply, message number | a POST with a body and extra header lines |
| Answer | the echo agent's words | a long poll of up to 4 s |
| 20 polls | all genuine, seconds by the card's clock | the cost of one round trip |
| Idle 6 s / Idle 10 s | the seconds waited, and the connection: "(the same)" after 6 s, "(a NEW one)" after 10 s if keep-alive works as the reference describes | keep-alive reuse; the waits ignore the keyboard |
| Replay | refused, code 5, signed | |
| Forgery | refused, code 3, unsigned | |
| HMAC | 20 x 1,900 bytes, frames | how fast the card signs a full reply |
| Prefs | bytes stored; whether 15-31 are free | where the real key can live |
| Prefs W | written, read back, restored (or skipped) | |
| Disk | block 2 read after all the requests | the card's disk and HTTP traffic coexist |

Frames are 1/60 s, counted while waiting, so they are a lower bound.

**Please send** a photo of the screen and the dev server's log from the
run. The photo carries the probe's build number and the server's commit,
so nothing else needs noting.

## 5. Afterwards

Revoke the test key (the device id was printed in step 1; `npm run
devices` lists it again):

```bash
npm run devices
```

```bash
node src/cli.ts revoke <device id>
```

## If something fails

| Message | Likely cause |
|---|---|
| no A2PROBE.CFG on the disk | `make disk` was run without `URL=` and `KEY=` |
| KEY does not check | the key was copied wrong |
| Hello: no answer from the server: CONNECTION REFUSED | the server is not on `0.0.0.0`, or the Mac's firewall refused |
| Hello: DNS FAILED | a host name the card cannot resolve; use the address |
| Hello: not a Twoegram server | something else answered on that port |
| Hello: refused, code 2 UNKNOWN_DEVICE, device ... | the key was not paired with this server's device store: the server's first line lists the device ids it knows. Builds before b001 sent a wrong id (a compiler bug, now fixed) |
| Send: refused, code 4 NO_SESSION or 3 BAD_SIGNATURE | the card's handling of extra header lines differs from what the probe assumes (CR LF between lines, none after the last): worth a photo |
