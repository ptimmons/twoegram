// The endpoints over real HTTP, with a client that does what the Apple
// will: verify every reply before believing it.

import { randomBytes } from "node:crypto";
import { existsSync, mkdtempSync, readdirSync, statSync } from "node:fs";
import { createServer, request, type Server } from "node:http";
import type { AddressInfo } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { EchoAgent, type AgentPort, type ReplySink } from "../src/agent.ts";
import { FileConvStore } from "../src/convstore.ts";
import { DeviceStore } from "../src/devices.ts";
import {
  Code,
  MAX_PAYLOAD,
  deviceId,
  helloMac,
  helloProof,
  macEquals,
  parseKey,
  parseReply,
  replyMac,
  requestMac,
  sessionKey,
  type Parsed,
} from "../src/protocol.ts";
import { A2Server, displayLabel } from "../src/server.ts";

type Raw = { status: number; body: Buffer };

let http: Server;
let base: string;
let store: DeviceStore;
let a2: A2Server;
let clock: number;

function rawRequest(method: string, target: string, headers: Record<string, string> = {}, body = Buffer.alloc(0)): Promise<Raw> {
  return new Promise((resolve, reject) => {
    const r = request(`${base}${target}`, { method, headers: { ...headers, "Content-Length": body.length } }, (res) => {
      const chunks: Buffer[] = [];
      res.on("data", (c) => chunks.push(c));
      res.on("end", () => resolve({ status: res.statusCode ?? 0, body: Buffer.concat(chunks) }));
    });
    r.on("error", reject);
    r.end(body);
  });
}

/** The Apple's side of the protocol. */
class Client {
  key: Buffer;
  id: string;
  sid = "";
  sk: Buffer = Buffer.alloc(0);
  seq = 0;
  agents: string[] = [];

  constructor(key: Buffer) {
    this.key = key;
    this.id = deviceId(key);
  }

  async hello(): Promise<Parsed> {
    const cn = randomBytes(16);
    const proof = helloProof(this.key, cn).toString("hex");
    const raw = await rawRequest("GET", `/a2/hello?d=${this.id}&c=${cn.toString("hex")}&m=${proof}`);
    const r = parseReply(raw.body);
    if (r.code !== Code.OK) return r;
    const get = (w: string) => r.lines.find(([k]) => k === w)?.[1] ?? "";
    const sk = sessionKey(this.key, cn, Buffer.from(get("sn"), "hex"));
    if (!macEquals(helloMac(sk, r.payload), r.mac ?? "")) throw new Error("hello reply not genuine");
    this.sk = sk;
    this.sid = get("sid");
    this.seq = 0;
    this.agents = r.lines.filter(([k]) => k === "agent").map(([, v]) => v);
    return r;
  }

  /** A signed request; the reply is checked against this seq. */
  async call(method: string, target: string, body = "", opts: { seq?: number; tamper?: boolean } = {}): Promise<Parsed & { genuine: boolean }> {
    const seq = opts.seq ?? ++this.seq;
    const b = Buffer.from(body, "latin1");
    let mac = requestMac(this.sk, seq, method, target, b).toString("hex");
    if (opts.tamper) mac = (mac[0] === "0" ? "1" : "0") + mac.slice(1);
    const raw = await rawRequest(method, target, { "X-A2-Session": this.sid, "X-A2-Seq": String(seq), "X-A2-Mac": mac }, b);
    expect(raw.status).toBe(200);
    const r = parseReply(raw.body);
    const genuine = r.mac !== null && macEquals(replyMac(this.sk, seq, r.payload), r.mac);
    return { ...r, genuine };
  }
}

function pair(): Buffer {
  return parseKey(store.pair("test").written)!;
}

let dir: string;

async function start(agents: AgentPort = new EchoAgent(["main", "coder"], 20), opts: { keep?: boolean; convs?: boolean } = {}) {
  if (!opts.keep) dir = mkdtempSync(join(tmpdir(), "a2-"));
  store = new DeviceStore(join(dir, "devices.json"));
  clock = Date.UTC(2026, 10, 15, 14, 1);
  const convs = opts.convs ? new FileConvStore(join(dir, "conversations")) : undefined;
  a2 = new A2Server({ devices: store, agents, now: () => clock, convs });
  http = createServer((req, res) => void a2.handle(req, res));
  await new Promise<void>((r) => http.listen(0, "127.0.0.1", r));
  base = `http://127.0.0.1:${(http.address() as AddressInfo).port}`;
}

