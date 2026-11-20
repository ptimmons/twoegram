#!/usr/bin/env python3
"""cc65check.py file.s... -- find a known cc65 miscompile in generated assembly.

This cc65 (it reports V2.18) can emit

        ldy     #$00
        lda     ...             ; nothing that touches Y
        jsr     ldaxysp

ldaxysp loads A/X from the C stack with the HIGH byte at offset Y and the
low byte at Y-1, so Y must be at least 1. With Y = 0 it reads one byte from
below the stack, and the "pointer" points anywhere: in newsiie it sent a
memcpy() into the program's own code. This walks each straight-line run of
code, tracks a Y set by a constant load, and reports any ldaxysp (or
staxysp, the matching store) called with Y = 0.

It also refuses any function that returns a struct or union by value.
cc65 2.18 compiled

        static cogs_part_t part(const void *p, uint16_t n)   (4 bytes)

to build the struct in memory and return only its first two bytes in
A/X, never loading sreg with the other two, so every caller took a stale
sreg as the length. On the card that hashed the wrong bytes (Twoegram,
milestone 2). tests/cc65/struct_return.c reproduces it. cc65 marks each
function with a comment giving its prototype; a return type of struct or
union there is reported.

And it refuses data at a function's entry. In a file whose code and
constants share one segment (#pragma code-name and rodata-name the same,
as in the set-up overlay), cc65 emits a function's own static const
table into that segment right after the function's label, ahead of its
first instruction. A call then runs the table as code. Twoegram b037:
write_key's base-32 alphabet ran as code, pushing a byte with PHA ('H')
and writing zero page, and Open-Apple-V crashed on the real Apple. Some
of those bytes are undefined opcodes, which emulators decode
differently, so it worked in Virtual ][. The cure is a constant at file
scope.

And it refuses a branch straight after a call to a C function. cc65
compiled

        if (a == cur ? cur_news() : agent_unread(a))

to call either function and then beq, testing no register first. A
function with arguments returns through "jmp incsp1", whose inc sp sets
the flags from the stack pointer, so the branch read nearly every count
as nonzero: Twoegram b048 drew an unread diamond beside every agent but
the open one. tests/cc65/ternary_flags.c reproduces it. The call may also
reach the branch by a bra or jmp to it. The cure is the value in a
variable, tested on its own.

Exit status 1 if any are found.

tests/cc65/ldaxysp_y0.c reproduces it. With Homebrew's cc65 2.19 bottle it
needs -Cl (static locals) and the optimizer: -O, -Or and -Os miscompile it;
-Oirs, no -O, and dropping -Cl do not. The newest numbered release is still
2.19 (2020). Upstream fixed a related ldaxysp bug in the same optimizer pass
(cc65 PR #2951, issues #2942 and #2461, merged 2026-04-07, development
snapshots only); whether that also fixes this one is untested.
"""
import re
import sys

# Instructions that change Y in a way we cannot follow statically.
Y_UNKNOWN = re.compile(r"^\s+(tay|ply|ldy\s+[^#]|jsr|jmp)\b")
Y_CONST = re.compile(r"^\s+ldy\s+#\$([0-9A-Fa-f]+)")
Y_INC = re.compile(r"^\s+iny\b")
Y_DEC = re.compile(r"^\s+dey\b")
LABEL = re.compile(r"^[A-Za-z_.@][\w.@]*:")
WANT = re.compile(r"^\s+jsr\s+(ldaxysp|staxysp|ldeaxysp)\b")
STRUCT_RET = re.compile(r"^; (struct|union) \S+ .*?(\w+) \(")

def check(path):
    problems = []
    y = None
    proc = "?"
    for n, line in enumerate(open(path), 1):
        m = STRUCT_RET.match(line)
        if m:
            problems.append(f"{path}:{n}: {m.group(2)} returns a {m.group(1)} by value, which cc65 miscompiles")
        m = re.match(r"^\.proc\s+(\S+)", line)
        if m:
            proc = m.group(1).rstrip(":")
        if LABEL.match(line):
            y = None            # a branch target: Y could be anything
        if WANT.match(line):
            if y == 0:
                problems.append(f"{path}:{n}: {proc}: {WANT.match(line).group(1)} with Y = 0")
            y = None
            continue
        m = Y_CONST.match(line)
        if m:
            y = int(m.group(1), 16)
        elif Y_INC.match(line) and y is not None:
            y = (y + 1) & 0xFF
        elif Y_DEC.match(line) and y is not None:
            y = (y - 1) & 0xFF
        elif Y_UNKNOWN.match(line):
            y = None
    return problems

SEGMENT = re.compile(r'^\.segment\s+"(\w+)"')
DATA = re.compile(r"^(?:[A-Za-z_.@][\w.@]*:)?\s*\.(byte|word|addr|dword|res)\b")
INSN = re.compile(r"^(?:[A-Za-z_.@][\w.@]*:)?\s+[a-z]{3}\b")

def check_entries(path):
    """Data in a function's own segment before its first instruction."""
    problems = []
    seg = "CODE"
    proc = None          # (name, its segment), until its first instruction
    cur = seg
    for n, line in enumerate(open(path), 1):
        m = SEGMENT.match(line)
        if m:
            cur = m.group(1)
            if proc is None:
                seg = cur
            continue
        m = re.match(r"^\.proc\s+(\S+)", line)
        if m:
            proc = (m.group(1).rstrip(":"), seg)
            cur = seg
            continue
        if proc is None or cur != proc[1]:
            continue
        if DATA.match(line):
            problems.append(f"{path}:{n}: {proc[0]}: data at its entry, which runs as code: "
                            "move its static const to file scope")
            proc = None
        elif INSN.match(line):
            proc = None
    return problems

BRANCH = re.compile(r"^\s+(beq|bne|bmi|bpl|bcc|bcs|bvc|bvs)\b")
CALL = re.compile(r"^(?:[A-Za-z_.@][\w.@]*:)?\s+jsr\s+(_\w+)")
GOTO = re.compile(r"^\s+(?:bra|jmp)\s+(L\w+)\s*$")

def check_flags(path):
    """A conditional branch on the flags a C function's return left."""
    lines = open(path).read().split("\n")
    labels = {}
    for i, line in enumerate(lines):
        m = LABEL.match(line)
        if m:
            labels[m.group(0)[:-1]] = i

    def insn(i):
        """The first instruction from line i on, without its label."""
        while i < len(lines):
            text = LABEL.sub("", lines[i], count=1)
            if text.strip() and not text.strip().startswith(";"):
                return text
            i += 1
        return ""

    problems = []
    proc = "?"
    for n, line in enumerate(lines, 1):
        m = re.match(r"^\.proc\s+(\S+)", line)
        if m:
            proc = m.group(1).rstrip(":")
        m = CALL.match(line)
        if not m:
            continue
        nxt = insn(n)
        g = GOTO.match(nxt)
        if g and g.group(1) in labels:
            nxt = insn(labels[g.group(1)])
        if BRANCH.match(nxt):
            problems.append(f"{path}:{n}: {proc}: {BRANCH.match(nxt).group(1)} on the flags {m.group(1)} "
                            "returned with: test its value in a variable")
    return problems

def main():
    found = []
    for path in sys.argv[1:]:
        found += check(path) + check_entries(path) + check_flags(path)
    for p in found:
        print(p)
    print(f"cc65check: {len(found)} problem(s) in {len(sys.argv) - 1} file(s)")
    return 1 if found else 0

if __name__ == "__main__":
    sys.exit(main())
