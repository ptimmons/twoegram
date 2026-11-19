# Testing backlog

What is built and waiting for the real Apple, the card, or your gateway.
**"Next: adding a gateway, and Settings"** is the next checklist, then
**"Next night: the relay"**, with every command in place: start there. "Waiting" below keeps each item's
background, newest first. When an entry passes, it moves to "Passed",
with the date and build. The older, detailed checklists stay in
[PROBE.md](PROBE.md).

## Next: adding a gateway, and Settings (K4, b048)

Set-up is now two screens (RELAY.md, "K4"): **Add a gateway** (the key
name and address, then the key, shown by itself) and **Settings**
(Open-Apple-K). No more Open-Apple-M or Open-Apple-V. Boot
`build/TWOEGRAM-b048.po`. Its TWOEGRAM.CFG lists claw1, then the mini.

### A. Start-up and switching

- [ ] It connects to Pinchy by itself, with nothing typed: the same
  master as on b046, so the same key for claw1.
- [ ] **Settings:** Open-Apple-K. Gateways: claw1 (`in use`) and mini,
  then Add a gateway. Below: Passphrase `none`, Start over with a new
  key, Remove the key from the card. The arrows move the bar, and the
  two lines under the list and the status bar change with it. Esc goes
  back to the conversation.
- [ ] **To the mini:** Open-Apple-K, Down to mini, Return. If the mini
  still has the Apple's key, Hermes is the agent. If you revoked it in
  part E and didn't add it back, Add a Gateway opens at mini's key by
  itself: `hermes a2 add "Apple IIe"` on the mini, then Return.
- [ ] **Back to claw1:** Open-Apple-K, Down to claw1, Return: Pinchy,
  with the conversation as it was.
- [ ] **K:** Open-Apple-K, on claw1 press K. claw1's key shows, with the
  same device id as `openclaw a2 list` on claw1. Return connects.

### B. The passphrase, in Settings

- [ ] Open-Apple-K, Down to Passphrase, Return. Type one (8 or more
  characters), Return, then again, Return. The status bar says `THE KEY
  IS LOCKED WITH THE PASSPHRASE`, and the line says `set`. Esc.
- [ ] Open-Apple-L locks, and the passphrase unlocks, as before.
- [ ] Quit and start Twoegram: the passphrase first. Press **Esc**
  instead: Settings, with "Locked: a new key, without the passphrase, is
  Start over below." at the foot. Esc again: the pane says `LOCKED`.
  Open-Apple-R asks for the passphrase again; type it: Pinchy.
- [ ] Open-Apple-K, Down to Passphrase, Delete, Y: `THE KEY NO LONGER
  NEEDS A PASSPHRASE`, and the line says `none`.

### C. Adding a gateway

- [ ] Open-Apple-K, Down to Add a gateway, Return. The fields are empty.
  Esc, and Esc again: back in the conversation, nothing changed.

### D. Starting over (last: it means adding both keys again)

This is the path a new card takes at its first start: the key step
making the master.

- [ ] Open-Apple-K, Down to Start over with a new key, Return, Y. It
  says `MAKING THIS APPLE'S KEY`, then shows claw1's new key. How long
  did that take? On claw1, `openclaw a2 add "Apple IIe"`; the device ids
  match. Return: Pinchy, with an empty conversation (a new device).
- [ ] The mini: Open-Apple-K, Down to mini, Return. The mini doesn't know
  the new key, so its key shows by itself: `hermes a2 add "Apple IIe"`,
  Return: Hermes.
- [ ] Tidy both gateways: `openclaw a2 list` and `hermes a2 list` show
  the old device as well. Revoke it on each (`a2 revoke <its id>`).

### E. The screens

- [ ] Do Settings and Add a gateway read well on the real screen? Is
  anything unclear, or in the wrong place?

## Next night: the relay

Parts A to E of the 2026-11-18 checklist passed on b046 (see "Passed").
What is left is the relay, which needs a new container. Use
`build/TWOEGRAM-b048.po`, or a later disk.

### The relay on a Proxmox LXC (R3 and R4)

- [ ] In Proxmox, make an unprivileged Ubuntu LTS container with a fixed
  address (1 core, 256 MB, 2 GB). Below, `RELAY` stands for that
  address.
- [ ] Your own login, not root: an admin user with sudo and the Mac's
  key, then root and password logins over SSH turned off
  ([relay/README.md](../relay/README.md), step 2). Below, `you` is that
  user. Check `ssh you@RELAY` and `sudo -v` before leaving the console.
- [ ] From the Mac Studio:

  ```bash
  scp ~/repos/twoegram/build/twoegram-relay-0.1.0.tgz you@RELAY:
  ```

- [ ] On the container, as `you`; only `install.sh` gets root. The
  checksum is
  `f573649ecbf9b9bd70bdadc788e8c2294bdfed2bf93231249c824fdc8435440a`:

  ```bash
  sha256sum twoegram-relay-0.1.0.tgz && tar xzf twoegram-relay-0.1.0.tgz && sudo twoegram-relay/install.sh
  ```

- [ ] Pair the relay at claw1 (on claw1):

  ```bash
  openclaw a2 pair "relay"
  ```

  Then, on the container, paste that key when asked:

  ```bash
  sudo -u twoegram-relay /opt/twoegram-relay/twoegram-relay upstream add claw1 192.168.1.20:8417
  ```

  It lists claw1's agents.
- [ ] The same for the mini: `hermes a2 pair "relay"` on the mini, then
  on the container:

  ```bash
  sudo -u twoegram-relay /opt/twoegram-relay/twoegram-relay upstream add mini 192.168.1.30:8417
  ```