afterEach(async () => {
  http.closeAllConnections();
  await new Promise((r) => http.close(r));
});

describe("pairing and hello", () => {
  beforeEach(() => start());

  it("keeps keys in a file only its owner can read", () => {
    pair();
    expect(statSync(store.path).mode & 0o777).toBe(0o600);
  });

  it("starts a session the client can verify, with the agent list", async () => {
    const c = new Client(pair());
    const r = await c.hello();
    expect(r.code).toBe(Code.OK);
    expect(c.sid).toMatch(/^[0-9a-f]{8}$/);
    expect(c.agents).toEqual(["main", "coder"]);
    // The echo agent calls main "Echo"; coder has no name of its own.
    expect(r.lines.filter(([k]) => k === "name").map(([, v]) => v)).toEqual(["main Echo"]);
  });

  it("sends each agent's name as the //e can show it", async () => {
    http.closeAllConnections();
    await new Promise((r) => http.close(r));
    await start(
      new EchoAgent(["main", "coder", "home", "same"], 20, {
        main: "Pinchy ✨",
        coder: "  Señor\nCódigo  the Magnificent",
        home: "🏠",
        same: "same",
      }),
    );
    const c = new Client(pair());
    const r = await c.hello();
    expect(r.lines.filter(([k]) => k === "name").map(([, v]) => v)).toEqual([
      "main Pinchy",
      "coder Senor Codigo the",
    ]);
  });

  it("refuses an unknown device and a wrong proof, unsigned", async () => {
    const stranger = new Client(randomBytes(16));
    const r1 = await stranger.hello();
    expect(r1.code).toBe(Code.UNKNOWN_DEVICE);
    expect(r1.mac).toBeNull();

    const real = pair();
    const impostor = new Client(randomBytes(16));
    impostor.id = deviceId(real); // knows the id, not the key
    const r2 = await impostor.hello();
    expect(r2.code).toBe(Code.BAD_SIGNATURE);
    expect(r2.mac).toBeNull();
  });

  it("locks a device after five bad proofs, and lets it in again later", async () => {
    const real = pair();
    const impostor = new Client(randomBytes(16));
    impostor.id = deviceId(real);
    for (let i = 0; i < 5; i++) await impostor.hello();
    const good = new Client(real);
    expect((await good.hello()).code).toBe(Code.LOCKED);
    clock += 16 * 60_000;
    expect((await good.hello()).code).toBe(Code.OK);
  });

  it("blocks an address after twenty refusals", async () => {
    for (let i = 0; i < 20; i++) await new Client(randomBytes(16)).hello();
    const good = new Client(pair());
    expect((await good.hello()).code).toBe(Code.LOCKED);
    clock += 6 * 60_000;
    expect((await good.hello()).code).toBe(Code.OK);
  });

  it("keeps at most four sessions a device", async () => {
    const key = pair();
    const first = new Client(key);
    await first.hello();
    for (let i = 0; i < 4; i++) {
      clock += 1000;
      await new Client(key).hello();
    }
    expect((await first.call("GET", "/a2/poll?a=main")).code).toBe(Code.NO_SESSION);
  });
});

