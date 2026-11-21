# openclaw-twoegram: the a2 channel

An OpenClaw channel plugin that lets **Twoegram**, a chat client for the
Enhanced Apple //e with a CoGS network card, talk to your gateway's
agents over your home network. Every request and reply is signed with
HMAC-SHA256; nothing is encrypted yet, so keep it on your LAN and never
forward its port to the Internet.

Needs OpenClaw 2026.11.3 or later (before 2026.12) and Node 24.16+ or
26.1+. No npm dependencies.

## Install

With the gateway stopped:

```bash
openclaw plugins install ./openclaw-twoegram-1.2.2.tgz --force --accept-capabilities
```

```bash
openclaw config set channels.a2 '{"host":"0.0.0.0","port":8417}' --strict-json
```

```bash
openclaw gateway start
```

Then add the Apple. In Twoegram, add this gateway (at the first start,
or Settings, Open-Apple-K, then Add a gateway): a key name and its
address. The Apple shows its key for this gateway. On the gateway:

```bash
openclaw a2 add "Apple //e"
```

It asks for the key without showing it, and prints the device id, which
should match the Apple's. `openclaw a2 list` and `openclaw a2 revoke
<id>` manage the Apples it knows. `openclaw a2 pair` still makes a key
on the gateway, for an Apple with Twoegram 1.0 or for a relay.

## Settings (`channels.a2`)

| Key | Meaning | Default |
|---|---|---|
| `host` | the address to listen on; the gateway's LAN address is the tightest choice | `0.0.0.0` |
| `port` | | `8417` |
| `agents` | the agents the Apple may talk to, e.g. `["main"]` | all |
| `commands` | whether OpenClaw slash commands typed on the Apple work | `true` |
| `enabled` | `false` turns the channel off | `true` |

The full guide, the protocol and the Apple program are in the Twoegram
repository: `docs/INSTALL.md`, `docs/PROTOCOL.md`, `docs/MANUAL.md`.

MIT licensed.
