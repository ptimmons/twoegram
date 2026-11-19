# Several agents: keys made on the Apple, and the relay

**Status:** planned 2026-11-17. Built 2026-11-18: K1, K2 (b037), R1
and R2 (`relay/`, 0.1.0). K3 passed on the card 2026-11-18 (b046).
Built 2026-11-18: K4, adding a gateway and Settings (b048). Waiting: K4
on the card, R3 (your LXC) and R4 (the card through the relay); see
Milestones.
PLAN.md, "Several agents", tracks it.

Twoegram holds one gateway and one key at a time. To talk to agents on
several gateways (Pinchy on claw1, Hermes on the Mac mini, and others
later), this plan builds two things, in this order:

1. **Keys made on the Apple.** The card holds one master secret. From
   it, the Apple derives a different key for each gateway, and you type
   that key into the gateway. Moving between gateways then costs no
   retyping and no pairing again.
2. **The relay.** A small service on its own Proxmox LXC. The Apple
   pairs with it like any gateway, and it lists every agent on every
   gateway behind it.

Both keep the current direct mode exactly as it is. The protocol
([PROTOCOL.md](PROTOCOL.md)) does not change for either. The relay is
just another gateway as far as the Apple can tell, so the same disk
works direct, through the relay, or both.

## Part 1. Keys made on the Apple

### Why, and what it does not buy

The keys stay symmetric (HMAC), so a gateway still holds a copy of the
key it checks. Making the key on the Apple does not change that. Only
public-key signatures would, and on the 6502 they would cost minutes for
each one (section "Later", below). What the master secret does buy:

- **One card, many gateways.** Each gateway gets its own key. Switching
  gateways in set-up changes only which key is derived, so you never
  retype a key. Today, switching from claw1 to the mini replaces the key,
  and going back means pairing again.
- **Gateways are kept apart.** A gateway that is broken into gives up its
  own key, never the master, and never another gateway's key. Revoking
  at one gateway leaves the others alone.
- **The key is born on the Apple.** It is shown on the Apple's screen,
  and typed at the gateway into a prompt that does not echo. It is never
  in an SSH session's scrollback or a shell's history.
- **The card still holds 16 bytes**, in the same `PREFS` slot.

### The derivation

    M      16 bytes from the card's RANDOM, made once in set-up
    K(g)   the first 16 bytes of HMAC-SHA256(M, "twoegram-gw" || g)

*g* is the gateway's **key name**: 1 to 16 of `a-z 0-9 - _`, chosen in
set-up (for example `claw1`, `mini`, `relay`). Each gateway's key is
then an ordinary device key, written in the usual 27-character form,
with its own device id, SHA-256(`"twoegram-id"` || K(g)). The name is
part of the key: renaming a gateway in set-up makes a new key, which
has to be added again, and set-up says so before doing it.

M comes from two 16-byte draws from `RANDOM`, hashed together with the
Apple's tick count on the card. That is cheap insurance if one draw
were ever poor.

**M is never shown, sent, or written anywhere but the card.** There is
no way to back it up. If the card is lost or replaced, you make a new
master and add its keys at each gateway again, which is no worse than
today.

### On the card

| Byte 15 | Bytes 16–31 |
|---|---|
| `$A2` | a gateway's key, as now |
| `$A3` | a gateway's key, scrambled with a passphrase, as now |
| `$A4` | the master M (new) |
| `$A5` | M, scrambled with a passphrase (new) |

Set-up no longer takes a typed key (decision 4). Until b048, a card
that already held `$A2` or `$A3` kept working as before: one key, used
for whichever gateway was set. From b048 those marks load as no key, and
the first gateway's key step makes a master over them (K4, below). The passphrase, the lock and Open-Apple-P work the same
on M as they do on a key now (lock.h). After a lock, the passphrase is
checked against the device id of the current gateway's derived key.

### TWOEGRAM.CFG

