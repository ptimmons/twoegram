# Testing backlog

What is built and waiting for the real Apple, the card, or your gateway,
newest first. Each entry says what to bring to the test and where its
steps are. When an entry passes, it moves to "Passed", with the date
and build. The detailed checklists stay in [PROBE.md](PROBE.md).

## Waiting

One disk covers all of this: `build/TWOEGRAM-b036.po` (or later), with
plugin 1.1.0 (`build/openclaw-twoegram-1.1.0.tgz`) on the gateway.

### 1. Twoegram on Hermes Agent (plugin 1.0.0, built 2026-11-17)

Steps: hermes/README.md, "Install", on the Mac mini (`mini.local`).
Installing changes your running Hermes, so it waits for you.

- **The Apple connects** to `http://192.168.1.30:8417`, with a key
  from `hermes a2 pair`. Hermes is listed as the one agent.
- **A real answer** from Hermes's model, folded to fit the screen.
- **An approval:** ask for something that needs one, and type
  `/approve` (and, another time, `/deny`).
- **`/sethome`**, then a scheduled message arriving on the Apple.
- **Busy:** the Apple shows Hermes working until the answer comes.
- **The new words** (b036): the splash, set-up's help, and the bad-key
  line.

### 2. Saying when the key is refused (b034, built 2026-11-17, optional)

Steps: PROBE.md, "Build b034" (needs a spare pairing to revoke).

- **The pane's second line**, "Pair this Apple again (openclaw a2 pair),
  then Open-Apple-K", after a revocation mid-conversation.

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
