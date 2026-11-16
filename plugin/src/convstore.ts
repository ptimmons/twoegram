// Each conversation's numbered messages, kept on disk so the Apple's
// history survives a gateway restart. One small JSON file per device and
// agent, readable only by its owner. OpenClaw keeps its own transcript of
// the same session; this log is what the Apple scrolls through, numbered
// as the protocol needs.

import { chmodSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import type { Msg } from "./protocol.ts";

export type Saved = { msgs: Msg[]; next: number };

export interface ConvStore {
  load(device: string, agent: string): Saved | undefined;
  save(device: string, agent: string, conv: Saved): void;
  /** Forget every conversation of a device (when it is revoked). */
  drop(device: string): void;
}

const DEVICE = /^[0-9a-f]{8}$/;
const AGENT = /^[a-z0-9_-]{1,16}$/;

export class FileConvStore implements ConvStore {
  readonly dir: string;

  constructor(dir: string) {
    this.dir = dir;
  }

  private file(device: string, agent: string): string {
    // Both are checked by the protocol before they get here; checked again
    // because they become a file name.
    if (!DEVICE.test(device) || !AGENT.test(agent)) throw new Error(`bad conversation name ${device}.${agent}`);
    return join(this.dir, `${device}.${agent}.json`);
  }

  load(device: string, agent: string): Saved | undefined {
    let text: string;
    try {
      text = readFileSync(this.file(device, agent), "utf8");
    } catch {
      return undefined;
    }
    const data = JSON.parse(text) as { version: 1; next: number; msgs: Msg[] };
    return { msgs: data.msgs, next: data.next };
  }

  save(device: string, agent: string, conv: Saved): void {
    const path = this.file(device, agent);
    mkdirSync(this.dir, { recursive: true, mode: 0o700 });
    const tmp = `${path}.tmp`;
    writeFileSync(tmp, JSON.stringify({ version: 1, next: conv.next, msgs: conv.msgs }) + "\n", { mode: 0o600 });
    chmodSync(tmp, 0o600);
    renameSync(tmp, path);
  }

  drop(device: string): void {
    if (!DEVICE.test(device)) return;
    let names: string[];
    try {
      names = readdirSync(this.dir);
    } catch {
      return;
    }
    for (const n of names) if (n.startsWith(`${device}.`)) rmSync(join(this.dir, n), { force: true });
  }
}
