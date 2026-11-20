# Installing the a2 channel on your OpenClaw gateway

> For a gateway running **Hermes Agent** instead, see
> [hermes/README.md](../hermes/README.md). The Apple's disk is the same.

The `a2` plugin lets Twoegram on the Apple //e talk to your gateway's
agents. It needs OpenClaw 2026.11.3 or later (before 2026.12; checked
with 2026.11.3 and 2026.11.4) and Node 24.16+ or 26.1+, which your
gateway already has. It has no npm dependencies.

The release (`make release`, in `build/release/`) is two files and their
checksums:

- `TWOEGRAM-1.0.po`: the Apple's disk, with Twoegram and the card probe.
  It carries no address and no key: Twoegram asks for a gateway at the
  first start, and makes the key itself.
- `openclaw-twoegram-1.2.1.tgz`: the plugin, compiled, with only the
  files OpenClaw loads.

## 1. Copy the plugin to the gateway host

From the Mac:

```bash
scp build/release/openclaw-twoegram-1.2.1.tgz build/release/SHA256SUMS GATEWAY-HOST:
```

On the gateway host, check it arrived whole:

```bash
grep tgz SHA256SUMS | sha256sum -c
```

## 2. Install it

With the gateway stopped:

```bash
openclaw gateway stop
```

**Coming from a linked folder** (the development install below), do
this first, or the install ends with `Plugin "a2" has no authoritative
package-owner metadata`: drop the folder from OpenClaw's load paths
(`[]` if it was the only one; `openclaw config get plugins.load.paths`
shows them) and refresh. Your `channels.a2` settings and paired Apples
stay.

```bash
openclaw config set plugins.load.paths '[]' --strict-json
```

```bash
openclaw plugins registry --refresh
```

Then install:

```bash
openclaw plugins install ./openclaw-twoegram-1.2.1.tgz --force --accept-capabilities
```

It should end with `Saved for the next Gateway start.` Leave the gateway
stopped until step 3 has set the channel up; step 3 starts it.

- `--force` confirms a source that is not on ClawHub.
- `--accept-capabilities` accepts that it is a channel.
- **Why stopped:** with the gateway running, the CLI asks the gateway to
  do the install instead. If the gateway refuses, the CLI retries the
  package as a hook pack, which needs the gateway stopped, and you see
  only that retry's error ("This operation requires exclusive offline
  state ownership"), not the gateway's reason. With the gateway stopped,
  any problem is reported as it is.
- **Updating** to a newer package: the same, with the new file.
- **If it already ended with that message:** the package is saved but
  the old folder still shadows it. Drop the load path and refresh as
  above, then run the install again; it ends `Installed plugin: a2`.
  Ignore its suggestion of `openclaw plugins reload a2`, which needs a
  running gateway; starting the gateway in step 3 loads the package.

## 3. Configure the channel

```bash
openclaw config set channels.a2 '{"host":"0.0.0.0","port":8417}' --strict-json
```

| Key | Meaning | Default |
|---|---|---|
| `host` | address the Apple connects to; the gateway host's LAN address is the tightest choice | `0.0.0.0`, every interface |
| `port` | | `8417` |
| `agents` | the agents the Apple may talk to, e.g. `["main"]` | all of the gateway's agents |
| `commands` | whether OpenClaw slash commands (`/status`, `/new`) typed on the Apple work | `true` |
| `enabled` | `false` turns the channel off | `true` |

The gateway itself stays on loopback; only port 8417 is on the LAN, with
its own small listener. Allow that port through the host's firewall for
the LAN only. **Never forward it to the internet.** Everything on it is
signed, but nothing is encrypted yet.

Then start the gateway:

```bash
openclaw gateway start
```

Its log should show a line like this, and `openclaw plugins list` should
show `a2` with no warning:

```
[a2] listening on http://0.0.0.0:8417/a2/ for 0 device(s); agents main
```

## 4. Add the Apple

The Apple makes its own key (docs/RELAY.md, part 1). On the Apple: start
Twoegram. The first time, it asks for a gateway: type a key name for
this gateway (such as `claw1`) and its address (for example
`192.168.1.20:8417`), and press Return. (Another gateway later: Settings,
Open-Apple-K, then Add a gateway.) The Apple shows its key for this
gateway and a device id. On the gateway host:

```bash
openclaw a2 add "Apple //e"
```

It asks for the key without showing it, and prints the device id, which
should be the one on the Apple's screen. Then press Return on the
Apple. No restart is needed: the running gateway sees new devices,
and revoked ones, on its next request.

