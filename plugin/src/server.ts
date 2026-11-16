// The /a2 endpoints (docs/PROTOCOL.md): sessions, signatures, lockouts,
// and each device's conversations. One handler serves both the
// stand-alone server and, from milestone 6, the OpenClaw route.

import type { IncomingMessage, ServerResponse } from "node:http";
import { randomBytes } from "node:crypto";
import type { AgentPort } from "./agent.ts";
import type { ConvStore } from "./convstore.ts";
import type { DeviceStore } from "./devices.ts";
import {
  AGENT_NAME,
  Code,
  DISPLAY_NAME_MAX,
  MAX_BODY,
  MAX_PAYLOAD,
  MAX_TARGET,
  MAX_WAIT_MS,
  VERSION,
  helloMac,
  helloProof,
  macEquals,
  msgRecord,
  replyMac,
  reply,
  requestMac,
  sessionKey,
  type Msg,
} from "./protocol.ts";
import { foldAscii, forApple, split } from "./text.ts";

export type Options = {
  devices: DeviceStore;
  agents: AgentPort;
  now?: () => number; // milliseconds
  log?: (line: string) => void;
  /** What the server is and which build, sent in every hello reply. */
  ident?: string;
  /** For diagnostics: the number of the TCP connection a request came
   * on. When given, every reply carries a `conn` line. */
  connOf?: (req: IncomingMessage) => number | undefined;
  /** For testing the client: each conversation starts with this many
   * made-up messages. */
  seed?: number;
  /** Where conversations are kept between restarts; in memory if absent. */
  convs?: ConvStore;
};

type Session = { sid: string; device: string; sk: Buffer; lastSeq: number; lastUsed: number };

type Conversation = { msgs: Msg[]; next: number; busy: number; seen: number };

const SESSIONS_PER_DEVICE = 4;
const SESSION_IDLE_MS = 30 * 60_000;
const DEVICE_FAILS = 5;
const DEVICE_WINDOW_MS = 5 * 60_000;
const DEVICE_LOCK_MS = 15 * 60_000;
const IP_REFUSALS = 20;
const IP_WINDOW_MS = 60_000;
const IP_BLOCK_MS = 5 * 60_000;
const LOG_KEEP = 500;
const HIST_MAX = 50;

class Refusal extends Error {
  code: number;
  why: string;
  constructor(code: number, why: string) {
    super(why);
    this.code = code;
    this.why = why;
  }
}

export class A2Server {
  private o: Required<Options>;
  private sessions = new Map<string, Session>();
  private convs = new Map<string, Conversation>();
  private waiters = new Map<string, Set<() => void>>(); // by device
  private deviceFails = new Map<string, { times: number[]; until: number }>();
  private ipRefusals = new Map<string, { count: number; start: number; until: number }>();

  constructor(options: Options) {
    this.o = { now: Date.now, log: () => {}, ident: "", connOf: () => undefined, seed: 0, convs: memoryOnly, ...options };
  }

  // ---- the handler ----

  async handle(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const ip = req.socket.remoteAddress ?? "?";
    const target = req.url ?? "";
    let out: Buffer;
    try {
      out = await this.dispatch(req, ip, target);
    } catch (e) {
      const r = e instanceof Refusal ? e : new Refusal(Code.BAD_REQUEST, "internal");
      if (!(e instanceof Refusal)) this.o.log(`a2: ${String(e)}`);
      // A refused hello still says which server refused it.
      const who = target.startsWith("/a2/hello") ? this.serverLine() : "";
      out = reply(r.code, null, Buffer.from(`why ${r.why}\n${who}`, "latin1"));
    }
    res.writeHead(200, { "Content-Type": "text/plain; charset=us-ascii", "Content-Length": out.length });
    res.end(out);
  }

