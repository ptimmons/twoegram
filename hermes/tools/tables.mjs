// Prints plugin/src/text.ts's tables as JSON, read from the source itself (for hermes/tools/tables.py).
import { readFileSync } from "node:fs";
const src = readFileSync(process.argv[2], "utf8");
const grab = (name) => {
  const m = new RegExp(`const ${name}[^=]*=\\s*(\\{[\\s\\S]*?\\n\\});`).exec(src) ?? new RegExp(`const ${name}[^=]*=\\s*(new Set\\(\\[[\\s\\S]*?\\]\\));`).exec(src);
  return eval("(" + m[1] + ")");
};
const out = { PUNCT: grab("PUNCT"), EMOJI: grab("EMOJI"), CHECKS: [...grab("CHECKS")] };
process.stdout.write(JSON.stringify(out));