```bash
openclaw a2 list
```

```bash
openclaw a2 revoke <device-id>
```

`revoke` also deletes that device's saved conversations.

**An Apple with Twoegram 1.0** (build b036 or earlier) cannot make keys:
its set-up screen takes one made by the gateway instead. From build b048
on, Twoegram no longer uses a key made by the gateway.

```bash
openclaw a2 pair "Apple //e"
```

This prints the key once, and the gateway's address. Plugin 1.2.0 keeps
`pair` for those Apples, and for a relay.

## 5. Check the gateway's security

```bash
openclaw security audit
```

It has no finding about the a2 channel itself. A plugin installed from a
package does show in two general findings, which apply to every
installed plugin: `plugins.extensions_no_allowlist` (set `plugins.allow`
to the plugin ids you trust, `a2` among them, and every other plugin you
use, or they stop loading) and `plugins.tools_reachable_permissive_policy`
(a2 gives agents no tools, so for a2 there is nothing to restrict).

## 6. Optional: try it from the Mac first

Pair a second device for the Python client, so the Apple's key stays
unseen:

```bash
openclaw a2 pair "Mac test"
```

```bash
A2_URL=http://GATEWAY-HOST:8417 A2_KEY=<that key> python3 tools/a2client.py chat main
```

Revoke it afterwards with `openclaw a2 revoke <id>`.

## For development: a linked folder

Instead of the package, OpenClaw can run the plugin's TypeScript
straight from a folder, so an update is an `rsync` and a gateway restart.
From the Mac:

```bash
rsync -a --exclude node_modules --exclude .dev --exclude dist ~/repos/twoegram/plugin/. GATEWAY-HOST:twoegram-plugin/
```

The `/.` at the end of the source means "the folder's contents". A
plain trailing slash means the same to rsync, but zsh removes a slash
that tab completion added as soon as you type a space, and then rsync
copies the folder itself, to `~/twoegram-plugin/plugin/`. Afterwards
`~/twoegram-plugin` should hold `package.json`, `openclaw.plugin.json`,
`index.ts` and `src/` directly. (`dist/`, the compiled copy, stays out:
an old one would be loaded in place of the newer source.)

With the gateway stopped:

```bash
openclaw plugins install -l ~/twoegram-plugin --force --accept-capabilities
```

OpenClaw's install scan checks the folder first. It refuses a
`node_modules` that points outside the plugin, which is why the `rsync`
leaves it out.

**If the folder moves**, OpenClaw quietly skips the plugin (the Apple
then gets `CONNECTION REFUSED`, and `openclaw plugins list` warns that a
load path is unavailable). With the gateway stopped, install again from
the new place, then drop the old path and refresh:

```bash
openclaw config set plugins.load.paths '["/home/you/twoegram-plugin"]' --strict-json
```

```bash
openclaw plugins registry --refresh
```

## How it fits into OpenClaw

- **Who may talk:** only paired devices. A request whose signature does
  not check out never reaches OpenClaw. In OpenClaw's terms the channel's
  DM policy is `allowlist`, and the allowlist is the device list.
- **What the Apple may do:** the Apple is not an owner
  (`commands.ownerAllowFrom`), so OpenClaw keeps its owner-only tools
  (`nodes`, `terminal`, `computer` and others) from agents answering it.
  Adding `a2:<device-id>` to that list changes this; see PLAN.md
  section 9, item 5, before doing so.
- **Sessions:** each device and agent pair is its own direct-message
  session, `agent:<agent>:a2:direct:<device>.<agent>`, whatever the
  gateway's `session.dmScope` is. Talking to `main` from the Apple does
  not share a transcript with `main` anywhere else.
- **Which agent:** the Apple's agent list is the gateway's agents, or
  `channels.a2.agents`. Agents added later show up at the Apple's next
  sign-in.
- **Names:** the Apple shows each agent's name as `openclaw agents list`
  does: `identity.name` in the agent's config, else the `Name:` line of
  `IDENTITY.md` in its workspace, else its id. Rename an agent there;
  the Apple picks the new name up at its next sign-in (Open-Apple-R).
- **The agent starting a conversation** (a reminder, a cron report): it
  can send to `a2:<device>.<agent>`. The message waits for the Apple's
  next poll.
- **Formatting:** the agent is told the reader has an 80-column
  plain-ASCII screen.
- **Where things are kept:**
  - `~/.openclaw/a2/devices.json`: the keys, mode 0600.
  - `~/.openclaw/a2/conversations/`: what the Apple scrolls through.
  - OpenClaw keeps its own transcript of each session as usual.
