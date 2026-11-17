// Writes hermes/tests/vectors.json: what the TypeScript plugin computes for a
// fixed set of inputs, so the Python port is checked against the same bytes.
//
//   node hermes/tools/vectors.ts > hermes/tests/vectors.json
//
// Run it again whenever plugin/src/protocol.ts or text.ts changes.

import { createHash } from "node:crypto";
import {
  deviceId,
  formatKey,
  helloMac,
  helloProof,
  msgRecord,
  reply,
  replyMac,
  requestMac,
  sessionKey,
} from "../../plugin/src/protocol.ts";
import { displayLabel } from "../../plugin/src/server.ts";
import { foldAscii, forApple, plainMarkdown, split } from "../../plugin/src/text.ts";

// Bytes that look random but are the same every run.
function bytes(seed: string, n: number): Buffer {
  const out: Buffer[] = [];
  for (let i = 0; out.length * 32 < n; i++) out.push(createHash("sha256").update(`${seed}/${i}`).digest());
  return Buffer.concat(out).subarray(0, n);
}

const keys = [Buffer.alloc(16), Buffer.alloc(16, 0xff), Buffer.from("00112233445566778899aabbccddeeff", "hex")];
for (let i = 0; i < 20; i++) keys.push(bytes(`key${i}`, 16));

const signing = keys.slice(0, 6).map((key, i) => {
  const cn = bytes(`cn${i}`, 16);
  const sn = bytes(`sn${i}`, 16);
  const sk = sessionKey(key, cn, sn);
  const body = Buffer.from(i % 2 ? `message ${i}\nline two` : "", "latin1");
  const target = `/a2/poll?a=main&after=${i}&w=4000`;
  const payload = Buffer.from(`more 0\nbusy ${i % 2}\n`, "latin1");
  return {
    key: key.toString("hex"),
    cn: cn.toString("hex"),
    sn: sn.toString("hex"),
    proof: helloProof(key, cn).toString("hex"),
    sk: sk.toString("hex"),
    seq: i * 1000 + 1,
    method: i % 2 ? "POST" : "GET",
    target,
    body: body.toString("hex"),
    request: requestMac(sk, i * 1000 + 1, i % 2 ? "POST" : "GET", target, body).toString("hex"),
    payload: payload.toString("hex"),
    reply: replyMac(sk, i * 1000 + 1, payload).toString("hex"),
    hello: helloMac(sk, payload).toString("hex"),
  };
});

const records = [
  { n: 1, mine: true, time: "09:05", text: "hi" },
  { n: 42, mine: false, time: "23:59", text: "two\nlines \x02" },
  { n: 7, mine: false, time: "00:00", text: "" },
].map((m) => ({ msg: m, record: msgRecord(m).toString("hex") }));
const replies = [
  { code: 0, mac: bytes("mac", 32).toString("hex"), payload: "more 0\n" },
  { code: 4, mac: null, payload: "why no such session\n" },
].map((r) => ({ ...r, bytes: reply(r.code, r.mac ? Buffer.from(r.mac, "hex") : null, Buffer.from(r.payload)).toString("hex") }));