- [ ] Start it, and watch its log:

  ```bash
  sudo systemctl enable --now twoegram-relay
  ```

  ```bash
  sudo journalctl -u twoegram-relay -f
  ```

  The log says `relay: listening on http://0.0.0.0:8417/a2/`, with
  claw1's agents and `hermes`.
- [ ] On the Apple, Open-Apple-K, Add a gateway: key name `relay`,
  Address `RELAY`, Return. The Apple shows its key. On the container:

  ```bash
  sudo -u twoegram-relay /opt/twoegram-relay/twoegram-relay add "Apple IIe"
  ```

  Same device id. Return: one list, with claw1's agents first, then
  `hermes`.
- [ ] Pinchy and Hermes in one session. Send to each and switch between
  them. Within about 20 seconds, the one you're not in shows a diamond
  when it has news.
- [ ] Stop the mini's gateway. Hermes gives a note, "mini is not
  answering", and Pinchy keeps working. Start the mini's gateway again.
- [ ] The firewall, in Proxmox, on the container's network device: input
  and output policy DROP. Allow in TCP 8417 from the Apple and TCP 22
  from the Mac. Allow out TCP to 192.168.1.20:8417 and
  192.168.1.30:8417. The Apple still gets through, and port 8417 is
  refused from another machine.

## Waiting

### 1. The relay on a Proxmox LXC (R3 and R4, relay 0.1.0, built 2026-11-18)

Steps: [relay/README.md](../relay/README.md), "On a Proxmox LXC", with
`build/twoegram-relay-0.1.0.tgz` (`make relay-dist`). Since the Apple adds its key to the relay the same way. It needs you
to make the container: unprivileged Ubuntu LTS, with a fixed address.

- **install.sh** on the container: the user, the folders (mode 0700),
  the systemd unit.
- **Each gateway:** `openclaw a2 pair "relay"` on claw1 and `hermes a2
  pair "relay"` on the mini, then `upstream add` on the container. Both
  say "answering" in `upstream list`.
- **The service** starts, and its log lists Pinchy's and Hermes's agents.
- **The Apple:** Add a gateway: key name `relay`, the container's address,
  then `twoegram-relay add`. One list: claw1's agents first, then the
  mini's (`hermes`).
- **Pinchy and Hermes in one session.** Send to each, and switch between
  them. Do the unread marks show for the one you're not in? They come
  within about 20 seconds.
- **The firewall:** from another machine on the LAN, port 8417 on the
  container is refused. The Apple still gets through.
- **A gateway down:** stop the mini's gateway. Hermes gives a note,
  "mini is not answering", and Pinchy still works. Start it again.
- **The note on the Apple:** how a relay's BAD_REQUEST note reads on the
  screen (`link.c` shows it, but its wording is untried on the card).

### 2. Older items, not yet seen

- **The key-save error message** (b017). If the card cannot keep the
  key, set-up says "The key could not be kept on the card:" and the
  card's reason. It never happened again after b016, so it is unseen.
  Nothing to do unless it turns up.
- **Pinchy's "interrupted" replies** (2026-11-16). Pinchy once said
  its replies had been interrupted, probably because of the gateway
  restarts during testing. Worth noticing if it happens again with no
  restarts.

## To do by hand

- **Optional: Hermes as a background service on the Mac mini**, so it
  comes back after a reboot. I was not allowed to do this over SSH:

  ```bash
  hermes gateway install --start-now --start-on-login
  ```

- **Delete `~/.hermes` on the Mac Studio (abbeyroad).** Hermes's test
  setup made it on 2026-11-17 at 12:31, while the plugin was being
  tested: an empty default skeleton and a default `SOUL.md`, nothing of
  yours. It is not the Mac mini's.

  ```bash
  rm -rf ~/.hermes
  ```

## Passed

- **Keys made on the Apple, Hermes, saving, paging, the lock**
  (2026-11-18, b046, OpenClaw plugin 1.2.0, Hermes plugin 1.1.0): parts
  A to E of that night's checklist.
  - Keys (K3): Open-Apple-M and Open-Apple-V on the card, `openclaw a2
    add` and `hermes a2 add` with matching device ids, switching
    between claw1 and the mini by key name and address alone, one
    gateway revoked while the other kept working, and the old claw1
    pairing revoked. b037 froze at Open-Apple-V: cc65 put
    `write_key`'s alphabet at the function's entry, fixed in b046
    (`tools/cc65check.py` now catches it).
  - Hermes on the mini: real answers, busy, `/approve` and `/deny`,
    `/sethome` and a scheduled message.
  - Saving (b033): 25 messages in about 6 s. One save got a transient
    ProDOS error on the network disk; the next attempt saved.
  - Paging (b023): Open-Apple-< to the start, scroll arrows, the
    18-column list.
  - The lock (b028): the idle lock, and a fresh start asking for the
    passphrase, with a wrong one caught by the gateway.

- **The passphrase and the lock** (2026-11-17, b028): the set-up screen
  from the disk, setting a passphrase, Open-Apple-L and unlocking,
  Open-Apple-P. The
  pause for 64 hashes is negligible, so `PASS_ROUNDS` stays at 64.
- **Lights from the Apple** (2026-11-17): Pinchy switches lights in the
  house when asked from Twoegram, with the Apple not an owner.
- **Release 1.0** (2026-11-16, b019, plugin 1.0.1): the package install,
  the newcomer's first boot, a long answer in parts, the security audit.
- **Milestone 7** (2026-11-16, b017): every error state, agents by name,
  the check mark and down arrow glyphs.
- **Milestones 2 to 6:** see PLAN.md.
