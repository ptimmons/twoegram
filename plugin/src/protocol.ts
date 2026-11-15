// The wire format of docs/PROTOCOL.md: keys, signatures, replies and
// records. No I/O here, so the tests and the server share it.

import { createHash, createHmac, randomBytes, timingSafeEqual } from "node:crypto";

export const VERSION = 1;
export const MAX_PAYLOAD = 1900;
export const MAX_TEXT = 1500;
export const MAX_BODY = 1024;
export const MAX_TARGET = 512;
export const MAX_WAIT_MS = 4000;

export const Code = {
  OK: 0,
  BAD_REQUEST: 1,
  UNKNOWN_DEVICE: 2,
  BAD_SIGNATURE: 3,
  NO_SESSION: 4,
  REPLAY: 5,
  LOCKED: 6,
  UNKNOWN_AGENT: 7,
  TOO_LARGE: 8,
  BUSY: 9,
} as const;
export type CodeValue = (typeof Code)[keyof typeof Code];

// ---- the device key, written for typing ----

const ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
// Crockford's check symbols: the 32 above, then five more for 32..36.
const CHECK = ALPHABET + "*~$=U";

export function newKey(): Buffer {
  return randomBytes(16);
}

/** Weighted sum modulo 37: a prime above every weight and difference, so
 * any one wrong character, or two neighbours swapped, changes it. */
function checkChar(values: number[]): number {
  let sum = 0;
  values.forEach((v, i) => {
    sum += (i + 1) * v;
  });
  return sum % 37;
}

/** 16 bytes as 26 base-32 characters and a check character, in groups. */
export function formatKey(key: Buffer): string {
  if (key.length !== 16) throw new Error("a device key is 16 bytes");
  const values: number[] = [];
  let acc = 0;
  let bits = 0;
  for (const b of key) {
    acc = (acc << 8) | b;
    bits += 8;
    while (bits >= 5) {
      values.push((acc >> (bits - 5)) & 31);
      bits -= 5;
    }
    acc &= (1 << bits) - 1;
  }
  values.push((acc << (5 - bits)) & 31); // 3 bits and 2 zeros
  const s = values.map((v) => ALPHABET[v]).join("") + CHECK[checkChar(values)];
  return s.match(/.{1,5}/g)!.join("-");
}

/** The written form back to 16 bytes, or null if it does not check. */
export function parseKey(text: string): Buffer | null {
  const clean = text
    .toUpperCase()
    .replace(/[\s-]/g, "")
    .replace(/O/g, "0")
    .replace(/[IL]/g, "1");
  if (clean.length !== 27) return null;
  const values: number[] = [];
  for (const ch of clean.slice(0, 26)) {
    const v = ALPHABET.indexOf(ch);
    if (v < 0) return null;
    values.push(v);
  }
  if (CHECK.indexOf(clean[26]) !== checkChar(values)) return null;
  if (values[25] & 3) return null; // the two padding bits must be zero
  const out = Buffer.alloc(16);
  let acc = 0;
  let bits = 0;
  let n = 0;
  for (const v of values) {
    acc = (acc << 5) | v;
    bits += 5;
    if (bits >= 8) {
      if (n < 16) out[n++] = (acc >> (bits - 8)) & 255;
      bits -= 8;
    }
    acc &= (1 << bits) - 1;
  }
  return out;
}

export function deviceId(key: Buffer): string {
  return createHash("sha256").update("twoegram-id").update(key).digest().subarray(0, 4).toString("hex");
}

// ---- signatures ----

export function hmac(key: Buffer, ...parts: (Buffer | string)[]): Buffer {
  const h = createHmac("sha256", key);
  for (const p of parts) h.update(p);
  return h.digest();
}

export function u32(n: number): Buffer {
  const b = Buffer.alloc(4);
  b.writeUInt32BE(n >>> 0);
  return b;
}

export function helloProof(key: Buffer, clientNonce: Buffer): Buffer {
  return hmac(key, "hello", clientNonce);
}

export function sessionKey(key: Buffer, clientNonce: Buffer, serverNonce: Buffer): Buffer {
  return hmac(key, "session", clientNonce, serverNonce);
}

export function requestMac(sk: Buffer, seq: number, method: string, target: string, body: Buffer): Buffer {
  return hmac(sk, "q", u32(seq), method, " ", target, "\n", body);
}

export function replyMac(sk: Buffer, seq: number, payload: Buffer): Buffer {
  return hmac(sk, "r", u32(seq), payload);
}

export function helloMac(sk: Buffer, payload: Buffer): Buffer {
  return hmac(sk, "h", payload);
}

/** Constant-time comparison of a signature with its 64-hex-digit form. */
export function macEquals(mac: Buffer, hex: string | undefined): boolean {
  if (!hex || !/^[0-9a-f]{64}$/.test(hex)) return false;
  return timingSafeEqual(mac, Buffer.from(hex, "hex"));
}

// ---- replies ----

/** The bytes of a reply: the header line, then the payload. */
export function reply(code: number, mac: Buffer | null, payload: Buffer): Buffer {
  const head = `A2 ${VERSION} ${code} ${mac ? mac.toString("hex") : "-"}\n`;
  return Buffer.concat([Buffer.from(head, "latin1"), payload]);
}

export type Msg = { n: number; mine: boolean; time: string; text: string };

export function msgRecord(m: Msg): Buffer {
  const text = Buffer.from(m.text, "latin1");
  return Buffer.concat([
    Buffer.from(`msg ${m.n} ${m.mine ? 1 : 0} ${m.time} ${text.length}\n`, "latin1"),
    text,
    Buffer.from("\n", "latin1"),
  ]);
}

export type Parsed = {
  version: number;
  code: number;
  mac: string | null;
  payload: Buffer;
  lines: [string, string][];
  msgs: Msg[];
};

/** Parse a reply as the Apple will. Throws on anything malformed. */
export function parseReply(body: Buffer): Parsed {
  const nl = body.indexOf(10);
  if (nl < 0) throw new Error("no header line");
  const head = body.subarray(0, nl).toString("latin1").split(" ");
  if (head.length !== 4 || head[0] !== "A2") throw new Error("bad header line");
  const payload = body.subarray(nl + 1);
  const lines: [string, string][] = [];
  const msgs: Msg[] = [];
  let i = 0;
  while (i < payload.length) {
    const e = payload.indexOf(10, i);
    if (e < 0) throw new Error("unterminated line");
    const line = payload.subarray(i, e).toString("latin1");
    i = e + 1;
    const sp = line.indexOf(" ");
    const word = sp < 0 ? line : line.slice(0, sp);
    const value = sp < 0 ? "" : line.slice(sp + 1);
    if (word === "msg") {
      const f = value.split(" ");
      if (f.length !== 4) throw new Error("bad msg line");
      const len = Number(f[3]);
      if (!Number.isInteger(len) || i + len + 1 > payload.length || payload[i + len] !== 10) {
        throw new Error("bad msg length");
      }
      msgs.push({ n: Number(f[0]), mine: f[1] === "1", time: f[2], text: payload.subarray(i, i + len).toString("latin1") });
      i += len + 1;
    } else {
      lines.push([word, value]);
    }
  }
  return { version: Number(head[1]), code: Number(head[2]), mac: head[3] === "-" ? null : head[3], payload, lines, msgs };
}

export const AGENT_NAME = /^[a-z0-9_-]{1,16}$/;
