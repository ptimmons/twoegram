// The /a2 endpoints on their own, answered by the echo agent: for
// developing the Apple side before the plugin is inside OpenClaw.
//
//   node src/dev-server.ts [--host 127.0.0.1] [--port 8417] [--store .dev/devices.json]
//
// It listens on loopback unless told otherwise. Give --host 0.0.0.0 (or
// the Mac's LAN address) for the Apple to reach it, and never forward the
// port to the internet: the protocol signs but does not encrypt.

import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
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

// Which code is answering: the package version and the git commit, with
// "+" when the working tree has changes since it. Sent in every hello
// reply, so the probe's photo says which server it talked to.
function ident(): string {
  const here = new URL("..", import.meta.url).pathname;
  const version = JSON.parse(readFileSync(new URL("../package.json", import.meta.url), "utf8")).version;
  let commit = "no-git";
  try {
    const git = (...args: string[]) => execFileSync("git", ["-C", here, ...args], { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] }).trim();
    commit = git("rev-parse", "--short", "HEAD") + (git("status", "--porcelain", "--", ".") ? "+" : "");
  } catch {
    // copied without .git: no commit to report
  }
  return `twoegram-dev ${version} ${commit}`;
}

const devices = new DeviceStore(values.store!);
const id = ident();
const connNumber = new WeakMap<object, number>();
const server = new A2Server({
  devices,
  agents: new EchoAgent(["main", "coder"], Number(values.delay)),
  log: (l) => console.log(`${new Date().toISOString()} ${l}`),
  ident: id,
  connOf: (req) => connNumber.get(req.socket),
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
// Each new TCP connection is logged, so a probe run shows whether the
// card's keep-alive kept one connection across requests.
let connections = 0;
http.on("connection", (sock) => {
  const n = ++connections;
  connNumber.set(sock, n); // each reply says which connection it came on
  const from = `${sock.remoteAddress}:${sock.remotePort}`;
  console.log(`${new Date().toISOString()} connection ${n} opened from ${from}`);
  sock.on("close", () => console.log(`${new Date().toISOString()} connection ${n} closed`));
});
http.listen(Number(values.port), values.host, () => {
  console.log(`${id} on http://${values.host}:${values.port}/a2/ with ${devices.list().length} device(s): ${devices.list().map((d) => d.id).join(" ")}`);
});
