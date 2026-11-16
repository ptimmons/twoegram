// The a2 channel as OpenClaw sees it. Everything specific to the Apple
// is in server.ts (the protocol) and openclaw-agent.ts (the bridge); this
// file is the part that imports the SDK, and is only loaded inside the
// gateway.
//
// Config, under channels.a2:
//   host      address to listen on (default 0.0.0.0: every interface)
//   port      default 8417
//   agents    the agents the Apple may talk to (default: all)
//   commands  whether OpenClaw slash commands typed on the Apple work (default true)
//   enabled   false to turn the channel off
//
// The listener is its own small HTTP server, not a route on the gateway,
// so the gateway itself can stay on loopback.

import { readFileSync } from "node:fs";
import { createServer, type Server } from "node:http";
import { createChatChannelPlugin } from "openclaw/plugin-sdk/channel-core";
import type { OpenClawConfig } from "openclaw/plugin-sdk/channel-core";
import { dispatchInboundDirectDm } from "openclaw/plugin-sdk/channel-inbound";
import { listAgentIds } from "openclaw/plugin-sdk/agent-scope-runtime";
import { resolveAgentWorkspaceDir } from "openclaw/plugin-sdk/agent-runtime";
import { readRuntimeConfig } from "openclaw/plugin-sdk/runtime-config-snapshot";
import { resolveStateDir } from "openclaw/plugin-sdk/state-paths";
import { DEFAULT_PORT, conversationsDir, devicesPath } from "./commands.ts";
import { FileConvStore } from "./convstore.ts";
import { DeviceStore } from "./devices.ts";
import { ACCOUNT, CHANNEL, OpenClawAgents, agentIds, parseTarget, type Cfg, type Dispatch } from "./openclaw-agent.ts";
import { A2Server } from "./server.ts";

export type A2Account = {
  accountId: string;
  enabled: boolean;
  configured: boolean;
  host: string;
  port: number;
  agents?: string[];
  commands: boolean;
};

type Section = { enabled?: boolean; host?: string; port?: number; agents?: string[]; commands?: boolean };

function section(cfg: OpenClawConfig): Section | undefined {
  const s = (cfg.channels as Record<string, unknown> | undefined)?.[CHANNEL];
  return s && typeof s === "object" ? (s as Section) : undefined;
}

export function resolveAccount(cfg: OpenClawConfig, accountId?: string | null): A2Account {
  const s = section(cfg);
  return {
    accountId: accountId ?? ACCOUNT,
    enabled: s?.enabled !== false,
    configured: !!s,
    host: s?.host || "0.0.0.0",
    port: s?.port ?? DEFAULT_PORT,
    agents: s?.agents,
    commands: s?.commands !== false,
  };
}

// package.json is one folder up from src/channel.ts, and two from the
// compiled dist/src/channel.js in the package.
const VERSION = (() => {
  for (const up of ["../package.json", "../../package.json"]) {
    try {
      const pkg = JSON.parse(readFileSync(new URL(up, import.meta.url), "utf8"));
      if (pkg.name === "openclaw-twoegram") return pkg.version as string;
    } catch {
      // not there: the next
    }
  }
  return "?";
})();

const listAgents = (cfg: Cfg) => listAgentIds(cfg as unknown as OpenClawConfig);

/** The running listener, for sends the agent starts itself. */
let active: A2Server | null = null;

const FORMATTING = {
  text_markup: "plain text",
  rules: [
    "The reader is on an Apple //e: an 80-column text screen showing plain ASCII only.",
    "Markdown is shown as plain text; code blocks are kept line for line. Prefer short paragraphs and short lines.",
    "No images, files, links to click, buttons or reactions: everything must be readable as text.",
    "Non-ASCII characters and emoji are replaced, so prefer plain ASCII.",
  ],
};

