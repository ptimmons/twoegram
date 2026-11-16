#!/usr/bin/env python3
"""a2client.py -- talk to the a2 channel as the Apple will (docs/PROTOCOL.md).

Standard library only, written independently of the plugin's TypeScript so
the two check each other.

    a2client.py --url http://127.0.0.1:8417 --key 7M2QK-...-3$ hello
    a2client.py ... send main "What's on the calendar?"   (then waits for the answer)
    a2client.py ... poll main [after]
    a2client.py ... hist main [before]
    a2client.py ... chat main                              (type lines; Ctrl-D ends)

The key can also come from the A2_KEY environment variable.
"""
import argparse
import hashlib
import hmac
import http.client
import os
import secrets
import struct
import sys
import time
import urllib.parse

ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
CHECK = ALPHABET + "*~$=U"


def parse_key(text):
    s = text.upper().replace("-", "").replace(" ", "").replace("O", "0").replace("I", "1").replace("L", "1")
    if len(s) != 27:
        raise SystemExit("a key is 27 characters")
    vals = [ALPHABET.index(c) for c in s[:26] if c in ALPHABET]
    if len(vals) != 26:
        raise SystemExit("a key has a character that cannot be in one")
    if CHECK.index(s[26]) != sum((i + 1) * v for i, v in enumerate(vals)) % 37:
        raise SystemExit("the key does not check: a typing slip?")
    bits = "".join(f"{v:05b}" for v in vals)[:128]
    return int(bits, 2).to_bytes(16, "big")


def mac(key, *parts):
    h = hmac.new(key, digestmod=hashlib.sha256)
    for p in parts:
        h.update(p if isinstance(p, bytes) else p.encode("latin-1"))
    return h.digest()


def parse_reply(body):
    nl = body.index(b"\n")
    head = body[:nl].decode("latin-1").split(" ")
    if len(head) != 4 or head[0] != "A2":
        raise ValueError("bad header line")
    payload = body[nl + 1:]
    lines, msgs, i = [], [], 0
    while i < len(payload):
        e = payload.index(b"\n", i)
        line = payload[i:e].decode("latin-1")
        i = e + 1
        word, _, value = line.partition(" ")
        if word == "msg":
            n, mine, hhmm, ln = value.split(" ")
            ln = int(ln)
            if payload[i + ln:i + ln + 1] != b"\n":
                raise ValueError("bad msg length")
            msgs.append((int(n), mine == "1", hhmm, payload[i:i + ln].decode("latin-1")))
            i += ln + 1
        else:
            lines.append((word, value))
    return int(head[2]), None if head[3] == "-" else head[3], payload, lines, msgs


CODES = ["OK", "BAD_REQUEST", "UNKNOWN_DEVICE", "BAD_SIGNATURE", "NO_SESSION", "REPLAY",
         "LOCKED", "UNKNOWN_AGENT", "TOO_LARGE", "BUSY"]


