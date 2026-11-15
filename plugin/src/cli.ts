// Devices for the stand-alone server. Inside OpenClaw these become
// `openclaw a2 pair | list | revoke` (milestone 6).
//
//   node src/cli.ts pair [name]      make a key; it is shown once
//   node src/cli.ts list
//   node src/cli.ts revoke <id>
//
// --store <path> picks the file (default .dev/devices.json).

import { parseArgs } from "node:util";
import { DeviceStore } from "./devices.ts";

const { values, positionals } = parseArgs({
  allowPositionals: true,
  options: { store: { type: "string", default: ".dev/devices.json" } },
});
const store = new DeviceStore(values.store!);
const [cmd, arg] = positionals;

if (cmd === "pair") {
  const { device, written } = store.pair(arg ?? "");
  console.log(`Device ${device.id} (${device.name}).`);
  console.log(`Type this key into Twoegram. It is not shown again:\n\n    ${written}\n`);
} else if (cmd === "list") {
  for (const d of store.list()) console.log(`${d.id}  ${d.created}  ${d.name}`);
  if (!store.list().length) console.log("no devices");
} else if (cmd === "revoke" && arg) {
  console.log(store.revoke(arg) ? `revoked ${arg}` : `no device ${arg}`);
} else {
  console.log("usage: cli.ts pair [name] | list | revoke <id>   [--store path]");
  process.exitCode = 1;
}