export const a2Plugin = createChatChannelPlugin<A2Account>({
  base: {
    id: CHANNEL,
    meta: {
      id: CHANNEL,
      label: "Apple //e (Twoegram)",
      selectionLabel: "Apple //e",
      docsPath: "/channels/a2",
      blurb: "Chat with your agents from an Apple //e running Twoegram, over the LAN, signed.",
      markdownCapable: false,
    },
    capabilities: { chatTypes: ["direct"] },
    // A change to channels.a2 (address, port, agents) restarts the listener.
    reload: { configPrefixes: [`channels.${CHANNEL}`] },
    config: {
      listAccountIds: () => [ACCOUNT],
      defaultAccountId: () => ACCOUNT,
      resolveAccount,
      inspectAccount: (cfg) => {
        const a = resolveAccount(cfg);
        return { enabled: a.enabled, configured: a.configured };
      },
      isEnabled: (a) => a.enabled,
      isConfigured: (a) => a.configured,
    },
    agentPrompt: { inboundFormattingHints: () => FORMATTING },
    messaging: {
      normalizeTarget: (raw) => {
        const m = /^(?:a2:)?([0-9a-f]{8}(?:\.[a-z0-9_-]{1,16})?)$/.exec(raw.trim().toLowerCase());
        return m ? `${CHANNEL}:${m[1]}` : undefined;
      },
      targetResolver: {
        looksLikeId: (raw) => /^(?:a2:)?[0-9a-f]{8}(?:\.[a-z0-9_-]{1,16})?$/i.test(raw.trim()),
        hint: "<device>.<agent>, e.g. 1a2b3c4d.main (openclaw a2 list shows devices)",
      },
    },
    gateway: {
      startAccount: async (ctx) => {
        const account = ctx.account;
        const log = (line: string) => ctx.log?.info(line);
        const dir = resolveStateDir();
        const devices = new DeviceStore(devicesPath(dir));
        const bridge = new OpenClawAgents({
          // The live config, so agents added or removed apply at once;
          // what the channel started with if it cannot be read.
          cfg: () => {
            try {
              return readRuntimeConfig() as unknown as Cfg;
            } catch {
              return ctx.cfg as unknown as Cfg;
            }
          },
          dispatch: dispatchInboundDirectDm as unknown as Dispatch,
          allow: account.agents,
          listAgents,
          workspaceDir: (cfg, agent) => resolveAgentWorkspaceDir(cfg as unknown as OpenClawConfig, agent),
          paired: (id) => !!devices.get(id),
          deviceName: (id) => devices.get(id)?.name,
          commands: account.commands,
          log,
        });
        const server = new A2Server({
          devices,
          agents: bridge,
          convs: new FileConvStore(conversationsDir(dir)),
          log,
          ident: `openclaw-a2 ${VERSION}`,
        });
        const http: Server = createServer((req, res) => {
          if (!req.url?.startsWith("/a2/")) {
            res.writeHead(404).end();
            return;
          }
          void server.handle(req, res);
        });
        http.keepAliveTimeout = 10_000; // longer than the card's 8 s cutoff
        await new Promise<void>((resolve, reject) => {
          http.once("error", reject);
          http.listen(account.port, account.host, () => {
            http.off("error", reject);
            resolve();
          });
        });
        active = server;
        const agents = bridge.agents();
        log(`a2: listening on http://${account.host}:${account.port}/a2/ for ${devices.list().length} device(s); agents ${agents.join(", ") || "(none)"}`);
        if (!agents.length) ctx.log?.warn("a2: no agent the Apple may talk to; check channels.a2.agents");
        ctx.setStatus({ ...ctx.getStatus(), running: true, connected: true, lastStartAt: Date.now(), lastError: null });
        try {
          await new Promise<void>((resolve) => {
            if (ctx.abortSignal.aborted) resolve();
            else ctx.abortSignal.addEventListener("abort", () => resolve(), { once: true });
          });
        } finally {
          if (active === server) active = null;
          http.closeAllConnections();
          await new Promise<void>((resolve) => http.close(() => resolve()));
          ctx.setStatus({ ...ctx.getStatus(), running: false, connected: false, lastStopAt: Date.now() });
          log("a2: stopped");
        }
      },
    },
  },
  security: {
    dm: {
      channelKey: CHANNEL,
      // The device list is the allowlist; it is never open.
      resolvePolicy: () => "allowlist",
      resolveAllowFrom: () => {
        try {
          return new DeviceStore(devicesPath(resolveStateDir())).list().map((d) => d.id);
        } catch {
          return [];
        }
      },
      defaultPolicy: "allowlist",
    },
  },
  outbound: {
    base: { deliveryMode: "direct", textChunkLimit: 1500 },
    attachedResults: {
      channel: CHANNEL,
      sendText: async (params) => {
        const server = active;
        if (!server) throw new Error("a2: the channel is not running");
        const cfg = params.cfg as unknown as Cfg;
        const target = parseTarget(params.to, agentIds(cfg, resolveAccount(params.cfg).agents, listAgents));
        if (!target) throw new Error(`a2: not a device and agent: ${params.to}`);
        const n = server.post(target.device, target.agent, params.text);
        return { messageId: `${target.device}.${target.agent}.${n}` };
      },
    },
  },
});