class Client:
    def __init__(self, url, key):
        u = urllib.parse.urlsplit(url)
        self.conn = http.client.HTTPConnection(u.hostname, u.port or 80, timeout=15)
        self.key = key
        self.id = hashlib.sha256(b"twoegram-id" + key).digest()[:4].hex()
        self.sid, self.sk, self.seq, self.agents = None, None, 0, []

    def _get(self, method, target, headers=None, body=b""):
        self.conn.request(method, target, body=body, headers=headers or {})
        r = self.conn.getresponse()
        return r.read()

    def hello(self):
        cn = secrets.token_bytes(16)
        target = f"/a2/hello?d={self.id}&c={cn.hex()}&m={mac(self.key, 'hello', cn).hex()}"
        code, sig, payload, lines, _ = parse_reply(self._get("GET", target))
        if code:
            raise SystemExit(f"hello refused: {CODES[code]} {dict(lines).get('why', '')} (unsigned)")
        d = {}
        for k, v in lines:
            d.setdefault(k, []).append(v)
        sk = mac(self.key, "session", cn, bytes.fromhex(d["sn"][0]))
        if not hmac.compare_digest(mac(sk, "h", payload).hex(), sig or ""):
            raise SystemExit("the hello reply is not genuine")
        self.sid, self.sk, self.seq, self.agents = d["sid"][0], sk, 0, d.get("agent", [])
        return d

    def call(self, method, target, body=b""):
        for attempt in range(2):
            if not self.sk:
                self.hello()
            self.seq += 1
            seq = self.seq
            sig = mac(self.sk, "q", struct.pack(">I", seq), method, " ", target, "\n", body).hex()
            raw = self._get(method, target, {"X-A2-Session": self.sid, "X-A2-Seq": str(seq), "X-A2-Mac": sig}, body)
            code, rsig, payload, lines, msgs = parse_reply(raw)
            genuine = rsig is not None and hmac.compare_digest(
                mac(self.sk, "r", struct.pack(">I", seq), payload).hex(), rsig)
            if code in (4, 5) and attempt == 0:      # NO_SESSION, REPLAY: hello again
                self.sk = None
                continue
            if code and not genuine:
                raise SystemExit(f"refused: {CODES[code]} (unsigned, so not to be believed further)")
            if not genuine:
                raise SystemExit("a reply that is not genuine: stopping")
            if code:
                raise SystemExit(f"refused: {CODES[code]} {dict(lines).get('why', '')}")
            return dict(lines), msgs
        raise SystemExit("could not keep a session")


def show(msgs):
    for n, mine, hhmm, text in msgs:
        who = "you" if mine else "agent"
        print(f"[{n} {hhmm} {who}]")
        # byte 2 is the check mark the Apple draws in MouseText
        for line in text.replace("\x02", "✓").split("\n"):
            print(("        " if mine else "  ") + line)


def wait_for_answer(c, agent, after, quiet=6.0):
    """Poll until the agent is no longer busy and nothing new comes."""
    last = after
    idle_since = time.time()
    while True:
        lines, msgs = c.call("GET", f"/a2/poll?a={agent}&after={last}&w=4000")
        show(msgs)
        if msgs:
            last = msgs[-1][0]
            idle_since = time.time()
        if lines.get("more") == "1":
            continue
        if lines.get("busy") == "0" and (msgs or time.time() - idle_since > quiet or last > after):
            return last
        if time.time() - idle_since > 60:
            print("(no answer in a minute)")
            return last


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=os.environ.get("A2_URL", "http://127.0.0.1:8417"))
    ap.add_argument("--key", default=os.environ.get("A2_KEY"))
    ap.add_argument("cmd", choices=["hello", "send", "poll", "hist", "chat"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    if not a.key:
        raise SystemExit("give --key or set A2_KEY")
    c = Client(a.url, parse_key(a.key))

    if a.cmd == "hello":
        d = c.hello()
        print(f"session {c.sid}, device {c.id}, agents: {' '.join(c.agents)}, gateway time {d['time'][0]}")
    elif a.cmd == "send":
        agent, text = a.args[0], " ".join(a.args[1:])
        _, msgs = c.call("POST", f"/a2/send?a={agent}", text.encode("ascii"))
        show(msgs)
        wait_for_answer(c, agent, msgs[0][0])
    elif a.cmd == "poll":
        lines, msgs = c.call("GET", f"/a2/poll?a={a.args[0]}&after={a.args[1] if len(a.args) > 1 else 0}")
        print(" ".join(f"{k}={v}" for k, v in lines.items()))
        show(msgs)
    elif a.cmd == "hist":
        before = f"&before={a.args[1]}" if len(a.args) > 1 else ""
        lines, msgs = c.call("GET", f"/a2/hist?a={a.args[0]}{before}&k=50")
        print(" ".join(f"{k}={v}" for k, v in lines.items()))
        show(msgs)
    elif a.cmd == "chat":
        agent = a.args[0]
        lines, msgs = c.call("GET", f"/a2/hist?a={agent}&k=20")
        show(msgs)
        for text in sys.stdin:
            text = text.rstrip("\n")
            if not text:
                continue
            _, msgs = c.call("POST", f"/a2/send?a={agent}", text.encode("ascii", "replace"))
            wait_for_answer(c, agent, msgs[0][0])


if __name__ == "__main__":
    main()
