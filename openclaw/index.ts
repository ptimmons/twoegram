// The a2 channel plugin: Twoegram, an Apple //e client, talking to this
// gateway's agents over the LAN with every request and reply signed.
// See ../docs/PROTOCOL.md and ../PLAN.md.

import { defineChannelPluginEntry } from "openclaw/plugin-sdk/channel-core";
import { resolveStateDir } from "openclaw/plugin-sdk/state-paths";
import { a2Plugin, resolveAccount } from "./src/channel.ts";
import { registerA2Commands, type Command } from "./src/commands.ts";

export default defineChannelPluginEntry({
  id: "a2",
  name: "Apple //e (Twoegram)",
  description: "Chat with your agents from an Apple //e running Twoegram",
  plugin: a2Plugin,
  registerCliMetadata(api) {
    api.registerCli(
      ({ program, config }) => {
        registerA2Commands(program as unknown as Command, {
          stateDir: () => resolveStateDir(),
          listen: () => {
            const a = resolveAccount(config);
            return { host: a.host, port: a.port };
          },
        });
      },
      { descriptors: [{ name: "a2", description: "Apple //e devices for the a2 channel (Twoegram)", hasSubcommands: true }] },
    );
  },
});