  private async dispatch(req: IncomingMessage, ip: string, target: string): Promise<Buffer> {
    const now = this.o.now();
    this.ipCheck(ip, now);
    if (target.length > MAX_TARGET) throw this.refuse(ip, Code.TOO_LARGE, "request line too long", false);
    const url = new URL(target, "http://a2");
    const body = await readBody(req);
    if (body === null) throw new Refusal(Code.TOO_LARGE, "body too long");

    if (url.pathname === "/a2/hello" && req.method === "GET") return this.hello(url, ip, now, req);

    // Everything else: a session, a signature, a fresh sequence number.
    const sid = header(req, "x-a2-session");
    const seq = Number(header(req, "x-a2-seq"));
    const mac = header(req, "x-a2-mac");
    const s = sid ? this.sessions.get(sid) : undefined;
    if (!s) throw new Refusal(Code.NO_SESSION, "no such session");
    if (!this.o.devices.get(s.device)) {
      // Revoked since the hello: its sessions end now.
      this.endSessions(s.device);
      throw new Refusal(Code.NO_SESSION, "device revoked");
    }
    if (now - s.lastUsed > SESSION_IDLE_MS) {
      this.sessions.delete(s.sid);
      throw new Refusal(Code.NO_SESSION, "session expired");
    }
    this.deviceCheck(s.device, now);
    if (!Number.isInteger(seq) || seq < 1 || seq > 0xffffffff) throw new Refusal(Code.BAD_REQUEST, "bad seq");
    if (!macEquals(requestMac(s.sk, seq, req.method ?? "", target, body), mac)) {
      this.deviceFail(s.device, now);
      throw this.refuse(ip, Code.BAD_SIGNATURE, "bad signature", true);
    }
    // Genuine from here on, so replies are signed.
    const signed = (code: number, body: Buffer) => {
      const payload = Buffer.concat([body, this.connLine(req)]);
      return reply(code, replyMac(s.sk, seq, payload), payload);
    };
    if (seq <= s.lastSeq) return signed(Code.REPLAY, Buffer.from("why seq not rising\n"));
    s.lastSeq = seq;
    s.lastUsed = now;

    try {
      const agent = url.searchParams.get("a") ?? "";
      if (!this.o.agents.agents().includes(agent)) throw new Refusal(Code.UNKNOWN_AGENT, "no such agent");
      let payload: Buffer;
      if (url.pathname === "/a2/send" && req.method === "POST") payload = this.send(s.device, agent, body);
      else if (url.pathname === "/a2/poll" && req.method === "GET") payload = await this.poll(s.device, agent, url);
      else if (url.pathname === "/a2/hist" && req.method === "GET") payload = this.hist(s.device, agent, url);
      else throw new Refusal(Code.BAD_REQUEST, "no such request");
      return signed(Code.OK, payload);
    } catch (e) {
      if (e instanceof Refusal) return signed(e.code, Buffer.from(`why ${e.why}\n`, "latin1"));
      throw e;
    }
  }

  // ---- hello ----

  private hello(url: URL, ip: string, now: number, req: IncomingMessage): Buffer {
    const id = url.searchParams.get("d") ?? "";
    const cHex = url.searchParams.get("c") ?? "";
    const proof = url.searchParams.get("m") ?? "";
    if (!/^[0-9a-f]{8}$/.test(id) || !/^[0-9a-f]{32}$/.test(cHex)) throw new Refusal(Code.BAD_REQUEST, "bad hello");
    this.deviceCheck(id, now);
    const dev = this.o.devices.get(id);
    if (!dev) {
      this.o.log(`a2: hello from unknown device ${id} at ${ip}`);
      throw this.refuse(ip, Code.UNKNOWN_DEVICE, "unknown device", true);
    }
    const cn = Buffer.from(cHex, "hex");
    if (!macEquals(helloProof(dev.key, cn), proof)) {
      this.o.log(`a2: bad hello proof for device ${id} at ${ip}`);
      this.deviceFail(id, now);
      throw this.refuse(ip, Code.BAD_SIGNATURE, "bad hello proof", true);
    }
    const sn = randomBytes(16);
    let sid = randomBytes(4).toString("hex");
    while (this.sessions.has(sid)) sid = randomBytes(4).toString("hex");
    const s: Session = { sid, device: id, sk: sessionKey(dev.key, cn, sn), lastSeq: 0, lastUsed: now };
    this.sessions.set(sid, s);
    const mine = [...this.sessions.values()].filter((x) => x.device === id).sort((a, b) => a.lastUsed - b.lastUsed);
    while (mine.length > SESSIONS_PER_DEVICE) this.sessions.delete(mine.shift()!.sid);

    const lines = [`sid ${sid}`, `sn ${sn.toString("hex")}`, `ver ${VERSION}`, `time ${Math.floor(now / 1000)}`];
    const agents = this.o.agents.agents();
    for (const a of agents) lines.push(`agent ${a}`);
    for (const a of agents) {
      const label = displayLabel(this.o.agents.displayName?.(a));
      if (label && label !== a) lines.push(`name ${a} ${label}`);
    }
    const payload = Buffer.concat([
      Buffer.from(lines.join("\n") + "\n" + this.serverLine(), "latin1"),
      this.connLine(req),
    ]);
    this.o.log(`a2: hello from ${dev.name} (${id}) at ${ip}`);
    return reply(Code.OK, helloMac(s.sk, payload), payload);
  }

