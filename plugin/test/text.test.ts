import { describe, expect, it } from "vitest";
import { foldAscii, forApple, plainMarkdown, split } from "../src/text.ts";

const printable = (s: string) => [...s].every((c) => c === "\n" || (c >= " " && c <= "~"));

describe("folding to ASCII", () => {
  it("handles punctuation, accents and emoji", () => {
    expect(foldAscii("“It’s café time” — naïve…")).toBe('"It\'s cafe time" -- naive...');
    expect(foldAscii("Straße æon")).toBe("Strasse aeon");
    expect(foldAscii("ok \u{1F44D}\u{FE0F}")).toBe("ok :thumbs-up:");
    expect(foldAscii("\u{1FAE0}\u{1F9D1}")).toBe("??");
    // A joined emoji folds part by part; the joiner itself vanishes.
    expect(foldAscii("\u{1F9D1}‍\u{1F680}")).toBe("?:rocket:");
    expect(foldAscii("中文")).toBe("??");
    expect(foldAscii("a\tb\r\nc\x07")).toBe("a    b\nc");
  });

  it("never leaves anything the //e cannot draw", () => {
    let s = "";
    for (let c = 0; c < 0x3000; c += 7) s += String.fromCodePoint(c);
    s += "\u{1F600}\u{1F4A9}\u{1FAE0}";
    expect(printable(foldAscii(s))).toBe(true);
  });
});

describe("Markdown to plain text", () => {
  it("drops emphasis, headings and fences, keeps code as it is", () => {
    const md = "## Title\n\nSome **bold**, *em*, _also_ and `code`.\n* item\n```js\n  x = a*b*c;\n```\nsnake_case_name stays";
    expect(plainMarkdown(md)).toBe("Title\n\nSome bold, em, also and code.\n- item\n  x = a*b*c;\nsnake_case_name stays");
  });

  it("writes links as text and address", () => {
    expect(plainMarkdown("See [the docs](https://x.org/a) or <https://y.org>."))
      .toBe("See the docs (https://x.org/a) or <https://y.org>.");
    expect(plainMarkdown("[https://x.org](https://x.org)")).toBe("https://x.org");
  });
});

describe("for the Apple", () => {
  it("tidies blank lines and trailing spaces", () => {
    expect(forApple("a  \n\n\n\nb\n")).toBe("a\n\nb");
    expect(forApple("\n\n  \nx\n\n")).toBe("x");
  });

  it("keeps the indent of a chunk that starts with code", () => {
    expect(forApple("```\n        lda #$00\nloop:   sta $0200,y\n```")).toBe("        lda #$00\nloop:   sta $0200,y");
  });
});

describe("splitting", () => {
  it("leaves a short text whole", () => {
    expect(split("hello")).toEqual(["hello"]);
    expect(split("")).toEqual([""]);
  });

  it("cuts at a paragraph, then a line, then a space, then anywhere", () => {
    const para = "word ".repeat(100).trim(); // 499
    const parts = split([para, para, para, para].join("\n\n"), 1100);
    expect(parts.length).toBe(2);
    expect(parts[0]).toBe([para, para].join("\n\n"));
    expect(parts.every((p) => p.length <= 1100)).toBe(true);

    const solid = "x".repeat(3000);
    expect(split(solid, 1500)).toEqual(["x".repeat(1500), "x".repeat(1500)]);
  });

  it("keeps every word", () => {
    const text = Array.from({ length: 800 }, (_, i) => `w${i}`).join(" ");
    const parts = split(text, 1500);
    expect(parts.join(" ")).toBe(text);
    expect(parts.every((p) => p.length <= 1500)).toBe(true);
  });
});
