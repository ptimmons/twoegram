"""Writes hermes/twoegram/tables.py from plugin/src/text.ts's tables, read by
tables.mjs, so the two foldings use the same characters.

    python3 hermes/tools/tables.py
"""

import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

d = json.loads(subprocess.run(["node", str(ROOT / "hermes/tools/tables.mjs"), str(ROOT / "plugin/src/text.ts")],
                              check=True, capture_output=True, text=True).stdout)
out = ["# Generated from plugin/src/text.ts's tables by hermes/tools/tables.py: do not edit.", ""]
out.append("PUNCT = {")
out += [f"    {ascii(k)}: {ascii(v)}," for k, v in d["PUNCT"].items()]
out.append("}\n")
out.append("EMOJI = {")
out += [f"    {ascii(k)}: {ascii(v)}," for k, v in d["EMOJI"].items()]
out.append("}\n")
out.append("CHECKS = frozenset({" + ", ".join(ascii(c) for c in d["CHECKS"]) + "})")
(ROOT / "hermes/twoegram/tables.py").write_text("\n".join(out) + "\n")
print("wrote hermes/twoegram/tables.py")