  private connLine(req: IncomingMessage): Buffer {
    const n = this.o.connOf(req);
    return Buffer.from(n === undefined ? "" : `conn ${n}\n`, "latin1");
  }

  private serverLine(): string {
    const id = this.o.ident.replace(/[^\x20-\x7e]/g, "?").slice(0, 60);
    return id ? `server ${id}\n` : "";
  }

  private endSessions(device: string): void {
    for (const x of [...this.sessions.values()]) if (x.device === device) this.sessions.delete(x.sid);
  }

  // ---- the conversation ----

  private conv(device: string, agent: string): Conversation {
    const k = `${device}:${agent}`;
    let c = this.convs.get(k);
    if (!c) {
      c = { msgs: [], next: 1, busy: 0, seen: 0 };
      this.convs.set(k, c);
      let saved;
      try {
        saved = this.o.convs.load(device, agent);
      } catch (e) {
        this.o.log(`a2: could not read the log of ${device}.${agent}: ${String(e)}`);
      }
      if (saved) {
        c.msgs = saved.msgs;
        c.next = saved.next;
        c.seen = c.next - 1;
        return c;
      }
      for (const [mine, text] of seedMessages(agent, this.o.seed)) {
        c.msgs.push({ n: c.next, mine, time: hhmm(this.o.now() - (this.o.seed - c.next + 1) * 60_000), text });
        c.next++;
      }
      c.seen = c.next - 1;
    }
    return c;
  }

  private append(device: string, agent: string, mine: boolean, text: string): Msg {
    const c = this.conv(device, agent);
    const m: Msg = { n: c.next++, mine, time: hhmm(this.o.now()), text };
    c.msgs.push(m);
    if (c.msgs.length > LOG_KEEP) c.msgs.splice(0, c.msgs.length - LOG_KEEP);
    // A reply still arriving for a revoked device is not written back.
    if (this.o.devices.get(device)) try {
      this.o.convs.save(device, agent, { msgs: c.msgs, next: c.next });
    } catch (e) {
      this.o.log(`a2: could not save the log of ${device}.${agent}: ${String(e)}`);
    }
    this.wake(device);
    return m;
  }

  /**
   * A message the agent sends on its own (a reminder, a cron job's
   * report): added to the conversation for the Apple's next poll. Returns
   * the number of its last part.
   */
  post(device: string, agent: string, text: string): number {
    if (!this.o.devices.get(device)) throw new Error(`a2: no device ${device}`);
    if (!this.o.agents.agents().includes(agent)) throw new Error(`a2: no agent ${agent}`);
    const folded = forApple(text);
    if (!folded) throw new Error("a2: nothing to send");
    let n = 0;
    for (const part of split(folded)) n = this.append(device, agent, false, part).n;
    return n;
  }

  private wake(device: string): void {
    const w = this.waiters.get(device);
    if (!w) return;
    this.waiters.delete(device);
    for (const f of w) f();
  }

