# Testing backlog

What is built and waiting for the real Apple, the card, or your gateway,
newest first. Each entry says what to bring to the test and where its
steps are. When an entry passes, it moves to "Passed", with the date
and build. The detailed checklists stay in [PROBE.md](PROBE.md).

## Waiting

One disk covers all of this: `build/TWOEGRAM-b037.po` (or later), with
plugin 1.2.0 (`build/openclaw-twoegram-1.2.0.tgz`) on claw1 and the
Hermes plugin 1.1.0 on the Mac mini. b037 keeps working with the key now
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

### 2. Twoegram on Hermes Agent (plugin 1.0.0 installed, built 2026-11-17)

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

### 3. Saving the conversation (b033, built 2026-11-17)

Steps: PROBE.md, "Build b033".

- **Open-Apple-S** saves the whole conversation; how long it takes.
- **The file** reads right on the Apple (or on the Mac).
- **Replacing** a file asks first.
- **Esc** partway stops it and says how much was saved.
- **A network disk:** writable, or not.

### 4. The rest of the lock (b028, built 2026-11-16)

Steps: PROBE.md, "Build b028", steps 4 and 5. Setting a passphrase,
the lock and Open-Apple-P passed on 2026-11-17; these were not
mentioned:

- **The idle lock** after 10 minutes.
- **A fresh start** asks for the passphrase; a wrong one is caught by the
  gateway and asked again.

### 5. Straight to the start, scroll arrows, narrower list (b023, built 2026-11-16)

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

### 6. Older items, not yet seen

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