describe("signed requests", () => {
  let c: Client;
  beforeEach(async () => {
    await start();
    c = new Client(pair());
    await c.hello();
  });

  it("answers a genuine request with a reply signed for its seq", async () => {
    const r = await c.call("GET", "/a2/poll?a=main&after=0");
    expect(r.code).toBe(Code.OK);
    expect(r.genuine).toBe(true);
  });

  it("refuses a bad signature, unsigned", async () => {
    const r = await c.call("GET", "/a2/poll?a=main", "", { tamper: true });
    expect(r.code).toBe(Code.BAD_SIGNATURE);
    expect(r.mac).toBeNull();
  });

  it("refuses a seq that does not rise, and does not act on it", async () => {
    const sent = await c.call("POST", "/a2/send?a=main", "once");
    expect(sent.code).toBe(Code.OK);
    const again = await c.call("POST", "/a2/send?a=main", "once", { seq: c.seq });
    expect(again.code).toBe(Code.REPLAY);
    expect(again.genuine).toBe(true);
    const h = await c.call("GET", "/a2/hist?a=main&k=50");
    expect(h.msgs.filter((m) => m.mine).length).toBe(1);
  });

  it("will not let a session be used by another key", async () => {
    const other = new Client(pair());
    other.sid = c.sid; // a stolen session id, without its key
    other.sk = randomBytes(32);
    expect((await other.call("GET", "/a2/poll?a=main")).code).toBe(Code.BAD_SIGNATURE);
  });

  it("ends a session after 30 idle minutes", async () => {
    clock += 31 * 60_000;
    expect((await c.call("GET", "/a2/poll?a=main")).code).toBe(Code.NO_SESSION);
    expect((await c.hello()).code).toBe(Code.OK);
    expect((await c.call("GET", "/a2/poll?a=main")).code).toBe(Code.OK);
  });

  it("refuses an unknown agent, signed", async () => {
    const r = await c.call("GET", "/a2/poll?a=nobody");
    expect(r.code).toBe(Code.UNKNOWN_AGENT);
    expect(r.genuine).toBe(true);
  });

  it("refuses a body over 1,024 bytes, unsigned", async () => {
    const r = await c.call("POST", "/a2/send?a=main", "x".repeat(1025));
    expect(r.code).toBe(Code.TOO_LARGE);
    expect(r.mac).toBeNull();
  });

  it("refuses text the //e could not have typed", async () => {
    const r = await c.call("POST", "/a2/send?a=main", "caf\u00e9");
    expect(r.code).toBe(Code.BAD_REQUEST);
    expect(r.genuine).toBe(true);
  });

  it("refuses a request line over 512 bytes", async () => {
    const r = await c.call("GET", `/a2/poll?a=main&x=${"y".repeat(520)}`);
    expect(r.code).toBe(Code.TOO_LARGE);
  });
});