  private send(device: string, agent: string, body: Buffer): Buffer {
    if (!body.length) throw new Refusal(Code.BAD_REQUEST, "empty message");
    for (const b of body) {
      if (b !== 10 && (b < 32 || b > 126)) throw new Refusal(Code.BAD_REQUEST, "not printable ASCII");
    }
    const m = this.append(device, agent, true, body.toString("latin1"));
    const c = this.conv(device, agent);
    c.busy++;
    let finished = false;
    this.o.agents.deliver(device, agent, m.text, {
      text: (t) => {
        if (finished) return;
        const folded = forApple(t);
        if (!folded) return;
        for (const part of split(folded)) this.append(device, agent, false, part);
      },
      done: () => {
        if (finished) return;
        finished = true;
        c.busy = Math.max(0, c.busy - 1);
        this.wake(device);
      },
    });
    return msgRecord(m);
  }

  private async poll(device: string, agent: string, url: URL): Promise<Buffer> {
    const after = intParam(url, "after", 0);
    const wait = Math.min(intParam(url, "w", 0), MAX_WAIT_MS);
    const c = this.conv(device, agent);
    c.seen = Math.max(c.seen, after);
    const busyAtStart = c.busy > 0;
    const hasNews = () => c.msgs.some((m) => m.n > after) || c.busy > 0 !== busyAtStart;
    if (!hasNews() && wait > 0) {
      await new Promise<void>((resolve) => {
        const set = this.waiters.get(device) ?? new Set();
        let timer: ReturnType<typeof setTimeout>;
        const done = () => {
          clearTimeout(timer);
          set.delete(done);
          resolve();
        };
        timer = setTimeout(done, wait);
        set.add(done);
        this.waiters.set(device, set);
      });
    }

    const head: string[] = [];
    for (const other of this.o.agents.agents()) {
      if (other === agent) continue;
      const oc = this.convs.get(`${device}:${other}`);
      const n = oc ? oc.msgs.filter((m) => !m.mine && m.n > oc.seen).length : 0;
      if (n) head.push(`unread ${other} ${n}`);
    }
    const fresh = c.msgs.filter((m) => m.n > after);
    return pack([`busy ${c.busy > 0 ? 1 : 0}`, ...head], fresh, false, fresh.length);
  }

  private hist(device: string, agent: string, url: URL): Buffer {
    const c = this.conv(device, agent);
    const before = intParam(url, "before", c.next);
    const k = Math.min(Math.max(intParam(url, "k", 20), 1), HIST_MAX);
    const older = c.msgs.filter((m) => m.n < before);
    return pack([], older.slice(-k), true, older.length);
  }

  // ---- lockouts ----

  private refuse(ip: string, code: number, why: string, counts: boolean): Refusal {
    if (counts) {
      const now = this.o.now();
      const r = this.ipRefusals.get(ip) ?? { count: 0, start: now, until: 0 };
      if (now - r.start > IP_WINDOW_MS) {
        r.count = 0;
        r.start = now;
      }
      if (++r.count >= IP_REFUSALS) {
        r.until = now + IP_BLOCK_MS;
        this.o.log(`a2: blocking ${ip} for ${IP_BLOCK_MS / 60_000} minutes`);
      }
      this.ipRefusals.set(ip, r);
    }
    return new Refusal(code, why);
  }

  private ipCheck(ip: string, now: number): void {
    const r = this.ipRefusals.get(ip);
    if (r && r.until > now) throw new Refusal(Code.LOCKED, "address blocked");
  }

  private deviceFail(id: string, now: number): void {
    const f = this.deviceFails.get(id) ?? { times: [], until: 0 };
    f.times = f.times.filter((t) => now - t < DEVICE_WINDOW_MS);
    f.times.push(now);
    if (f.times.length >= DEVICE_FAILS) {
      f.until = now + DEVICE_LOCK_MS;
      f.times = [];
      this.o.log(`a2: locking device ${id} for ${DEVICE_LOCK_MS / 60_000} minutes`);
    }
    this.deviceFails.set(id, f);
  }

