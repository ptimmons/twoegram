# Twoegram for Hermes Agent

The a2 platform for [Hermes Agent](https://github.com/NousResearch/hermes-agent):
chat with Hermes from an Apple //e running Twoegram, over your LAN. It
speaks exactly the protocol of the OpenClaw plugin ([docs/PROTOCOL.md](../docs/PROTOCOL.md)),
so the same Twoegram disk talks to either gateway.

- **Standard library only.** It needs no packages beyond Hermes itself.
- **Signed, not encrypted.** Every request and reply is signed with the
  device's key; anyone on the LAN can read the traffic (PLAN.md section 2).
- **LAN only.** Never forward its port to the internet.

Built against Hermes Agent v0.23.0, and tested again on v0.23.2 (commit `ae2a81ccc0`), the stable release the Mac mini runs.

## Install

On the machine running Hermes:

1. **Copy the plugin** into the profile's plugin folder:

   ```bash
   scp -r hermes/twoegram you@hermes-host:.hermes/plugins/twoegram
   ```

2. **Enable it**, and turn the platform on:

   ```bash
   hermes plugins enable twoegram
   ```

   Then add this to `~/.hermes/config.yaml`, inside its `gateway:`
   section. If there is no `gateway:` section yet, add one.

   ```yaml
   gateway:
     platforms:
       a2:
         enabled: true
   ```

3. **Restart the gateway**:

   ```bash
   hermes gateway restart
   ```

   Its log should then say `a2: listening on http://0.0.0.0:8417/a2/`.

4. **Pair the Apple**:

   ```bash
   hermes a2 pair "Apple IIe"
   ```

   This prints the key, once, and the address to type into Twoegram's
   set-up (Open-Apple-K). There is no need to restart the gateway: a
   device paired or revoked takes effect at its next request.

To check and manage devices:

```bash
hermes a2 list
```

```bash
hermes a2 revoke <id>
```

A revoked device's conversations are deleted with it.

## Settings

These are all optional. Set them in `~/.hermes/.env`, or under
`gateway.platforms.a2.extra` in `config.yaml` with the key in brackets.

| Setting | Default | |
|---|---|---|
| `A2_HOST` (`host`) | `0.0.0.0` | address to listen on |
| `A2_PORT` (`port`) | `8417` | the port the Apple connects to |
| `A2_AGENT` (`agent`) | `hermes` | the agent's id on the Apple: 1 to 16 of `a-z 0-9 - _` |
| `A2_AGENT_NAME` (`name`) | `Hermes` | what the Apple calls it |
| `A2_HOME_CHANNEL` | | `<device>.<agent>` for scheduled messages; typing `/sethome` on the Apple does the same |

If Hermes and OpenClaw run on the same machine, give one of them another
port.

## How it fits into Hermes

| | |
|---|---|
| Who may talk | Only paired devices. A request reaches Hermes only after its signature checks, and the device list is the adapter's allowlist (`allow_from`). If you set `GATEWAY_ALLOWED_USERS`, add the device ids to it too. |
| Conversations | One per device: Hermes's session `agent:<profile>:a2:dm:<device>.<agent>`. The plugin also keeps what the Apple scrolls through, up to 500 messages, in `~/.hermes/platforms/a2/conversations/`. |
| Keys | `~/.hermes/platforms/a2/devices.json`, readable only by its owner. It uses the same format as the OpenClaw plugin's file. |
| Answers | Hermes sends one message per finished answer. The plugin folds it to ASCII and cuts it into parts for the Apple. The platform asks for Hermes's `low` display tier, so tool progress, streaming and interim messages stay off a screen that cannot edit what it has drawn. |
| Busy | The Apple's `busy 1` runs from the message arriving until Hermes's processing hook says the turn is over. |
| Approvals | Text: the user types `/approve` or `/deny`. |
| Scheduled messages | Delivered to the home channel, `<device>.<agent>`. |

Files:

| File | |
|---|---|
| `twoegram/protocol.py` | keys, signatures, replies; a port of `plugin/src/protocol.ts` |
| `twoegram/text.py`, `tables.py` | folding to what the //e can draw; a port of `text.ts` |
| `twoegram/store.py` | the device and conversation files |
| `twoegram/server.py` | the `/a2/` endpoints, and a small HTTP/1.1 listener |
| `twoegram/adapter.py` | the Hermes platform adapter and its registration |
| `twoegram/cli.py` | `hermes a2 pair`, `list` and `revoke` |
| `twoegram/echo.py` | the echo agent, for the tests and the dev server |

## Development

The tests need Python 3.11 or later, with pytest and pytest-asyncio. The
adapter tests also need Hermes's source. `make hermes-test` uses a clone in
`.sdk/hermes-agent` and a virtualenv in `.sdk/hermes-venv`. To set these up:

```bash
make hermes-sdk
```

Without Hermes's source, the protocol and server tests still run.

- **Shared vectors.** `tests/vectors.json` holds what the TypeScript
  plugin computes for a fixed set of keys, signatures and texts, so the
  two implementations are checked against the same bytes. Remake it after
  changing `plugin/src/protocol.ts` or `text.ts`:

  ```bash
  make hermes-vectors
  ```

- **Character tables.** `tables.py` is generated from `text.ts` by
  `tools/tables.mjs`.

- **Without Hermes.** `tools/devserver.py` runs the server with the echo
  agent, for trying a client, such as `tools/a2client.py` or the Apple,
  with no Hermes at all.