describe("the conversation", () => {
  let c: Client;
  beforeEach(async () => {
    await start();
    c = new Client(pair());
    await c.hello();
  });

  it("stores what is sent and delivers the answer, folded, to a long poll", async () => {
    const sent = await c.call("POST", "/a2/send?a=main", "Hello\nthere");
    expect(sent.msgs).toEqual([{ n: 1, mine: true, time: expect.stringMatching(/^\d\d:\d\d$/), text: "Hello\nthere" }]);

    const p = await c.call("GET", "/a2/poll?a=main&after=1&w=2000");
    expect(p.genuine).toBe(true);
    expect(p.msgs.length).toBe(1);
    expect(p.msgs[0]).toMatchObject({ n: 2, mine: false });
    expect(p.msgs[0].text).toBe('main heard you -- "Hello\nthere" \x02');
  });

  it("says when the agent is busy, and when it has finished", async () => {
    let finish: ReplySink | undefined;
    const slow: AgentPort = { agents: () => ["main"], deliver: (_d, _a, _t, out) => (finish = out) };
    await new Promise((r) => http.close(r));
    await start(slow);
    c = new Client(pair());
    await c.hello();
    await c.call("POST", "/a2/send?a=main", "think hard");
    const busy = await c.call("GET", "/a2/poll?a=main&after=1");
    expect(busy.lines).toContainEqual(["busy", "1"]);
    const waiting = c.call("GET", "/a2/poll?a=main&after=1&w=3000");
    setTimeout(() => {
      finish!.text("Done thinking.");
      finish!.done();
    }, 50);
    const r = await waiting;
    expect(r.msgs.map((m) => m.text)).toEqual(["Done thinking."]);
    const after = await c.call("GET", "/a2/poll?a=main&after=2");
    expect(after.lines).toContainEqual(["busy", "0"]);
  });

  it("returns at once with nothing new when not asked to wait", async () => {
    const t0 = Date.now();
    const r = await c.call("GET", "/a2/poll?a=main&after=0");
    expect(Date.now() - t0).toBeLessThan(500);
    expect(r.msgs).toEqual([]);
    expect(r.lines).toContainEqual(["more", "0"]);
  });

  it("cuts a long answer into parts that each fit a reply, and pages them", async () => {
    await c.call("POST", "/a2/send?a=coder", "say something long");
    await new Promise((r) => setTimeout(r, 100));
    let after = 1;
    const texts: string[] = [];
    for (let i = 0; i < 20; i++) {
      const r = await c.call("GET", `/a2/poll?a=coder&after=${after}`);
      expect(r.genuine).toBe(true);
      expect(r.payload.length).toBeLessThanOrEqual(MAX_PAYLOAD);
      for (const m of r.msgs) {
        expect(m.text.length).toBeLessThanOrEqual(1500);
        expect([...m.text].every((ch) => ch === "\n" || ch === "\x02" || (ch >= " " && ch <= "~"))).toBe(true);
        texts.push(m.text);
        after = m.n;
      }
      if (r.lines.find(([k]) => k === "more")?.[1] === "0") break;
    }
    expect(texts.length).toBeGreaterThan(3);
    expect(texts.join("\n")).toContain("Part 5");
    expect(texts.join("\n")).toContain("loop:   sta $0200,y");
  });

  it("counts unread messages from other agents until polled", async () => {
    await c.call("POST", "/a2/send?a=coder", "hi coder");
    await new Promise((r) => setTimeout(r, 100));
    const onMain = await c.call("GET", "/a2/poll?a=main&after=0");
    expect(onMain.lines).toContainEqual(["unread", "coder 1"]);
    await c.call("GET", "/a2/poll?a=coder&after=2");
    const again = await c.call("GET", "/a2/poll?a=main&after=0");
    expect(again.lines.some(([k]) => k === "unread")).toBe(false);
  });

  it("serves history, newest page first, oldest first on the wire", async () => {
    for (let i = 1; i <= 6; i++) await c.call("POST", "/a2/send?a=main", `message ${i}`);
    await new Promise((r) => setTimeout(r, 100));
    const h = await c.call("GET", "/a2/hist?a=main&before=12&k=3");
    expect(h.msgs.map((m) => m.n)).toEqual([9, 10, 11]);
    expect(h.lines).toContainEqual(["more", "1"]);
    const all = await c.call("GET", "/a2/hist?a=main&before=4&k=50");
    expect(all.msgs.map((m) => m.n)).toEqual([1, 2, 3]);
    expect(all.lines).toContainEqual(["more", "0"]);
  });

  it("serves history from the start, and forward from a point", async () => {
    for (let i = 1; i <= 6; i++) await c.call("POST", "/a2/send?a=main", `message ${i}`);
    await new Promise((r) => setTimeout(r, 100));
    const first = await c.call("GET", "/a2/hist?a=main&after=0&k=3");
    expect(first.code).toBe(0);
    expect(first.msgs.map((m) => m.n)).toEqual([1, 2, 3]);
    expect(first.lines).toContainEqual(["more", "1"]);
    const rest = await c.call("GET", "/a2/hist?a=main&after=9&k=50");
    expect(rest.msgs.map((m) => m.n)).toEqual([10, 11, 12]);
    expect(rest.lines).toContainEqual(["more", "0"]);
    const both = await c.call("GET", "/a2/hist?a=main&after=0&before=5");
    expect(both.code).toBe(1);
  });

  it("does not count history from the start as seen", async () => {
    await c.call("POST", "/a2/send?a=coder", "hi coder");
    await new Promise((r) => setTimeout(r, 100));
    await c.call("GET", "/a2/hist?a=coder&after=0&k=50");
    const onMain = await c.call("GET", "/a2/poll?a=main&after=0");
    expect(onMain.lines).toContainEqual(["unread", "coder 1"]);
  });

  it("keeps each device's conversations apart", async () => {
    await c.call("POST", "/a2/send?a=main", "mine alone");
    const other = new Client(pair());
    await other.hello();
    const h = await other.call("GET", "/a2/hist?a=main&k=50");
    expect(h.msgs).toEqual([]);
  });
});

