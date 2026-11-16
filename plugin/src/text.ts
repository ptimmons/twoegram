// The agent's text, turned into what the //e can draw (PROTOCOL.md
// section 5), and cut into parts that fit a reply.

import { MAX_TEXT } from "./protocol.ts";

const PUNCT: Record<string, string> = {
  "‘": "'", "’": "'", "‚": "'", "‛": "'", "′": "'",
  "“": '"', "”": '"', "„": '"', "″": '"', "«": '"', "»": '"',
  "‐": "-", "‑": "-", "‒": "-", "–": "-", "—": "--", "―": "--", "−": "-",
  "…": "...", "•": "*", "·": "*", "‣": "*", "●": "*", "◦": "*",
  " ": " ", " ": " ", " ": " ", " ": " ", " ": " ", " ": " ",
  "​": "", "‌": "", "‍": "", "﻿": "", "️": "", "︎": "",
  "×": "x", "÷": "/", "°": " degrees", "©": "(c)", "®": "(R)", "™": "(TM)",
  "½": "1/2", "¼": "1/4", "¾": "3/4", "€": "EUR", "£": "GBP", "¥": "JPY",
  "←": "<-", "→": "->", "↑": "^", "↓": "v", "⇒": "=>", "≤": "<=", "≥": ">=",
  "≠": "!=", "≈": "~", "ß": "ss", "æ": "ae", "Æ": "AE", "œ": "oe", "Œ": "OE",
  "ø": "o", "Ø": "O", "ł": "l", "Ł": "L", "ð": "d", "Ð": "D", "þ": "th",
  "✗": "x", "✘": "x",
};

/** The byte the Apple draws as MouseText's check mark (PROTOCOL.md 5). */
export const CHECK = "\x02";

/** Drawn as the check mark: thumbs-up and the check marks. */
const CHECKS = new Set(["\u{1F44D}", "\u{2705}", "\u{2714}", "\u{2713}", "\u{2611}"]);

const EMOJI: Record<string, string> = {
  "\u{1F44E}": "thumbs-down", "\u{1F600}": "grin", "\u{1F603}": "smile",
  "\u{1F604}": "smile", "\u{1F642}": "smile", "\u{1F60A}": "smile", "\u{1F609}": "wink",
  "\u{1F602}": "laugh", "\u{1F923}": "laugh", "\u{1F622}": "sad", "\u{1F641}": "sad",
  "\u{1F914}": "thinking", "\u{1F389}": "party", "\u{1F680}": "rocket",
  "\u{274C}": "no", "\u{26A0}": "warning", "\u{2757}": "!", "\u{2753}": "?", "\u{1F4A1}": "idea",
  "\u{1F525}": "fire", "\u{2764}": "heart", "\u{1F64F}": "thanks", "\u{1F44B}": "wave",
  "\u{1F4C5}": "calendar", "\u{1F4E7}": "email", "\u{1F4CC}": "pin", "\u{1F4DD}": "note",
  "\u{1F50D}": "search", "\u{2B50}": "star", "\u{1F31F}": "star", "\u{1F4AF}": "100",
  "\u{1F527}": "wrench", "\u{1F41B}": "bug", "\u{23F0}": "alarm", "\u{1F3E0}": "house",
  "\u{2600}": "sun", "\u{1F327}": "rain", "\u{2614}": "rain", "\u{2744}": "snow",
};

/** Markdown to plain text: emphasis, headings, links and code fences. */
export function plainMarkdown(s: string): string {
  const out: string[] = [];
  let inCode = false;
  for (const line of s.split("\n")) {
    if (/^\s*(```|~~~)/.test(line)) {
      inCode = !inCode;
      continue;
    }
    if (inCode) {
      out.push(line);
      continue;
    }
    let l = line;
    l = l.replace(/^#{1,6}\s+/, "");
    l = l.replace(/^(\s*)[*+]\s+/, "$1- ");
    l = l.replace(/!\[([^\]]*)\]\(([^)\s]+)[^)]*\)/g, "[image: $1]");
    l = l.replace(/\[([^\]]+)\]\(([^)\s]+)[^)]*\)/g, (_m, t: string, u: string) => (t === u ? u : `${t} (${u})`));
    l = l.replace(/`([^`]+)`/g, "$1");
    l = l.replace(/(\*\*|__)(?=\S)(.+?)(?<=\S)\1/g, "$2");
    l = l.replace(/(^|[^\w*])\*(?=\S)([^*]+?)(?<=\S)\*(?!\w)/g, "$1$2");
    l = l.replace(/(^|\W)_(?=\S)([^_]+?)(?<=\S)_(?!\w)/g, "$1$2");
    l = l.replace(/~~(.+?)~~/g, "$1");
    out.push(l);
  }
  return out.join("\n");
}

/** Unicode to printable ASCII, tabs and CR LF tidied. */
export function foldAscii(s: string): string {
  s = s.replace(/\r\n?/g, "\n").replace(/\t/g, "    ");
  let out = "";
  for (const ch of s) {
    const c = ch.codePointAt(0)!;
    if (c === 10 || (c >= 32 && c < 127)) {
      out += ch;
    } else if (CHECKS.has(ch)) {
      out += CHECK;
    } else if (ch in PUNCT) {
      out += PUNCT[ch];
    } else if (ch in EMOJI) {
      out += `:${EMOJI[ch]}:`;
    } else {
      const base = ch.normalize("NFKD").replace(/[̀-ͯ]/g, "");
      if (base.length && [...base].every((b) => b.codePointAt(0)! >= 32 && b.codePointAt(0)! < 127)) {
        out += base;
      } else if (c >= 0x1f000 || (c >= 0x2300 && c < 0x2400) || (c >= 0x2600 && c < 0x2800) || (c >= 0x2b00 && c < 0x2c00)) {
        // An emoji without a name: usually a line's marker (OpenClaw's
        // /status starts each line with one), so drawn as a bullet.
        out += "*";
      } else if (c < 32 || (c >= 127 && c < 160)) {
        // control characters: dropped
      } else {
        out += "?";
      }
    }
  }
  return out;
}

/** The whole treatment, for an agent's reply. */
export function forApple(s: string): string {
  // Blank lines at either end go, but not the first line's indent: a
  // chunk may start inside a code block.
  return foldAscii(plainMarkdown(s))
    .replace(/[ ]+\n/g, "\n")
    .replace(/\n{3,}/g, "\n\n")
    .replace(/^(?:[ ]*\n)+/, "")
    .trimEnd();
}

/**
 * Cut text into parts of at most max bytes, at a paragraph break if one is
 * in the second half of the part, else a line break, else a space.
 */
export function split(s: string, max = MAX_TEXT): string[] {
  const parts: string[] = [];
  while (s.length > max) {
    const window = s.slice(0, max + 1);
    let at = window.lastIndexOf("\n\n");
    if (at < max / 2) at = window.lastIndexOf("\n");
    if (at < max / 2) at = window.lastIndexOf(" ");
    if (at <= 0) at = max;
    parts.push(s.slice(0, at).trimEnd());
    s = s.slice(at).replace(/^[\n ]+/, "");
  }
  if (s.length || !parts.length) parts.push(s);
  return parts;
}