(As built in K2; K4, below, makes it a list.) It holds the gateway in
use, with its key name before the address:

    # Twoegram: the gateway
    GATEWAY=claw1 http://192.168.1.20:8417

The form before key names, `GATEWAY=http://...`, still loads, as a
gateway with no name. That works with a card that still holds a
gateway's key (`$A2` or `$A3`); with a master, set-up asks for a name.

**Planned as a list of 4 gateways, and dropped in K2 for room.** The
list, and the screen to pick from it, made the set-up overlay 9K, and it
has 6K ($0800 up to the program at $2000). Without the list, moving to
another gateway is typing its key name and address. No key is typed
again, which was the point.

### Set-up (the TWOEGRAM.SETUP overlay)

As built in K2 (K4, below, replaces it):

- **Three lines:** Key name, Address, Passphrase. Key names are
  lowercased as they are typed, and only `a-z 0-9 - _` are taken.
- **Open-Apple-M** makes a master. It asks first if the card already
  holds a key, because the gateways that know the old key will stop
  working. It saves M at once (`$A4`); a passphrase typed afterwards and
  Return lock it (`$A5`).
- **Open-Apple-V** shows K(g) for the key name in the field, with its
  device id and the commands to type on the gateway:

      openclaw a2 add "Apple IIe"
      hermes a2 add "Apple IIe"

  It needs the secret unlocked if there is a passphrase. The key is
  wiped from the screen and memory at the next key pressed.
- **Typing a key is gone.** The overlay writes keys out in base 32
  instead of reading them.
- **Open-Apple-X** and **Open-Apple-P** work as before, on the master.

**Memory.** The overlay is 5,615 bytes: code and text in `OVERLAY1`,
and its variables in their own segment, `OVL1DATA`, loaded with it.
Moving set-up's variables out of main memory, and the file writer
(`cfg_save`) into the overlay, gave main memory 223 bytes more: heap
2,685, from 2,462. `ks_derive` and `lock_use` live in the language card.

### K4: adding a gateway, and Settings (b048)

K2's set-up worked on the card (K3), but its hot keys confused: when to
press Open-Apple-M and when Open-Apple-V. Decided 2026-11-18, built the
same night:

- **No master to make by hand.** With no master on the card (or only a
  gateway's old key), the first key step makes one, without asking.
- **Adding a gateway** (TWOEGRAM.SETUP, `setup.c`) is two steps: the key
  name and address, then the key, shown by itself, with the `a2 add`
  commands. Return connects; while the gateway refuses the key, it comes
  back at the key step, saying so. At start-up a refused key opens it
  there, already filled in.
- **Settings** (Open-Apple-K; TWOEGRAM.PREFS, `prefs.c`, a third
  overlay) is one list: the saved gateways (connect, K to show the key
  again, Delete to forget), Add a gateway, Passphrase (set, change,
  remove: it covers every gateway), Start over with a new key, Remove
  the key from the card. Anything that shows a key returns to `link.c`,
  which opens the first overlay: two overlays cannot be loaded at once.
  Starting over is clearing the card, so the key step makes the new
  master.
- **TWOEGRAM.CFG is a list again**, up to four, the one in use first:

      # Twoegram: the gateways, the one in use first
      GATEWAY=claw1 http://192.168.1.20:8417
      GATEWAY=mini http://192.168.1.30:8417

  The program reads only the first line; the list is the overlays'
  (`setup_list.h`, included by both). A fifth forgets the last.
- **Open-Apple-R** connects, and if the gateway refuses the key, or there
  is none in memory, shows the key step.

**Memory.** Splitting set-up in two is what made room for the list.
TWOEGRAM.SETUP is 5,454 bytes with its data, TWOEGRAM.PREFS 6,064 of
the 6,144 an overlay may take. Main memory's heap is 1,936 (from
2,685): reading and writing the file moved into main memory, for both
overlays, and `link.c` grew.

### `a2 add`, on every server

