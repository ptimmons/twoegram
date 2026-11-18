"""The Python port against what the TypeScript plugin computes (vectors.json,
from hermes/tools/vectors.ts), and the protocol's own properties."""

import json
import random
from pathlib import Path

import pytest

from twoegram import protocol as p
from twoegram import text as t

V = json.loads((Path(__file__).parent / "vectors.json").read_text())


@pytest.mark.parametrize("k", V["keys"], ids=lambda k: k["id"])
def test_keys(k):
    key = bytes.fromhex(k["key"])
    assert p.format_key(key) == k["written"]
    assert p.parse_key(k["written"]) == key
    assert p.device_id(key) == k["id"]


@pytest.mark.parametrize("d", V["derived"], ids=lambda d: d["name"] + "/" + d["id"])
def test_derived_keys(d):
    key = p.derive_key(bytes.fromhex(d["master"]), d["name"])
    assert key.hex() == d["key"]
    assert p.format_key(key) == d["written"] and p.device_id(key) == d["id"]


def test_derive_refuses_what_the_apple_could_not_type():
    for bad in ["", "Claw1", "claw 1", "x" * 17, "caf\u00e9"]:
        with pytest.raises(ValueError):
            p.derive_key(bytes(16), bad)
    with pytest.raises(ValueError):
        p.derive_key(bytes(15), "claw1")


@pytest.mark.parametrize("s", V["signing"], ids=lambda s: str(s["seq"]))
def test_signing(s):
    key, cn, sn = (bytes.fromhex(s[x]) for x in ("key", "cn", "sn"))
    assert p.hello_proof(key, cn).hex() == s["proof"]
    sk = p.session_key(key, cn, sn)
    assert sk.hex() == s["sk"]
    body, payload = bytes.fromhex(s["body"]), bytes.fromhex(s["payload"])
    assert p.request_mac(sk, s["seq"], s["method"], s["target"], body).hex() == s["request"]
    assert p.reply_mac(sk, s["seq"], payload).hex() == s["reply"]
    assert p.hello_mac(sk, payload).hex() == s["hello"]


def test_records_and_replies():
    for r in V["records"]:
        m = p.Msg(**r["msg"])
        assert p.msg_record(m).hex() == r["record"]
    for r in V["replies"]:
        mac = bytes.fromhex(r["mac"]) if r["mac"] else None
        raw = p.reply(r["code"], mac, r["payload"].encode())
        assert raw.hex() == r["bytes"]
        parsed = p.parse_reply(raw)
        assert (parsed.code, parsed.mac, parsed.payload) == (r["code"], r["mac"], r["payload"].encode())


@pytest.mark.parametrize("c", V["text"], ids=lambda c: repr(c["in"][:20]))
def test_text(c):
    assert t.fold_ascii(c["in"]) == c["fold"]
    assert t.plain_markdown(c["in"]) == c["md"]
    assert t.for_apple(c["in"]) == c["apple"]


@pytest.mark.parametrize("c", V["splits"], ids=lambda c: f"{len(c['in'])}/{c['max']}")
def test_split(c):
    assert t.split(c["in"], c["max"]) == c["parts"]


@pytest.mark.parametrize("c", V["labels"], ids=lambda c: repr(c["in"]))
def test_labels(c):
    assert t.display_label(c["in"]) == c["out"]


def test_key_round_trip_and_slips():
    rnd = random.Random(1)
    for _ in range(50):
        key = bytes(rnd.randrange(256) for _ in range(16))
        w = p.format_key(key)
        assert p.parse_key(w) == key
        sloppy = w.lower().replace("-", " ").replace("0", "o").replace("1", "l")
        assert p.parse_key(sloppy) == key
        flat = w.replace("-", "")
        for pos in range(26):
            for ch in p.ALPHABET:
                if ch != flat[pos]:
                    assert p.parse_key(flat[:pos] + ch + flat[pos + 1:]) is None
            if pos < 25 and flat[pos] != flat[pos + 1]:
                assert p.parse_key(flat[:pos] + flat[pos + 1] + flat[pos] + flat[pos + 2:]) is None
    assert p.parse_key("ABC") is None
    assert p.parse_key("U" * 27) is None


def test_mac_equals_only_well_formed_hex():
    mac = bytes([0xAB] * 32)
    assert p.mac_equals(mac, mac.hex())
    assert not p.mac_equals(mac, mac.hex().upper())
    assert not p.mac_equals(mac, None)
    assert not p.mac_equals(mac, "ab")


def test_parse_refuses_bad_length():
    with pytest.raises(ValueError):
        p.parse_reply(b"A2 1 0 -\nmsg 1 0 10:00 5\nabc\n")
