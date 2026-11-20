"""The agent's text, turned into what the //e can draw (PROTOCOL.md section 5),
and cut into parts that fit a reply.

A port of openclaw/src/text.ts, regular expression for regular expression.
JavaScript's \\w is ASCII-only and its \\s Unicode, so \\w is written out
here as W and \\s is left to Python's Unicode default. tests/vectors.json,
made by the TypeScript, checks the two agree.
"""

from __future__ import annotations

import re
import unicodedata

from .protocol import DISPLAY_NAME_MAX, MAX_TEXT
from .tables import CHECKS, EMOJI, PUNCT

CHECK = "\x02"   # the byte the Apple draws as MouseText's check mark

W = "A-Za-z0-9_"  # JavaScript's \w

_FENCE = re.compile(r"^\s*(```|~~~)")
_HEADING = re.compile(r"^#{1,6}\s+")
_BULLET = re.compile(r"^(\s*)[*+]\s+")
_IMAGE = re.compile(r"!\[([^\]]*)\]\(([^)\s]+)[^)]*\)")
_LINK = re.compile(r"\[([^\]]+)\]\(([^)\s]+)[^)]*\)")
_CODE = re.compile(r"`([^`]+)`")
_BOLD = re.compile(r"(\*\*|__)(?=\S)(.+?)(?<=\S)\1")
_EM_STAR = re.compile(rf"(^|[^{W}*])\*(?=\S)([^*]+?)(?<=\S)\*(?![{W}])")
_EM_UNDER = re.compile(rf"(^|[^{W}])_(?=\S)([^_]+?)(?<=\S)_(?![{W}])")
_STRIKE = re.compile(r"~~(.+?)~~")


def plain_markdown(s: str) -> str:
    """Markdown to plain text: emphasis, headings, links and code fences."""
    out: list[str] = []
    in_code = False
    for line in s.split("\n"):
        if _FENCE.match(line):
            in_code = not in_code
            continue
        if in_code:
            out.append(line)
            continue
        l = _HEADING.sub("", line, count=1)
        l = _BULLET.sub(r"\1- ", l, count=1)
        l = _IMAGE.sub(r"[image: \1]", l)
        l = _LINK.sub(lambda m: m.group(2) if m.group(1) == m.group(2) else f"{m.group(1)} ({m.group(2)})", l)
        l = _CODE.sub(r"\1", l)
        l = _BOLD.sub(r"\2", l)
        l = _EM_STAR.sub(r"\1\2", l)
        l = _EM_UNDER.sub(r"\1\2", l)
        l = _STRIKE.sub(r"\1", l)
        out.append(l)
    return "\n".join(out)


def _printable(ch: str) -> bool:
    return 32 <= ord(ch) < 127


def fold_ascii(s: str) -> str:
    """Unicode to printable ASCII, tabs and CR LF tidied."""
    s = re.sub(r"\r\n?", "\n", s).replace("\t", "    ")
    out: list[str] = []
    for ch in s:
        c = ord(ch)
        if c == 10 or 32 <= c < 127:
            out.append(ch)
        elif ch in CHECKS:
            out.append(CHECK)
        elif ch in PUNCT:
            out.append(PUNCT[ch])
        elif ch in EMOJI:
            out.append(f":{EMOJI[ch]}:")
        else:
            base = re.sub("[̀-ͯ]", "", unicodedata.normalize("NFKD", ch))
            if base and all(_printable(b) for b in base):
                out.append(base)
            elif c >= 0x1F000 or 0x2300 <= c < 0x2400 or 0x2600 <= c < 0x2800 or 0x2B00 <= c < 0x2C00:
                # An emoji without a name: usually a line's marker, so drawn as a bullet.
                out.append("*")
            elif c < 32 or 127 <= c < 160:
                pass                                # control characters: dropped
            else:
                out.append("?")
    return "".join(out)


def _trim_end(s: str) -> str:
    # JavaScript's trimEnd, for text that is already ASCII.
    return s.rstrip(" \t\n\r\x0b\x0c")


def for_apple(s: str) -> str:
    """The whole treatment, for an agent's reply."""
    t = fold_ascii(plain_markdown(s))
    t = re.sub(r"[ ]+\n", "\n", t)
    t = re.sub(r"\n{3,}", "\n\n", t)
    # Blank lines at the start go, but not the first line's indent: a part
    # may start inside a code block.
    t = re.sub(r"^(?:[ ]*\n)+", "", t, count=1)
    return _trim_end(t)


def split(s: str, max_len: int = MAX_TEXT) -> list[str]:
    """Cut text into parts of at most max_len bytes, at a paragraph break if one
    is in the second half of the part, else a line break, else a space."""
    parts: list[str] = []
    while len(s) > max_len:
        window = s[:max_len + 1]
        at = window.rfind("\n\n")
        if at < max_len / 2:
            at = window.rfind("\n")
        if at < max_len / 2:
            at = window.rfind(" ")
        if at <= 0:
            at = max_len
        parts.append(_trim_end(s[:at]))
        s = re.sub(r"^[\n ]+", "", s[at:], count=1)
    if s or not parts:
        parts.append(s)
    return parts


def _pictographic(c: int) -> bool:
    """Near enough Unicode's Extended_Pictographic, which Python's re lacks,
    with the variation selector, the joiner and skin tones."""
    return (c in (0x00A9, 0x00AE, 0x203C, 0x2049, 0x2122, 0x2139, 0x2328, 0x23CF, 0x24C2, 0x25B6, 0x25C0,
                  0x2B50, 0x2B55, 0x3030, 0x303D, 0x3297, 0x3299, 0xFE0F, 0x200D)
            or 0x2194 <= c <= 0x21AA or 0x231A <= c <= 0x231B or 0x23E9 <= c <= 0x23FA
            or 0x25AA <= c <= 0x25AB or 0x25FB <= c <= 0x25FE or 0x2600 <= c <= 0x27BF
            or 0x2934 <= c <= 0x2935 or 0x2B05 <= c <= 0x2B1C or 0x1F000 <= c <= 0x1FAFF)


def display_label(name: str | None) -> str:
    """An agent's name as the Apple shows it: printable ASCII, one line, at most
    DISPLAY_NAME_MAX characters, without emoji; empty if nothing is left."""
    if not name:
        return ""
    bare = "".join(ch for ch in name if not _pictographic(ord(ch)))
    flat = re.sub(r"[^\x20-\x7e]+", " ", fold_ascii(bare))
    flat = re.sub(r" +", " ", flat).strip()
    return flat[:DISPLAY_NAME_MAX].rstrip()
