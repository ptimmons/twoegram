# Running the milestone 2 probe

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
