// The /a2 endpoints on their own, answered by the echo agent: for
// developing the Apple side before the plugin is inside OpenClaw.
//
//   node src/dev-server.ts [--host 127.0.0.1] [--port 8417] [--store .dev/devices.json]
//
// It listens on loopback unless told otherwise. Give --host 0.0.0.0 (or
// the Mac's LAN address) for the Apple to reach it, and never forward the
// port to the internet: the protocol signs but does not encrypt.

import { createServer } from "node:http";
import { parseArgs } from "node:util";
import { EchoAgent } from "./agent.ts";
import { DeviceStore } from "./devices.ts";
import { A2Server } from "./server.ts";

const { values } = parseArgs({
  options: {
    host: { type: "string", default: "127.0.0.1" },
    port: { type: "string", default: "8417" },
    store: { type: "string", default: ".dev/devices.json" },
    delay: { type: "string", default: "800" },
  },
});

const devices = new DeviceStore(values.store!);
const server = new A2Server({
  devices,
  agents: new EchoAgent(["main", "coder"], Number(values.delay)),
  log: (l) => console.log(`${new Date().toISOString()} ${l}`),
});

const http = createServer((req, res) => {
  if (!req.url?.startsWith("/a2/")) {
    res.writeHead(404).end();
    return;
  }
  devices.reload(); // pick up `npm run pair` without a restart
  void server.handle(req, res);
});
http.keepAliveTimeout = 10_000; // longer than the card's 8 s cutoff
http.listen(Number(values.port), values.host, () => {
  console.log(`a2 dev server on http://${values.host}:${values.port}/a2/ with ${devices.list().length} device(s)`);
});