  private deviceCheck(id: string, now: number): void {
    const f = this.deviceFails.get(id);
    if (f && f.until > now) throw new Refusal(Code.LOCKED, "device locked");
  }
}

// ---- helpers ----

function header(req: IncomingMessage, name: string): string | undefined {
  const v = req.headers[name];
  return Array.isArray(v) ? v[0] : v;
}

function intParam(url: URL, name: string, dflt: number): number {
  const v = url.searchParams.get(name);
  if (v === null) return dflt;
  const n = Number(v);
  if (!Number.isInteger(n) || n < 0) throw new Refusal(Code.BAD_REQUEST, `bad ${name}`);
  return n;
}

function hhmm(ms: number): string {
  const d = new Date(ms);
  return `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
}

/** The body, or null if it is longer than the protocol allows. */
function readBody(req: IncomingMessage): Promise<Buffer | null> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    let n = 0;
    let over = false;
    req.on("data", (c: Buffer) => {
      n += c.length;
      if (n > MAX_BODY) over = true;
      else chunks.push(c);
    });
    req.on("end", () => resolve(over ? null : Buffer.concat(chunks)));
    req.on("error", reject);
  });
}

/**
 * A `more` line, the other lines, then as many whole messages as fit
 * MAX_PAYLOAD. `more` is 1 when fewer than total went in. History fills
 * from the newest, so the most recent older messages are the ones kept;
 * the wire is always oldest first.
 */
function pack(lines: string[], msgs: Msg[], fromNewest: boolean, total: number): Buffer {
  const head = Buffer.from(lines.map((l) => l + "\n").join(""), "latin1");
  const moreLen = "more 0\n".length;
  let room = MAX_PAYLOAD - head.length - moreLen;
  const order = fromNewest ? [...msgs].reverse() : msgs;
  const kept: Buffer[] = [];
  for (const m of order) {
    const r = msgRecord(m);
    if (r.length > room) break;
    kept.push(r);
    room -= r.length;
  }
  if (fromNewest) kept.reverse();
  const more = Buffer.from(`more ${kept.length < total ? 1 : 0}\n`, "latin1");
  return Buffer.concat([more, head, ...kept]);
}

/** Made-up history: questions and answers of varied length, a code block
 * now and then, numbered so a reader can see where they are. */
function seedMessages(agent: string, n: number): [boolean, string][] {
  const out: [boolean, string][] = [];
  const para =
    "This answer is long on purpose, so that it wraps across several lines " +
    "of the Apple's screen and the scrolling has something to work through.";
  for (let i = 1; i <= n; i++) {
    const hex = (i % 256).toString(16).padStart(2, "0");
    if (i % 2) {
      out.push([true, "Question " + i + " for " + agent + ": what comes next?"]);
    } else if (i % 10 === 0) {
      out.push([false, "Answer " + i + ", with code:\n\n        ldx #" + "$" + hex + "\nloop:   dex\n        bne loop\n        rts"]);
    } else if (i % 6 === 0) {
      out.push([false, "Answer " + i + ", a long one.\n\n" + para + " " + para + "\n\n" + para]);
    } else {
      out.push([false, "Answer " + i + ": a short reply."]);
    }
  }
  return out;
}

const memoryOnly: ConvStore = { load: () => undefined, save: () => {}, drop: () => {} };

/** An agent's name as the Apple shows it: printable ASCII, one line, at
 * most DISPLAY_NAME_MAX characters, without emoji; empty if nothing is
 * left. */
export function displayLabel(name: string | undefined): string {
  if (!name) return "";
  // Emoji go (in a name, `*` or `:house:` reads as noise); the rest folds.
  const bare = name.replace(/[\p{Extended_Pictographic}\u{FE0F}\u{200D}\u{1F3FB}-\u{1F3FF}]/gu, "");
  const flat = foldAscii(bare).replace(/[^\x20-\x7e]+/g, " ").replace(/ +/g, " ").trim();
  return flat.slice(0, DISPLAY_NAME_MAX).trimEnd();
}
