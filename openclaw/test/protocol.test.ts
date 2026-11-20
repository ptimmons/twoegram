import { createHmac } from "node:crypto";
import { describe, expect, it } from "vitest";
import {
  deriveKey,
  deviceId,
  formatKey,
  hmac,
  macEquals,
  msgRecord,
  parseKey,
  parseReply,
  reply,
  requestMac,
  u32,
} from "../src/protocol.ts";

describe("device key", () => {
  it("round-trips through its written form", () => {
    for (let i = 0; i < 200; i++) {
      const key = Buffer.from(Array.from({ length: 16 }, () => Math.floor(Math.random() * 256)));
      const w = formatKey(key);
      expect(w).toMatch(/^[0-9A-Z]{5}(-[0-9A-Z]{5}){4}-[0-9A-Z][0-9A-Z*~$=]$/);
      expect(parseKey(w)!.equals(key)).toBe(true);
    }
  });

  it("has 26 characters of key and one check character", () => {
    expect(formatKey(Buffer.alloc(16)).replace(/-/g, "")).toBe("0".repeat(27));
    expect(formatKey(Buffer.alloc(16, 0xff)).replace(/-/g, "").length).toBe(27);
  });

  it("forgives case, spacing and look-alike letters", () => {
    const key = Buffer.from("00112233445566778899aabbccddeeff", "hex");
    const w = formatKey(key);
    const sloppy = w.toLowerCase().replace(/-/g, " ").replace(/0/g, "o").replace(/1/g, "l");
    expect(parseKey(sloppy)!.equals(key)).toBe(true);
  });

  it("catches every single-character slip and every swap of neighbours", () => {
    const alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    for (let k = 0; k < 20; k++) {
      const w = formatKey(Buffer.from(Array.from({ length: 16 }, () => Math.floor(Math.random() * 256)))).replace(/-/g, "");
      for (let pos = 0; pos < 26; pos++) {
        for (const ch of alphabet) {
          if (ch === w[pos]) continue;
          expect(parseKey(w.slice(0, pos) + ch + w.slice(pos + 1))).toBeNull();
        }
        if (pos < 25 && w[pos] !== w[pos + 1]) {
          expect(parseKey(w.slice(0, pos) + w[pos + 1] + w[pos] + w.slice(pos + 2))).toBeNull();
        }
      }
    }
  });

  it("refuses the wrong length or a stray letter", () => {
    expect(parseKey("ABC")).toBeNull();
    expect(parseKey("U".repeat(27))).toBeNull();
  });

  it("derives an 8-digit device id", () => {
    const id = deviceId(Buffer.alloc(16, 7));
    expect(id).toMatch(/^[0-9a-f]{8}$/);
    expect(deviceId(Buffer.alloc(16, 7))).toBe(id);
    expect(deviceId(Buffer.alloc(16, 8))).not.toBe(id);
  });
});

describe("signatures", () => {
  it("is HMAC-SHA256 over the parts in order (RFC 4231 case 2)", () => {
    const mac = hmac(Buffer.from("Jefe"), "what do ya want ", "for nothing?");
    expect(mac.toString("hex")).toBe("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
  });

  it("binds the sequence number, method, target and body", () => {
    const sk = Buffer.alloc(32, 1);
    const base = requestMac(sk, 5, "POST", "/a2/send?a=main", Buffer.from("hi"));
    const expected = createHmac("sha256", sk)
      .update(Buffer.concat([Buffer.from("q"), u32(5), Buffer.from("POST /a2/send?a=main\nhi")]))
      .digest();
    expect(base.equals(expected)).toBe(true);
    expect(requestMac(sk, 6, "POST", "/a2/send?a=main", Buffer.from("hi")).equals(base)).toBe(false);
    expect(requestMac(sk, 5, "GET", "/a2/send?a=main", Buffer.from("hi")).equals(base)).toBe(false);
    expect(requestMac(sk, 5, "POST", "/a2/send?a=coder", Buffer.from("hi")).equals(base)).toBe(false);
    expect(requestMac(sk, 5, "POST", "/a2/send?a=main", Buffer.from("ho")).equals(base)).toBe(false);
  });

  it("compares only well-formed hex", () => {
    const mac = Buffer.alloc(32, 0xab);
    expect(macEquals(mac, mac.toString("hex"))).toBe(true);
    expect(macEquals(mac, mac.toString("hex").toUpperCase())).toBe(false);
    expect(macEquals(mac, undefined)).toBe(false);
    expect(macEquals(mac, "ab")).toBe(false);
  });
});

describe("replies", () => {
  it("parse back to what was written", () => {
    const payload = Buffer.concat([
      Buffer.from("more 0\nbusy 1\nunread coder 2\n"),
      msgRecord({ n: 3, mine: false, time: "14:01", text: "two\nlines" }),
      msgRecord({ n: 4, mine: true, time: "14:02", text: "" }),
    ]);
    const r = parseReply(reply(0, Buffer.alloc(32, 9), payload));
    expect(r.code).toBe(0);
    expect(r.mac).toBe("09".repeat(32));
    expect(r.payload.equals(payload)).toBe(true);
    expect(r.lines).toEqual([
      ["more", "0"],
      ["busy", "1"],
      ["unread", "coder 2"],
    ]);
    expect(r.msgs).toEqual([
      { n: 3, mine: false, time: "14:01", text: "two\nlines" },
      { n: 4, mine: true, time: "14:02", text: "" },
    ]);
  });

  it("marks an unsigned reply", () => {
    expect(parseReply(reply(4, null, Buffer.from("why x\n"))).mac).toBeNull();
  });

  it("refuses a record whose length is wrong", () => {
    expect(() => parseReply(Buffer.from("A2 1 0 -\nmsg 1 0 10:00 5\nabc\n"))).toThrow();
  });
});

describe("keys derived from the Apple's master secret", () => {
  const master = Buffer.from("00112233445566778899aabbccddeeff", "hex");

  it("are HMAC(M, \"twoegram-gw\" || name), cut to 16 bytes", () => {
    const want = createHmac("sha256", master).update("twoegram-gwclaw1").digest().subarray(0, 16);
    expect(deriveKey(master, "claw1").equals(want)).toBe(true);
  });

  it("differ for every name, and so do their device ids", () => {
    const names = ["claw1", "mini", "relay", "a", "claw-1", "claw_1"];
    const ids = new Set(names.map((n) => deviceId(deriveKey(master, n))));
    expect(ids.size).toBe(names.length);
  });

  it("refuse a name the Apple could not type, and a master of the wrong size", () => {
    for (const bad of ["", "Claw1", "claw 1", "x".repeat(17), "café"]) {
      expect(() => deriveKey(master, bad)).toThrow();
    }
    expect(() => deriveKey(Buffer.alloc(15), "claw1")).toThrow();
  });
});