describe("revoking, restarting, and messages the agent starts", () => {
  it("ends a revoked device's sessions at once, even when revoked by another process", async () => {
    await start();
    const c = new Client(pair());
    await c.hello();
    expect((await c.call("GET", "/a2/poll?a=main")).code).toBe(Code.OK);
    // The CLI is another process with its own copy of the file.
    await new Promise((r) => setTimeout(r, 20)); // a later mtime
    expect(new DeviceStore(store.path).revoke(c.id)).toBe(true);
    expect((await c.call("GET", "/a2/poll?a=main")).code).toBe(Code.NO_SESSION);
    expect((await c.hello()).code).toBe(Code.UNKNOWN_DEVICE);
  });

  it("sees a device paired by another process without a restart", async () => {
    await start();
    const written = new DeviceStore(store.path).pair("later").written;
    const c = new Client(parseKey(written)!);
    expect((await c.hello()).code).toBe(Code.OK);
  });

  it("keeps conversations across a restart, numbering on from the last", async () => {
    await start(undefined, { convs: true });
    const key = pair();
    let c = new Client(key);
    await c.hello();
    await c.call("POST", "/a2/send?a=main", "before the restart");
    await new Promise((r) => setTimeout(r, 100));
    const file = join(dir, "conversations", `${c.id}.main.json`);
    expect(statSync(file).mode & 0o777).toBe(0o600);

    http.closeAllConnections();
    await new Promise((r) => http.close(r));
    await start(undefined, { keep: true, convs: true });
    c = new Client(key);
    await c.hello();
    const h = await c.call("GET", "/a2/hist?a=main&k=50");
    expect(h.msgs.map((m) => m.text)).toEqual(["before the restart", expect.stringContaining("before the restart")]);
    const sent = await c.call("POST", "/a2/send?a=main", "after");
    expect(sent.msgs[0].n).toBe(3);
  });

  it("drops a device's conversations when asked (on revoke)", async () => {
    await start(undefined, { convs: true });
    const c = new Client(pair());
    await c.hello();
    await c.call("POST", "/a2/send?a=coder", "x");
    await new Promise((r) => setTimeout(r, 100));
    const convs = new FileConvStore(join(dir, "conversations"));
    expect(readdirSync(convs.dir).some((n) => n.startsWith(c.id))).toBe(true);
    convs.drop(c.id);
    expect(existsSync(join(convs.dir, `${c.id}.coder.json`))).toBe(false);
  });

  it("delivers a message the agent starts on the next poll, folded and cut into parts", async () => {
    await start();
    const c = new Client(pair());
    await c.hello();
    const n = a2.post(c.id, "main", "**Reminder:** water the plants \u{1F331}\n\n" + "More. ".repeat(400));
    expect(n).toBeGreaterThan(1); // more than one part
    const p = await c.call("GET", "/a2/poll?a=main&after=0");
    expect(p.lines).toContainEqual(["more", "1"]); // the rest on the next poll
    expect(p.msgs[0].text.startsWith("Reminder: water the plants")).toBe(true);
    const rest = await c.call("GET", `/a2/poll?a=main&after=${p.msgs[p.msgs.length - 1].n}`);
    expect(rest.msgs[rest.msgs.length - 1].n).toBe(n);
    expect([...p.msgs, ...rest.msgs].every((m) => !m.mine)).toBe(true);
    expect(() => a2.post("00000000", "main", "hi")).toThrow(/no device/);
    expect(() => a2.post(c.id, "nobody", "hi")).toThrow(/no agent/);
  });
});

describe("display names", () => {
  it("are printable ASCII on one line, at most 16 characters", () => {
    expect(displayLabel(undefined)).toBe("");
    expect(displayLabel("   ")).toBe("");
    expect(displayLabel("Pinchy")).toBe("Pinchy");
    expect(displayLabel("Café\tBot")).toBe("Cafe Bot");
    expect(displayLabel("A very long agent name indeed")).toBe("A very long agen");
    expect(displayLabel("Fifteen chars x yz")).toBe("Fifteen chars x");
    expect(displayLabel("🦞 Pinchy 👍🏽")).toBe("Pinchy");
  });
});
