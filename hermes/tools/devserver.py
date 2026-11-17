"""The Python a2 server on its own, with the echo agent, for trying a client
against it without Hermes (as plugin/src/dev-server.ts is for the TypeScript).

    python3 hermes/tools/devserver.py [--port 8417] [--store .dev-py] [--seed N]
    python3 hermes/tools/devserver.py --store .dev-py pair

Standard library only.
"""

import argparse
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from twoegram.echo import EchoAgent  # noqa: E402
from twoegram.server import A2Server, HttpListener  # noqa: E402
from twoegram.store import ConvStore, DeviceStore  # noqa: E402



async def serve(a):
    store = DeviceStore(Path(a.store) / "devices.json")
    echo = EchoAgent(delay=0.8)
    server = A2Server(store, echo, convs=ConvStore(Path(a.store) / "conversations"), seed=a.seed,
                      log=print, ident="twoegram-dev-py 1.0.0")
    echo.server = server
    listener = HttpListener(server.handle, 1024)
    await listener.start(a.host, a.port)
    print(f"a2 dev server on http://{a.host}:{listener.port}/a2/ ({len(store.list())} device(s))", flush=True)
    await asyncio.Event().wait()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8417)
    ap.add_argument("--store", default=".dev-py")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("cmd", nargs="?", choices=["pair"])
    a = ap.parse_args()
    if a.cmd == "pair":
        dev, written = DeviceStore(Path(a.store) / "devices.json").pair("dev")
        print(written)
        return
    asyncio.run(serve(a))


if __name__ == "__main__":
    main()
