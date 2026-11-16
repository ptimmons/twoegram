// `openclaw a2 pair | list | revoke`, run on the gateway host. They edit
// the device file; the running gateway notices the change on its next
// request, so no restart is needed.

import { networkInterfaces } from "node:os";
import { join } from "node:path";
import { FileConvStore } from "./convstore.ts";
import { DeviceStore } from "./devices.ts";

export const DEFAULT_PORT = 8417;

export function devicesPath(stateDir: string): string {
  return join(stateDir, "a2", "devices.json");
}

export function conversationsDir(stateDir: string): string {
  return join(stateDir, "a2", "conversations");
}

/** The little of commander's Command that is used here. */
export interface Command {
  command(nameAndArgs: string): Command;
  description(text: string): Command;
  action(fn: (...args: any[]) => void | Promise<void>): Command;
}

/** LAN addresses of this host, for the set-up hint. */
function lanAddresses(): string[] {
  const out: string[] = [];
  for (const list of Object.values(networkInterfaces())) {
    for (const a of list ?? []) if (a.family === "IPv4" && !a.internal) out.push(a.address);
  }
  return out;
}

export type CliOptions = {
  stateDir: () => string;
  /** channels.a2.host and port, for the hint. */
  listen: () => { host: string; port: number };
  print?: (line: string) => void;
};

export function registerA2Commands(program: Command, o: CliOptions): void {
  const print = o.print ?? ((l: string) => console.log(l));
  const store = () => new DeviceStore(devicesPath(o.stateDir()));
  const a2 = program.command("a2").description("Apple //e devices for the a2 channel (Twoegram)");

  a2.command("pair [name]")
    .description("Make a key for a new Apple //e; it is shown once")
    .action((name?: string) => {
      const { device, written } = store().pair(name ?? "");
      const { host, port } = o.listen();
      const hosts = host === "0.0.0.0" || host === "::" ? lanAddresses() : [host];
      print(`Device ${device.id} (${device.name}).`);
      print("");
      print("Type this key into Twoegram's set-up (Open-Apple-K). It is not shown again:");
      print("");
      print(`    ${written}`);
      print("");
      if (hosts.length) print(`Gateway: ${hosts.map((h) => `http://${h}:${port}`).join("  or  ")}`);
    });

  a2.command("list")
    .description("List the paired devices")
    .action(() => {
      const list = store().list();
      if (!list.length) print("no devices");
      for (const d of list) print(`${d.id}  ${d.created}  ${d.name}`);
    });

  a2.command("revoke <id>")
    .description("Remove a device and its conversations; it can no longer sign in")
    .action((id: string) => {
      const had = store().revoke(id);
      if (had) new FileConvStore(conversationsDir(o.stateDir())).drop(id);
      print(had ? `revoked ${id}` : `no device ${id}`);
    });
}
