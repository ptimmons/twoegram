# Testing backlog

What is built and waiting for the real Apple, the card, or your gateway.
**"Tonight, in order"** is the whole of it as one checklist, with every
command in place: start there. "Waiting" below keeps each item's
background, newest first. When an entry passes, it moves to "Passed",
with the date and build. The older, detailed checklists stay in
[PROBE.md](PROBE.md).

## Tonight, in order (2026-11-18)

Everything below "Waiting", as one list to work down, with the commands
in place. The files are on the Mac Studio, in `~/repos/twoegram/build/`.
Tick each box as it passes, and note anything odd beside it; the other
documents are only for reference. Parts A to E need about an hour;
part F (the relay) needs a new container and can be another night.

### A. Get the gateways ready (from the Mac Studio)

**claw1: OpenClaw plugin 1.2.0** (it adds `openclaw a2 add`).

- [ ] Copy it over (put your user name in place of `you`):

  ```bash
  scp ~/repos/twoegram/build/openclaw-twoegram-1.2.0.tgz you@192.168.1.20:
  ```

- [ ] On claw1, check it arrived whole:

  ```bash
  echo "218a402958fc2401a133e22de27a374d0a264a449dc7cb010fbb8a5666cbef0e  openclaw-twoegram-1.2.0.tgz" | sha256sum -c
  ```

- [ ] On claw1, install it with the gateway stopped (INSTALL.md step 2
  says why):

  ```bash
  openclaw gateway stop
  ```

  ```bash
  openclaw plugins install ./openclaw-twoegram-1.2.0.tgz --force --accept-capabilities
  ```

  ```bash
  openclaw gateway start
  ```

- [ ] `openclaw plugins list` shows `a2` at 1.2.0, with no warning, and
  `openclaw a2 --help` lists `add`.

**The Mac mini: Hermes plugin 1.1.0** (it adds `hermes a2 add`).

- [ ] From the Mac Studio, copy the plugin over the old one:

  ```bash
  rsync -a --delete --exclude __pycache__ ~/repos/twoegram/hermes/twoegram/ you@192.168.1.30:.hermes/plugins/twoegram/
  ```

- [ ] On the mini, stop the gateway that's running now. I started it by
  hand on 2026-11-17, not as a service:

  ```bash
  hermes gateway stop
  ```

- [ ] Then start it again with the new plugin, this time as a background
  service that comes back after a reboot (the step I wasn't allowed to
  do over SSH):

  ```bash
  hermes gateway install --start-now --start-on-login
  ```

  (Or, without a service: `hermes gateway restart`, which then runs in
  that terminal.)

- [ ] On the mini, `grep version ~/.hermes/plugins/twoegram/plugin.yaml`
  says 1.1.0, `hermes a2 --help` lists `add`, and
  `~/.hermes/logs/gateway.log` ends with `a2: listening on
  http://0.0.0.0:8417/a2/`.

### B. The new disk with the key you have now

Boot `build/TWOEGRAM-b037.po`, as you've booted the others.

- [ ] It connects to Pinchy by itself, as before: the key now on the card
  still works.
- [ ] **Saving** (b033): Open-Apple-S, Return. How long did it take, and
  for how many messages? The file reads right. Saving again to the same
  name asks `REPLACE ...?`. Esc partway says how much was saved. On the
  network disk: does it save, or say why not?
- [ ] **Paging** (b023): Open-Apple-< goes to the very start in one
  step, and scrolling down from there fetches newer messages. There is
  no `THE START OF THE CONVERSATION` message any more. The scroll arrows
  in column 80 come and go as there is more above or below, and the text
  stops one column before them. The list is 18 columns wide, with names
  in full and a diamond for news.

### C. Make the Apple's own key (K3)