Built in K1, in both plugins and both dev servers, and in the relay
when it comes. The plugins release it with K2, as OpenClaw plugin 1.2.0
and Hermes plugin 1.1.0:

    openclaw a2 add "Apple IIe"
    Key (from Twoegram's set-up; not shown as you type):
    Added device 5c01e9a2 (Apple IIe).
    Check that Twoegram's set-up shows the same device id.

- The key is read from a prompt that does not echo, never from an
  argument, and its check character catches a mistyped key.
- It refuses a key whose device id is already paired.
- The device it adds is exactly what `pair` makes today: the same
  file, the same `list` and `revoke`.
- `pair` stays. The relay needs it for its own key at each upstream,
  and so does an Apple on a disk from before K2.

### Testing

- **Shared vectors.** `derive_key(M, g)` goes in the Python
  `protocol.py` and the TypeScript `protocol.ts`, and the vectors in
  `hermes/tests/vectors.json` check it. The Apple's host tests check the
  C against the same vectors.
- **`tools/a2client.py --master`**, so the whole flow can be tried
  against the dev servers without the Apple.
- **Host tests:**
  - the new marks, and the old ones still working;
  - the config file's old and new forms;
  - switching gateways;
  - the lock with a master;
  - making a master when a key is already stored.
- **On the card:**
  - make a master;
  - add its key at claw1 and the mini;
  - switch between them in set-up;
  - lock and unlock;
  - revoke at one gateway, and check that the other still works.

## Part 2. The relay

### What it is

`twoegram-relay` is a small service, Python standard library only, run
by systemd on its own LXC.

- **Facing the Apple**, it is an ordinary a2 server: the same hello,
  signatures, sessions, lockouts and limits as the plugins, from shared
  code. Its own `devices.json` lists the Apples it serves, managed with
  `twoegram-relay pair | add | list | revoke`.
- **Facing the gateways**, it is an a2 client. Each gateway, an
  **upstream**, knows the relay as one more paired device, with a key
  made by that gateway's `a2 pair`. You can revoke it there like any
  Apple.

The Apple's hello at the relay lists every agent on every upstream.
Messages to an agent go to the upstream that has it.

### Pass-through, not a second copy

I first sketched the relay keeping its own log of every conversation.
On a closer look, **passing requests through** is simpler and loses
nothing:

| The Apple asks the relay | The relay asks the agent's upstream |
|---|---|
| `send?a=X` | `send?a=X'` (X's id on that upstream), same body |
| `poll?a=X&after=n&w=4000` | `poll?a=X'&after=n&w=3000` (a margin for the extra hop, well under the card's 8 s cutoff) |
| `hist?a=X&...` | `hist?a=X'&...`, unchanged |

- **What gets copied, and what doesn't.** The relay checks the Apple's
  signature, makes its own signed request upstream, checks the upstream's
  signature on the reply, and signs the payload again for the Apple. The
  message numbers are the upstream's, so history, paging, the start of
  the conversation and busy all work unchanged. The relay keeps no
  conversations, and it never folds text a second time: what the
  upstream already folded and cut into parts passes through as it is.
- **Unread marks.** Each upstream's poll reply carries `unread` lines for
  that upstream's other agents. For the upstreams the Apple is not using
  just then, the relay polls quietly every 20 seconds, with `w=0` and
  `after` set to the last number the Apple has seen there, so nothing is
  marked read that the Apple has not fetched. It adds those counts to the
  `unread` lines it sends.
- **Agent ids.** Ids pass through unchanged when they are unique. When
  two clash, the upstreams file sets an alias. Without one, the later
  upstream's agent gets its upstream's name as a suffix, such as
  `main-mini`, cut to 16 characters. The Apple shows the agent's `name`
  line in any case, so you still see "Pinchy" and "Hermes". The order is
  that of the upstreams file, so ids stay put. The Apple lists at most 8
  agents (`SES_AGENTS_MAX`). With more than that, the upstreams file
  picks which ones to list.
- **Sessions upstream.** The relay holds two sessions on each upstream,
  one for long polls and one for everything else, because sequence
  numbers must rise within a session. It says hello again on NO_SESSION,
  on REPLAY, and after 25 idle minutes (the limit is 30).
- **An upstream that is down** keeps its agents in the list. Requests to
  them get a signed BAD_REQUEST, with `why claw1 is not answering`, or
  `why claw1 refused the relay's key` after a revocation. The Apple
  already shows that as a note and does not retry (`link.c`). Whether
  that wording is right on the screen is a check for the card.
- **A relay restart** loses nothing but its upstream sessions and the
  unread counts from its quiet polls. Both are rebuilt within a poll.

**One limitation:** each upstream sees the relay as a new device, so
conversations through the relay start empty. What you said to Pinchy
from the Apple directly stays with that direct pairing, and you can
still reach it there.

### As built (R2)

- **The quiet poll's starting point.** Before the Apple has polled an
  agent through the relay, the relay does not know what the Apple has
  seen there. It asks for that agent's newest message (`hist`, which
  marks nothing as seen) and counts only what arrives after it. The
  gateway's own `unread` lines, for its other agents, are exact.
- **After a restart**, the relay lists only the gateways that answer
  its first hello. One that is down then is added within a minute of
  coming back. One that goes down later keeps its agents listed.
- **Code:**
  - `relay/twoegram_relay/upstream.py`: the client;
  - `relay.py`: `RelayServer`, an `A2Session`;
  - `config.py` and `cli.py`;
  - `relay/install.sh` and `twoegram-relay.service` for the LXC.
  - `make relay-dist` builds `build/twoegram-relay-0.1.0.tgz`, with the
    shared modules from `hermes/twoegram` copied in.
- **Python 3.11 or later**, checked by the script itself and by
  install.sh. (macOS's own Python 3.9 failed at one point, which is how
  the check came about.)

### Its files

| | |
|---|---|
| `/etc/twoegram-relay/upstreams.json` | name, URL, key and aliases for each upstream; mode 0600 |
| `/var/lib/twoegram-relay/devices.json` | the Apples it serves; mode 0600 |
| `/etc/twoegram-relay/relay.conf` | listening address and port (8417) |

`twoegram-relay upstream add claw1 http://192.168.1.20:8417` prompts for
the key from claw1's `openclaw a2 pair`, says hello to check it, and
lists the agents found. `upstream list` and `upstream remove` do the
obvious. No command prints a stored key.

### The security surface

- **The relay is the most valuable box in the system.** It holds a key
  for every gateway, so anyone who takes it can speak to every agent as
  the relay. Keep it small, and keep it alone.
- **LXC:** unprivileged Ubuntu LTS, Python 3 from the distribution,
  nothing else installed beyond the base system. The service runs as its
  own user, with systemd's sandboxing (`ProtectSystem=strict`,
  `NoNewPrivileges`, `PrivateTmp`, and write access only to its two
  folders).
- **Firewall** (Proxmox's, on the container's interface):
  - inbound TCP 8417 from the Apple's address only, and SSH from the
    Mac Studio only;
  - outbound only to each upstream's a2 port, plus DNS and package
    mirrors while updating.
- **Each gateway's side:** the relay is one device on its list. Revoke it
  there and the relay is cut off from that gateway at once.
- **Both legs are signed and not encrypted**, as everything is today.
  Never forward 8417 to the internet.

### Where the code lives

The Hermes plugin's `protocol.py`, `text.py`, `store.py` and the
session half of `server.py` are what the relay needs. The plan:

1. **Split `hermes/twoegram/server.py`** into the session layer (hello,
   signatures, sessions, lockouts) and the conversation layer (send,
   poll, hist over a ConvStore). The Hermes plugin keeps its behaviour,
   and its 130 tests prove it.
2. **`relay/`** holds the relay's own pieces:
   - the upstream client, an asyncio version of `tools/a2client.py`'s
     protocol code;
   - the pass-through layer;
   - the CLI;
   - the systemd unit.
3. **`make relay-dist`** builds one tarball with the shared modules
   copied in, because the Hermes plugin's folder has to stay
   self-contained to install.

### Testing

- **No Apple, no gateway.** Run two dev servers on this Mac: the
  TypeScript one (OpenClaw-shaped, with agents `main` and `coder`) and
  the Python one (agent `hermes`). Put the relay in front of both and
  drive it with `tools/a2client.py`. This covers:
  - the merged agent list and an alias;
  - send, poll and hist to each upstream;
  - unread from the quiet upstream;
  - an upstream stopped and started;
  - the relay revoked at one upstream;
  - a relay restart.
  Each run gets a clean environment, with no provider keys.
- **On the LXC:** install the tarball, then check that the firewall
  refuses any other address.
- **On the card:** pair the Apple with the relay, then talk to Pinchy and
  Hermes in one session. Watch the unread marks move between them.

## Later

- **HTTPS from the Apple to the relay.** The card's TLS checks against
  its bundled Mozilla roots and cannot add one of ours (PLAN.md section
  2), so a home-made certificate will not do. The relay would need a
  real certificate: a domain
  name you own, Let's Encrypt by DNS challenge, and that name resolving
  to the LXC on the LAN. The relay is the natural place for this, being
  a single small box. A probe would first check that the card connects
  to a LAN address by name with a public certificate.
- **Pairing by X25519.** The Apple and a server would agree on a key
  with one key exchange, then compare a short code on both screens, the
  way Bluetooth pairing does. No secret would ever be shown or typed.
  The cost is X25519 in 6502 code, which I estimate (not measured) at a
  minute or two each pairing at 1 MHz. After pairing, everything stays
  HMAC, as now.

## Decisions

All settled on 2026-11-18:

1. **Key names are typed by you** in set-up, not taken from the
   address, which can change.
2. **The LXC is Ubuntu LTS**, unprivileged, from Proxmox's template, at
   its own address on the LAN. That's your usual choice; nothing in the
   relay depends on the distribution.
3. **Clashing agent ids** get the upstream's name as a suffix
   (`main-mini`), unless the upstreams file gives an alias.

4. **Set-up no longer takes a typed key** (2026-11-18). Every byte
   in the set-up overlay counts, and one way of pairing is easier to
   explain and test.
   - Every gateway the Apple talks to directly needs `a2 add`: the
     OpenClaw plugin 1.2.0 or later, the Hermes plugin 1.1.0 or later,
     or the relay.
   - Once you make a master, there is no way back to a gateway's key on
     that disk. An older disk still has its set-up for that.

## Milestones

| | What | Done when |
|---|---|---|
| K1 | `derive_key` in all three languages, with vectors; `a2 add` in both plugins and the dev server | the tests pass; `a2client.py --master` pairs by `add` (**done 2026-11-18**) |
| K2 | the card's new marks, the key name in TWOEGRAM.CFG, set-up's screens | host tests pass; tried in Virtual ][ (**done 2026-11-18**, b037; the gateway list dropped for room) |
| K3 | on the card | a master, two gateways, switching, the lock, one revoked (**done 2026-11-18**, b046) |
| K4 | adding a gateway, and Settings, as two overlays; the list of gateways | host tests pass; on the card |
| R1 | `server.py` split; the Hermes plugin unchanged | its tests pass (**done 2026-11-18**) |
| R2 | the relay: upstream client, pass-through, CLI | the two-dev-server run passes (**done 2026-11-18**, relay 0.1.0) |
| R3 | the LXC: install, systemd, firewall | the Mac Studio talks through it; other addresses are refused |
| R4 | on the card through the relay | Pinchy and Hermes in one session |

K1 to K3 are useful without the relay. The relay doesn't depend on
them, but it gets `add` for free once K1 is done.
