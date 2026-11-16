// The AgentPort inside OpenClaw: each message from the Apple goes to the
// agent it names, through the SDK's direct-message path, and the agent's
// reply comes back through `deliver`.
//
// This file imports nothing from OpenClaw. channel.ts hands it the SDK's
// dispatchInboundDirectDm and the gateway's config, so the tests can hand
// it a fake of each.
//
// Which agent: the Apple picks one by name. OpenClaw picks agents by
// bindings, so each message's config carries one more binding, first in
// the list, that sends this device's peer for this agent to that agent.
// The peer is `<device>.<agent>`, so each device and agent pair is its own
// direct-message session, with the per-channel-peer scope set on the
// binding itself: never the shared main session, whatever the gateway's
// own session.dmScope says.

import type { AgentPort, ReplySink } from "./agent.ts";
import { AGENT_NAME } from "./protocol.ts";

export const CHANNEL = "a2";
export const ACCOUNT = "default";

/** The few config fields read here; the real type is OpenClawConfig. */
export type Cfg = {
  agents?: { entries?: Record<string, unknown>; list?: { id?: string }[] };
  bindings?: unknown[];
  [k: string]: unknown;
};

export type ReplyPayload = { text?: string; isReasoning?: boolean; isError?: boolean };

/** The parameters of dispatchInboundDirectDm that are used here. */
export type DirectDmParams = {
  cfg: Cfg;
  channel: string;
  channelLabel: string;
  accountId: string;
  peer: { kind: "direct"; id: string };
  senderId: string;
  senderAddress: string;
  recipientAddress: string;
  conversationLabel: string;
  rawBody: string;
  messageId: string;
  timestamp?: number;
  commandAuthorized?: boolean;
  channelIngress?: "unsupported";
  inboundAccessAuthorized?: boolean;
  assertAuthority?: () => void;
  originatingChannel?: string;
  originatingTo?: string;
  deliver: (payload: ReplyPayload) => Promise<void>;
  onRecordError: (err: unknown) => void;
  onDispatchError: (err: unknown, info: { kind: string }) => void;
};

export type Dispatch = (params: DirectDmParams) => Promise<unknown>;

export type BridgeOptions = {
  cfg: () => Cfg;
  dispatch: Dispatch;
  /** Agents the Apple may talk to, from channels.a2.agents; all if absent. */
  allow?: string[];
  /** The gateway's own agent lister (the SDK's listAgentIds). */
  listAgents?: (cfg: Cfg) => string[];
  /** Whether the device is still paired: checked again before each effect. */
  paired: (device: string) => boolean;
  deviceName?: (device: string) => string | undefined;
  /** Whether OpenClaw's slash commands typed on the Apple are honoured. */
  commands?: boolean;
  log?: (line: string) => void;
  now?: () => number;
};

/**
 * The gateway's agents, as the Apple may name them. Inside the gateway
 * the SDK's own lister is used; without it (the tests), agents.entries
 * keys, or the legacy agents.list ids.
 */
export function agentIds(cfg: Cfg, allow?: string[], list?: (cfg: Cfg) => string[]): string[] {
  const raw = list
    ? list(cfg)
    : cfg.agents?.entries
      ? Object.keys(cfg.agents.entries)
      : (cfg.agents?.list ?? []).map((a) => String(a?.id ?? ""));
  const listed = raw.map((id) => id.toLowerCase()).filter((id) => id);
  const all = listed.length ? listed : ["main"]; // OpenClaw's agent when none is listed
  const wanted = allow?.length ? new Set(allow.map((a) => a.toLowerCase())) : null;
  const out: string[] = [];
  for (const id of all) {
    if (!AGENT_NAME.test(id) || out.includes(id)) continue;
    if (wanted && !wanted.has(id)) continue;
    out.push(id);
  }
  return out;
}

export function peerOf(device: string, agent: string): string {
  return `${device}.${agent}`;
}

/** `a2:1a2b3c4d.main`, `1a2b3c4d.main` or `1a2b3c4d` (the first agent). */
export function parseTarget(to: string, agents: string[]): { device: string; agent: string } | null {
  const m = /^(?:a2:)?([0-9a-f]{8})(?:\.([a-z0-9_-]{1,16}))?$/.exec(to.trim().toLowerCase());
  if (!m) return null;
  const agent = m[2] ?? agents[0];
  if (!agent || !agents.includes(agent)) return null;
  return { device: m[1], agent };
}

/** The config for one message: the gateway's, with this pair's binding first. */
export function routedCfg(cfg: Cfg, device: string, agent: string): Cfg {
  const binding = {
    agentId: agent,
    match: { channel: CHANNEL, accountId: ACCOUNT, peer: { kind: "direct", id: peerOf(device, agent) } },
    session: { dmScope: "per-channel-peer" },
  };
  return { ...cfg, bindings: [binding, ...(Array.isArray(cfg.bindings) ? cfg.bindings : [])] };
}

export class OpenClawAgents implements AgentPort {
  private o: BridgeOptions;
  private count = 0;

  constructor(options: BridgeOptions) {
    this.o = options;
  }

  agents(): string[] {
    return agentIds(this.o.cfg(), this.o.allow, this.o.listAgents);
  }

  deliver(device: string, agent: string, text: string, out: ReplySink): void {
    const log = this.o.log ?? (() => {});
    const now = (this.o.now ?? Date.now)();
    const peer = peerOf(device, agent);
    const name = this.o.deviceName?.(device) ?? "Apple //e";
    const paired = () => {
      if (!this.o.paired(device)) throw new Error(`a2: device ${device} is no longer paired`);
    };
    let failed = false;
    const failNote = (why: string) => {
      if (failed) return;
      failed = true;
      out.text(`(The gateway could not answer: ${why})`);
    };

    let run: Promise<unknown>;
    try {
      run = this.o.dispatch({
        cfg: routedCfg(this.o.cfg(), device, agent),
        channel: CHANNEL,
        channelLabel: "Apple //e",
        accountId: ACCOUNT,
        peer: { kind: "direct", id: peer },
        senderId: device, // matches the allowlist: the device list
        senderAddress: `${CHANNEL}:${peer}`,
        recipientAddress: `${CHANNEL}:${agent}`,
        conversationLabel: `${name} (${device})`,
        rawBody: text,
        messageId: `${peer}.${now.toString(36)}.${++this.count}`,
        timestamp: now,
        commandAuthorized: this.o.commands ?? true,
        // The device proved itself with its key before this was called;
        // this channel's allowlist is its device list.
        channelIngress: "unsupported",
        inboundAccessAuthorized: true,
        assertAuthority: paired,
        originatingChannel: CHANNEL,
        originatingTo: `${CHANNEL}:${peer}`,
        deliver: async (payload) => {
          paired();
          if (payload.isReasoning) return;
          const t = payload.text ?? "";
          if (t.trim()) out.text(t);
        },
        onRecordError: (err) => log(`a2: session record failed for ${peer}: ${String(err)}`),
        onDispatchError: (err, info) => {
          log(`a2: ${info.kind} reply failed for ${peer}: ${String(err)}`);
          failNote(short(err));
        },
      });
    } catch (err) {
      run = Promise.reject(err);
    }
    run.then(
      () => out.done(),
      (err) => {
        log(`a2: dispatch failed for ${peer}: ${String(err)}`);
        failNote(short(err));
        out.done();
      },
    );
  }
}

function short(err: unknown): string {
  const s = err instanceof Error ? err.message : String(err);
  return s.length > 200 ? s.slice(0, 197) + "..." : s;
}