- [ ] Open-Apple-K. The set-up screen shows Key name (empty), Address
  (claw1's) and Passphrase.
- [ ] Open-Apple-M. It asks `THIS REPLACES THE KEY FROM THE GATEWAY ...`;
  press Y. It says `THIS APPLE HAS A NEW KEY`. How long did that take?
- [ ] Type `claw1` as the key name, then press Open-Apple-V. A key of 27
  characters appears, with a device id beside it.
- [ ] On claw1, type the key from the Apple's screen when asked (it
  isn't shown as you type):

  ```bash
  openclaw a2 add "Apple IIe"
  ```

  The device id it prints matches the Apple's.
- [ ] On the Apple, press a key and the key disappears. Then press Return:
  connected to Pinchy. The conversation starts empty, as a new device's
  does.
- [ ] Send Pinchy something; a real answer comes back.

### D. The Mac mini's key, and Hermes itself

- [ ] Open-Apple-K. Key name `mini`, Address `192.168.1.30:8417`,
  Open-Apple-V. On the mini:

  ```bash
  hermes a2 add "Apple IIe"
  ```

  Same device id. Press a key on the Apple, then Return: Hermes is the
  one agent.
- [ ] **A real answer** from Hermes's model, folded to fit the screen.
- [ ] **Busy:** the Apple shows Hermes working until the answer comes.
- [ ] **An approval:** ask for something that needs one, and type
  `/approve`. Another time, `/deny`.
- [ ] **`/sethome`**, then a scheduled message arriving on the Apple.
- [ ] **Back to Pinchy, with no key typed:** Open-Apple-K, key name
  `claw1`, Address `192.168.1.20:8417`, Return. Pinchy, with the
  conversation from part C.

### E. The passphrase, the lock, and a revocation

- [ ] Open-Apple-K, Tab twice to Passphrase, type one (8 or more
  characters) and Return, then type it again and Return.
- [ ] Open-Apple-L locks. A wrong passphrase says `That is not the
  passphrase.`, and the right one brings the conversation back.
- [ ] **The idle lock:** leave it untouched for 10 minutes, and it locks
  by itself.
- [ ] **A fresh start:** quit and start Twoegram. It asks for the
  passphrase first. A wrong one gets "The gateway does not know the key
  that passphrase makes", then it asks again.
- [ ] **One gateway revoked:** switch to the mini (key name `mini`),
  then on the mini run `hermes a2 list` and revoke the Apple:

  ```bash
  hermes a2 revoke <its id>
  ```

  At the next poll the pane says `THE GATEWAY DOES NOT KNOW THIS KEY`
  and, below it, "Add this Apple's key on the gateway again:
  Open-Apple-K, Open-Apple-V." Switch to claw1: it still works. Add the
  key on the mini again with `hermes a2 add` (Open-Apple-V shows it) if
  you want Hermes back.
- [ ] **Tidy claw1:** `openclaw a2 list` there still shows the Apple's
  old pairing, from before part C. Revoke it with `openclaw a2 revoke
  <its id>`.
- [ ] **The screen:** does set-up read well, and does the key show
  clearly? Typing fast on the real keyboard, are any keys lost?

### F. The relay on a Proxmox LXC (R3 and R4; can wait)

- [ ] In Proxmox, make an unprivileged Ubuntu LTS container with a fixed
  address (1 core, 256 MB, 2 GB). Below, `RELAY` stands for that
  address.
- [ ] From the Mac Studio:

  ```bash
  scp ~/repos/twoegram/build/twoegram-relay-0.1.0.tgz root@RELAY:
  ```

- [ ] On the container, as root. The checksum is
  `dc71554a630329588c19ff0475f6d4cafd8ab3eb1d039308f0ddd02a9ef42489`:

  ```bash
  sha256sum twoegram-relay-0.1.0.tgz && tar xzf twoegram-relay-0.1.0.tgz && twoegram-relay/install.sh
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
  systemctl enable --now twoegram-relay
  ```

  ```bash
  journalctl -u twoegram-relay -f
  ```

  The log says `relay: listening on http://0.0.0.0:8417/a2/`, with
  claw1's agents and `hermes`.
- [ ] On the Apple, Open-Apple-K: key name `relay`, Address `RELAY`,
  Open-Apple-V. On the container:

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

### G. Afterwards

- [ ] On the Mac Studio, delete the empty Hermes folder that its tests
  made (see "To do by hand" below):

  ```bash
  rm -rf ~/.hermes
  ```

- [ ] Tell me what passed, what didn't, and the timings. I'll move them
  to "Passed" and fix the rest.

## Waiting

One disk covers all of this: `build/TWOEGRAM-b037.po` (or later), with
plugin 1.2.0 (`build/openclaw-twoegram-1.2.0.tgz`) on claw1, the
Hermes plugin 1.1.0 on the Mac mini, and for item 2 the relay
(`build/twoegram-relay-0.1.0.tgz`) on a new LXC. b037 keeps working with the key now
on the card, so the older items can be tried before item 1.

### 1. Keys made on the Apple (K2, b037, built 2026-11-18)

Steps: PROBE.md, "Build b037". It needs the new plugins on both
gateways first (listed there).

- **Open-Apple-M** makes the key; how long it takes.
- **Open-Apple-V** shows claw1's key and device id; **`openclaw a2 add`**
  takes it, and the ids match.
- **The mini** gets its own key the same way, with `hermes a2 add`.
- **Switching** between claw1 and the mini by key name and address only.
- **The passphrase** and the lock, with the new key.
- **Revoked at one gateway**, the other still works; the new hint line
  (this replaces b034's check, which needed a typed key).
- **The old key** on the card works with b037 until replaced.

### 2. The relay on a Proxmox LXC (R3 and R4, relay 0.1.0, built 2026-11-18)

Steps: [relay/README.md](../relay/README.md), "On a Proxmox LXC", with
`build/twoegram-relay-0.1.0.tgz` (`make relay-dist`). Best after item
1, since the Apple adds its key to the relay the same way. It needs you
to make the container: unprivileged Ubuntu LTS, with a fixed address.

- **install.sh** on the container: the user, the folders (mode 0700),
  the systemd unit.
- **Each gateway:** `openclaw a2 pair "relay"` on claw1 and `hermes a2
  pair "relay"` on the mini, then `upstream add` on the container. Both
  say "answering" in `upstream list`.
- **The service** starts, and its log lists Pinchy's and Hermes's agents.
- **The Apple:** key name `relay`, the container's address, Open-Apple-V,
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

### 3. Twoegram on Hermes Agent (plugin 1.0.0 installed, built 2026-11-17)

Installed on the Mac mini on 2026-11-17 (Hermes v0.23.2): the plugin
is enabled, `platforms.a2.enabled: true` is set, and the gateway listens
on port 8417. The config from before is in
`~/.hermes/config.yaml.bak-before-twoegram`. A temporary device paired,
signed in from the Mac Studio, saw `hermes (Hermes)`, and was revoked,
so the mini has no devices.

The Apple reaches the mini as item 1, step 4 says: key name `mini`,
address `192.168.1.30:8417`, and `hermes a2 add`, which needs the
Hermes plugin 1.1.0 on the mini first. (With the b036 disk, `hermes a2
pair` and typing its key does the same, replacing claw1's key.)

**Optional:** make the gateway a background service, so it comes back
after a reboot. I was not allowed to do this over SSH:

```bash
hermes gateway install --start-now --start-on-login
```

Then check:

- **The Apple connects** to `http://192.168.1.30:8417`. Hermes is
  listed as the one agent.
- **A real answer** from Hermes's model, folded to fit the screen.
- **An approval:** ask for something that needs one, and type
  `/approve` (and, another time, `/deny`).
- **`/sethome`**, then a scheduled message arriving on the Apple.
- **Busy:** the Apple shows Hermes working until the answer comes.
- **The new words** (b036): the splash and set-up's help.

### 4. Saving the conversation (b033, built 2026-11-17)

Steps: PROBE.md, "Build b033".

- **Open-Apple-S** saves the whole conversation; how long it takes.
- **The file** reads right on the Apple (or on the Mac).
- **Replacing** a file asks first.
- **Esc** partway stops it and says how much was saved.
- **A network disk:** writable, or not.

### 5. The rest of the lock (b028, built 2026-11-16)

Steps: PROBE.md, "Build b028", steps 4 and 5. Setting a passphrase,
the lock and Open-Apple-P passed on 2026-11-17; these were not
mentioned:

- **The idle lock** after 10 minutes.
- **A fresh start** asks for the passphrase; a wrong one is caught by the
  gateway and asked again.

### 6. Straight to the start, scroll arrows, narrower list (b023, built 2026-11-16)

Steps: PROBE.md, "Build b023" (the b028 disk carries it too).

- **Plugin 1.1.0 first.** Install it on the gateway as INSTALL.md step 2
  says; `openclaw plugins list` shows `a2` at 1.1.0. (b023 against an
  older plugin shows the newest page as if it were the start.)
- **Open-Apple-< goes to the very start** in one request.
- **Scrolling down from the start** fetches newer messages.
- **The scroll arrows** in column 80: up at the pane's top when there is
  more above, down at its foot when there is more below. (Both glyphs,
  MouseText $4B and $4A, seen on the card 2026-11-17 and look right.)
- **No `THE START OF THE CONVERSATION` message** any more.
- **The narrower list:** 18 columns, names in full up to 16 characters,
  a diamond and no count.
- **The gap:** text stops one column before the arrows.

### 7. Older items, not yet seen

- **The key-save error message** (b017). If the card cannot keep the
  key, set-up says "The key could not be kept on the card:" and the
  card's reason. It never happened again after b016, so it is unseen.
  Nothing to do unless it turns up.
- **Pinchy's "interrupted" replies** (2026-11-16). Pinchy once said
  its replies had been interrupted, probably because of the gateway
  restarts during testing. Worth noticing if it happens again with no
  restarts.

## To do by hand

- **Delete `~/.hermes` on the Mac Studio (abbeyroad).** Hermes's test
  setup made it on 2026-11-17 at 12:31, while the plugin was being
  tested: an empty default skeleton and a default `SOUL.md`, nothing of
  yours. It is not the Mac mini's.

  ```bash
  rm -rf ~/.hermes
  ```

## Passed

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
