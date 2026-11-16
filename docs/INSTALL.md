# Installing the a2 channel on your OpenClaw gateway

The `a2` plugin lets Twoegram on the Apple //e talk to your gateway's
agents. It needs OpenClaw 2026.11.3 and Node 24.16+ or 26.1+, which your
gateway already has. It has no npm dependencies.

It was tested in a scratch OpenClaw 2026.11.3 on the Mac (an isolated home
directory, gateway on loopback, no model provider). That run covered
install, start-up, pairing, sign-in, sending to two agents, replies coming
back, slash commands, revoking, and history surviving a restart. What it
could not cover is a real model's answers: that needs your gateway.

## 1. Copy the plugin to the gateway host

Only the `plugin/` folder is needed, without `node_modules`. From the Mac:

```bash
rsync -a --exclude node_modules --exclude .dev ~/repos/twoegram/plugin/. GATEWAY-HOST:twoegram-plugin/
```

The `/.` at the end of the source means "the folder's contents". A
plain trailing slash means the same to rsync, but zsh removes a slash
that tab completion added as soon as you type a space, and then rsync
copies the folder itself, to `~/twoegram-plugin/plugin/`. Afterwards
`~/twoegram-plugin` should hold `package.json`, `openclaw.plugin.json`,
`index.ts` and `src/` directly.

(Or `git clone` the repository on the gateway host. The plugin is then
the clone's `plugin/` folder, so give that path to the install below.)

## 2. Install and enable it

On the gateway host, with the gateway stopped:

```bash
openclaw gateway stop
```

```bash
openclaw plugins install -l ~/twoegram-plugin --force --accept-capabilities
```

It should end with `Saved for the next Gateway start.` Leave the gateway
stopped until step 3 has set the channel up; step 3 starts it.

With the gateway running, the CLI asks the gateway to do the install
instead. If the gateway refuses, the CLI retries the folder as a hook
pack, which needs the gateway stopped, and you see only that retry's
error ("This operation requires exclusive offline state ownership"),
not the gateway's reason. With the gateway stopped, any problem is
reported as it is.

- `-l` links the folder rather than copying it, so a later `rsync` and a
  gateway restart is all an update needs. Keep the folder where it is:
  if it moves, OpenClaw quietly skips the plugin (the Apple then gets
  `CONNECTION REFUSED`, and `openclaw plugins list` warns that a load
  path is unavailable). To link the new place, with the gateway
  stopped, install again from it, then drop the old path and refresh:

  ```bash
  openclaw config set plugins.load.paths '["/home/you/twoegram-plugin"]' --strict-json
  ```

  ```bash
  openclaw plugins registry --refresh
  ```
- `--force` confirms a source that is not on ClawHub.
- `--accept-capabilities` accepts that it is a channel.

OpenClaw's install scan checks the folder first. It refuses a
`node_modules` that points outside the plugin, which is why step 1 leaves
it out.

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

(Later, after an `rsync` of a new version, `openclaw gateway restart`
is enough.)

Its log should show a line like this:

```
[a2] listening on http://0.0.0.0:8417/a2/ for 0 device(s); agents main
```

## 4. Pair the Apple

```bash
openclaw a2 pair "Apple //e"
```

It prints the device's key once, in groups of five characters, and the
gateway address to type in. No restart is needed: the running gateway
sees new devices, and revoked ones, on its next request.

```bash
openclaw a2 list
```

```bash
openclaw a2 revoke <device-id>
```

`revoke` also deletes that device's saved conversations.

On the Apple: start Twoegram, press Open-Apple-K, and enter the gateway
address (for example `http://192.168.1.20:8417`) and the new key. The key
replaces the dev server's key in the card's flash.

## 5. Optional: try it from the Mac first

Pair a second device for the Python client, so the Apple's key stays
unseen:

```bash
openclaw a2 pair "Mac test"
```

```bash
A2_URL=http://GATEWAY-HOST:8417 A2_KEY=<that key> python3 tools/a2client.py chat main
```

Revoke it afterwards with `openclaw a2 revoke <id>`.

## How it fits into OpenClaw

- **Who may talk:** only paired devices. A request whose signature does
  not check out never reaches OpenClaw. In OpenClaw's terms the channel's
  DM policy is `allowlist`, and the allowlist is the device list.
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