// Text: the tests' cases, the kinds of thing agents write, and a sweep.
const para = "This paragraph is here to make the answer long enough to be cut into parts. ".repeat(6);
const texts = [
  "“It’s café time” — naïve…",
  "Straße æon",
  "ok \u{1F44D}\u{FE0F}",
  "\u{2705} done \u{2713} \u{2714}\u{FE0F}",
  "\u{1F44E}",
  "\u{1FAE0}\u{1F9D1}",
  "\u{1F9D1}‍\u{1F680}",
  "中文",
  "a\tb\r\nc\x07",
  "\u{1F99E} OpenClaw\n\u{23F1}\u{FE0F} Uptime\n\u{6F22}",
  "## Title\n\nSome **bold**, *em*, _also_ and `code`.\n* item\n```js\n  x = a*b*c;\n```\nsnake_case_name stays",
  "See [the docs](https://x.org/a) or <https://y.org>.",
  "[https://x.org](https://x.org)",
  "![a cat](https://x.org/cat.png \"Cat\") and [x](y \"t\")",
  "a  \n\n\n\nb\n",
  "\n\n  \nx\n\n",
  "```\n        lda #$00\nloop:   sta $0200,y\n```",
  "~~gone~~ and ~not~ and __under__ and 2*3*4 and a_b_c and _x_.",
  "+ plus bullet\n  * nested *star*\n- dash",
  "**Reminder:** water the plants \u{1F331}\n\n" + "More. ".repeat(40),
  "⚠️ **Hermes wants to run a command that needs your OK**\n```\nrm -rf /tmp/x\n```\nWhy it was flagged: deletes files\n\nReply `/approve` to run it once, `/approve session` to allow this pattern for the rest of this session, `/approve always` to allow it permanently, or `/deny` to cancel.\nIf you don't answer within 5 minutes it will NOT run.\n",
  "⚡ Interrupting the current task… ⏳ queued ⏩ \u{1F4BE}",
  "Temperature: 21°C ± 0.5, 3×4 ÷ 2 ≤ 7 → done ½ © ™ €5",
  "#### deep heading\n###### six\n####### seven",
  "| a | b |\n|---|---|\n| 1 | 2 |",
  "Fancy “quotes” ‘and’ – dashes — ellipsis…   trailing   \n",
  "Text with nbsp and thin​zero-width﻿ bom",
  "Ünïcödé ÀÉÎÕÜ çñ ø Ø ł ð þ œ",
  "\u{1F600}\u{1F603}\u{1F604}\u{1F642}\u{1F60A}\u{1F609}\u{1F602}\u{1F923}\u{1F622}\u{1F641}\u{1F914}\u{1F389}\u{1F680}",
  "\u{274C}\u{26A0}\u{2757}\u{2753}\u{1F4A1}\u{1F525}\u{2764}\u{1F64F}\u{1F44B}\u{1F4C5}\u{1F4E7}\u{1F4CC}\u{1F4DD}",
  "\u{1F50D}\u{2B50}\u{1F31F}\u{1F4AF}\u{1F527}\u{1F41B}\u{23F0}\u{1F3E0}\u{2600}\u{1F327}\u{2614}\u{2744}",
  "## Part 1\n\n" + para + "\n\n## Part 2\n\n" + para,
];
let sweep = "";
for (let c = 0; c < 0x3000; c += 7) sweep += String.fromCodePoint(c);
for (let c = 0x1f000; c < 0x1fb00; c += 13) sweep += String.fromCodePoint(c);
texts.push(sweep);

const text = texts.map((s) => ({ in: s, fold: foldAscii(s), md: plainMarkdown(s), apple: forApple(s) }));

const splits = [
  ["word ".repeat(100).trim(), 1100],
  [["word ".repeat(100).trim(), "word ".repeat(100).trim(), "word ".repeat(100).trim(), "word ".repeat(100).trim()].join("\n\n"), 1100],
  ["x".repeat(3000), 1500],
  [Array.from({ length: 800 }, (_, i) => `w${i}`).join(" "), 1500],
  [forApple("## Part 1\n\n" + para.repeat(5) + "\n\n```\n" + "  lda #$00\n".repeat(80) + "```\n" + para.repeat(3)), 1500],
  ["", 1500],
  ["short", 1500],
  ["line one\nline two\n".repeat(120), 1500],
] as [string, number][];

const labels = [
  undefined, "", "   ", "Pinchy", "Café\tBot", "A very long agent name indeed", "Fifteen chars x yz",
  "🦞 Pinchy 👍🏽", "Pinchy ✨", "  Señor\nCódigo  the Magnificent", "🏠", "Hermes™ Agent", "Hermes",
];

process.stdout.write(
  JSON.stringify(
    {
      keys: keys.map((k) => ({ key: k.toString("hex"), written: formatKey(k), id: deviceId(k) })),
      signing,
      records,
      replies,
      text,
      splits: splits.map(([s, max]) => ({ in: s, max, parts: split(s, max) })),
      labels: labels.map((l) => ({ in: l ?? null, out: displayLabel(l) })),
    },
    null,
    1,
  ) + "\n",
);
