// The bridge to OpenClaw, with a fake of the SDK's dispatchInboundDirectDm.

import { describe, expect, it } from "vitest";
import type { ReplySink } from "../src/agent.ts";
import { OpenClawAgents, agentIds, parseTarget, routedCfg, type Cfg, type DirectDmParams } from "../src/openclaw-agent.ts";

function sink() {
  const texts: string[] = [];
  let done!: () => void;
  const finished = new Promise<void>((r) => (done = r));
  const out: ReplySink = { text: (t) => texts.push(t), done: () => done() };
  return { out, texts, finished };
}

describe("which agents the Apple sees", () => {
  it("is main when the gateway lists none", () => {
    expect(agentIds({})).toEqual(["main"]);
  });

  it("is the keys of agents.entries", () => {
    expect(agentIds({ agents: { entries: { main: {}, coder: {} } } })).toEqual(["main", "coder"]);
  });

  it("is what the gateway's own lister says, when given one", () => {
    expect(agentIds({}, undefined, () => ["ops", "main"])).toEqual(["ops", "main"]);
  });

  it("is the legacy list, lowercased, without names the protocol cannot carry", () => {
    const cfg: Cfg = { agents: { list: [{ id: "Main" }, { id: "coder" }, { id: "has space" }, { id: "x".repeat(17) }, { id: "main" }] } };
    expect(agentIds(cfg)).toEqual(["main", "coder"]);
  });

  it("is only the allowed ones when channels.a2.agents says", () => {
    const cfg: Cfg = { agents: { list: [{ id: "main" }, { id: "coder" }, { id: "ops" }] } };
    expect(agentIds(cfg, ["coder", "nope"])).toEqual(["coder"]);
  });
});

describe("routing", () => {
  it("puts this device and agent's binding first, keeping the gateway's own", () => {
    const theirs = { agentId: "main", match: { channel: "telegram" } };
    const cfg = routedCfg({ bindings: [theirs], session: { dmScope: "main" } }, "1a2b3c4d", "coder");
    expect(cfg.bindings).toEqual([
      {
        agentId: "coder",
        match: { channel: "a2", accountId: "default", peer: { kind: "direct", id: "1a2b3c4d.coder" } },
        session: { dmScope: "per-channel-peer" },
      },
      theirs,
    ]);
    expect(cfg.session).toEqual({ dmScope: "main" }); // the gateway's own, untouched
  });

  it("reads targets for messages the agent starts", () => {
    const agents = ["main", "coder"];
    expect(parseTarget("a2:1a2b3c4d.coder", agents)).toEqual({ device: "1a2b3c4d", agent: "coder" });
    expect(parseTarget("1A2B3C4D", agents)).toEqual({ device: "1a2b3c4d", agent: "main" });
    expect(parseTarget("1a2b3c4d.ops", agents)).toBeNull();
    expect(parseTarget("telegram:12345", agents)).toBeNull();
  });
});

describe("a message to an agent", () => {
  function bridge(dispatch: (p: DirectDmParams) => Promise<unknown>, paired = () => true) {
    return new OpenClawAgents({
      cfg: () => ({ agents: { list: [{ id: "main" }, { id: "coder" }] } }),
      dispatch,
      paired,
      deviceName: () => "Kitchen //e",
      now: () => Date.UTC(2026, 10, 15, 20, 0),
    });
  }

  it("goes in as a direct message from its own peer, and the reply comes back", async () => {
    let seen: DirectDmParams | undefined;
    const b = bridge(async (p) => {
      seen = p;
      await p.deliver({ text: "thinking about it", isReasoning: true });
      await p.deliver({ text: "Here you are." });
      await p.deliver({ text: "   " });
    });
    const s = sink();
    b.deliver("1a2b3c4d", "coder", "hello", s.out);
    await s.finished;
    expect(s.texts).toEqual(["Here you are."]);
    expect(seen).toMatchObject({
      channel: "a2",
      accountId: "default",
      peer: { kind: "direct", id: "1a2b3c4d.coder" },
      senderId: "1a2b3c4d",
      rawBody: "hello",
      conversationLabel: "Kitchen //e (1a2b3c4d)",
      inboundAccessAuthorized: true,
      channelIngress: "unsupported",
      commandAuthorized: true,
      originatingTo: "a2:1a2b3c4d.coder",
    });
    expect(seen!.cfg.bindings![0]).toMatchObject({ agentId: "coder" });
  });

  it("gives each message its own id", async () => {
    const ids: string[] = [];
    const b = bridge(async (p) => void ids.push(p.messageId));
    for (let i = 0; i < 2; i++) {
      const s = sink();
      b.deliver("1a2b3c4d", "main", "x", s.out);
      await s.finished;
    }
    expect(new Set(ids).size).toBe(2);
  });

  it("tells the Apple when the gateway fails, and still finishes", async () => {
    const b = bridge(async () => {
      throw new Error("no model configured");
    });
    const s = sink();
    b.deliver("1a2b3c4d", "main", "hi", s.out);
    await s.finished;
    expect(s.texts).toEqual(["(The gateway could not answer: no model configured)"]);
  });

  it("says so once when a reply cannot be delivered", async () => {
    const b = bridge(async (p) => {
      p.onDispatchError(new Error("boom"), { kind: "final" });
      p.onDispatchError(new Error("boom again"), { kind: "final" });
    });
    const s = sink();
    b.deliver("1a2b3c4d", "main", "hi", s.out);
    await s.finished;
    expect(s.texts).toEqual(["(The gateway could not answer: boom)"]);
  });

  it("stops at the next step when the device is revoked meanwhile", async () => {
    let paired = true;
    const b = bridge(async (p) => {
      p.assertAuthority!();
      paired = false;
      p.assertAuthority!(); // the host's next check refuses
    }, () => paired);
    const s = sink();
    b.deliver("1a2b3c4d", "main", "hi", s.out);
    await s.finished;
    expect(s.texts[0]).toMatch(/no longer paired/);
  });
});
