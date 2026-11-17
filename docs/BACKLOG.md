# Testing backlog

What is built and waiting for the real Apple, the card, or your gateway,
newest first. Each entry says what to bring to the test and where its
steps are. When an entry passes, it moves to "Passed", with the date
and build. The detailed checklists stay in [PROBE.md](PROBE.md).

## Waiting

One disk covers all of this: `build/TWOEGRAM-b028.po` (or later), with
plugin 1.1.0 (`build/openclaw-twoegram-1.1.0.tgz`) on the gateway.

### 1. The passphrase and the lock (b028, built 2026-11-16)

Steps: PROBE.md, "Build b028".

- **The set-up screen loads from the disk** (`TWOEGRAM.SETUP`), opens
  and closes cleanly, and opens again.
- **Setting a passphrase**, typed twice. **Time the pause** after the
  second Return: 64 hashes on the card, not yet measured. This has to
  be settled before you rely on a passphrase, because changing the
  count changes what a passphrase makes.
- **Open-Apple-L**, a wrong passphrase refused at once, the right one
  signs in again.
- **The idle lock** after 10 minutes.
- **A fresh start** asks for the passphrase; a wrong one is caught by the
  gateway and asked again.
- **Open-Apple-P** removes the passphrase.

### 2. Straight to the start, scroll arrows, narrower list (b023, built 2026-11-16)

Steps: PROBE.md, "Build b023" (the b028 disk carries it too).

- **Plugin 1.1.0 first.** Install it on the gateway as INSTALL.md step 2
  says; `openclaw plugins list` shows `a2` at 1.1.0. (b023 against an
  older plugin shows the newest page as if it were the start.)
- **Open-Apple-< goes to the very start** in one request.
- **Scrolling down from the start** fetches newer messages.
- **The scroll arrows** in column 80: up at the pane's top when there is
  more above, down at its foot when there is more below. The up arrow is
  MouseText $4B, which has not been seen on the card yet.
- **No `THE START OF THE CONVERSATION` message** any more.
- **The narrower list:** 18 columns, names in full up to 16 characters,
  a diamond and no count.
- **The gap:** text stops one column before the arrows.

### 3. Lights from the Apple (2026-11-17)

- **Ask Pinchy to turn a light off from Twoegram.** A test device read a
  light's state through a2 without being an owner (PLAN.md section 9,
  item 5); switching one has not been tried. Any build will do. (The
  test device was revoked on claw1 on 2026-11-17.)

### 4. Older items, not yet seen

- **The key-save error message** (b017). If the card cannot keep the
  key, set-up says "The key could not be kept on the card:" and the
  card's reason. It never happened again after b016, so it is unseen.
  Nothing to do unless it turns up.
- **Pinchy's "interrupted" replies** (2026-11-16). Pinchy once said
  its replies had been interrupted, probably because of the gateway
  restarts during testing. Worth noticing if it happens again with no
  restarts.

## Passed

- **Release 1.0** (2026-11-16, b019, plugin 1.0.1): the package install,
  the newcomer's first boot, a long answer in parts, the security audit.
- **Milestone 7** (2026-11-16, b017): every error state, agents by name,
  the check mark and down arrow glyphs.
- **Milestones 2 to 6:** see PLAN.md.
