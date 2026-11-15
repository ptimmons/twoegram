// The devices the plugin knows: one 16-byte key each, kept in a JSON
// file that only its owner may read.

import { chmodSync, existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";
import { deviceId, formatKey, newKey } from "./protocol.ts";

export type Device = { id: string; key: Buffer; name: string; created: string };

type Stored = { version: 1; devices: { id: string; key: string; name: string; created: string }[] };

export class DeviceStore {
  readonly path: string;
  private devices = new Map<string, Device>();

  constructor(path: string) {
    this.path = path;
    this.load();
  }

  private load(): void {
    this.devices.clear();
    if (!existsSync(this.path)) return;
    const data = JSON.parse(readFileSync(this.path, "utf8")) as Stored;
    for (const d of data.devices) {
      this.devices.set(d.id, { id: d.id, key: Buffer.from(d.key, "hex"), name: d.name, created: d.created });
    }
  }

  private save(): void {
    mkdirSync(dirname(this.path), { recursive: true, mode: 0o700 });
    const data: Stored = {
      version: 1,
      devices: [...this.devices.values()].map((d) => ({
        id: d.id,
        key: d.key.toString("hex"),
        name: d.name,
        created: d.created,
      })),
    };
    const tmp = `${this.path}.tmp`;
    writeFileSync(tmp, JSON.stringify(data, null, 2) + "\n", { mode: 0o600 });
    chmodSync(tmp, 0o600);
    renameSync(tmp, this.path);
  }

  /** Make a device; returns it and its key in written form, shown once. */
  pair(name: string): { device: Device; written: string } {
    let key = newKey();
    while (this.devices.has(deviceId(key))) key = newKey();
    const device = { id: deviceId(key), key, name: name || "Apple //e", created: new Date().toISOString() };
    this.devices.set(device.id, device);
    this.save();
    return { device, written: formatKey(key) };
  }

  revoke(id: string): boolean {
    const had = this.devices.delete(id);
    if (had) this.save();
    return had;
  }

  get(id: string): Device | undefined {
    return this.devices.get(id);
  }

  list(): Device[] {
    return [...this.devices.values()];
  }

  /** Read the file again, after another process (the CLI) changed it. */
  reload(): void {
    this.load();
  }
}
