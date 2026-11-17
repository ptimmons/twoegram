"""Hermes's own source, for the adapter tests: HERMES_SRC, else the clone in
.sdk/hermes-agent (at the commit the Mac mini runs; see hermes/README.md).
Without it, only the protocol tests run."""

import os
import sys
import tempfile
from pathlib import Path

# Importing Hermes makes a Hermes home if there is none: never the real
# ~/.hermes from a test run.
os.environ["HERMES_HOME"] = tempfile.mkdtemp(prefix="hermes-home-")

src = Path(os.environ.get("HERMES_SRC", Path(__file__).resolve().parents[2] / ".sdk" / "hermes-agent"))
if (src / "gateway" / "platforms" / "base.py").exists() and str(src) not in sys.path:
    sys.path.insert(0, str(src))
