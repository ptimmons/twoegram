import { mkdtempSync, rmSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { type Command, devicesPath, registerA2Commands } from "../src/commands.ts";
import { DeviceStore } from "../src/devices.ts";
import { deriveKey, deviceId, formatKey } from "../src/protocol.ts";

// Just enough of commander to call an action by name.
class FakeCommand implements Command {
  subs = new Map<string, FakeCommand>();
  fn: ((...args: any[]) => void | Promise<void>) | null = null;
  command(nameAndArgs: string): Command {
    const c = new FakeCommand();
    this.subs.set(nameAndArgs.split(" ")[0], c);
    return c;
  }
  description(): Command {
    return this;
  }
  action(fn: (...args: any[]) => void | Promise<void>): Command {
    this.fn = fn;
    return this;
  }
  async run(path: string[], ...args: any[]): Promise<void> {
    let c: FakeCommand = this;
    for (const p of path) c = c.subs.get(p)!;
    await c.fn!(...args);
  }
}

describe("openclaw a2 add", () => {
  let dir: string;
  let lines: string[];
  let typed: string;
  let asked: string[];
  let program: FakeCommand;
  const master = Buffer.from("8f2c1d0e9b7a6f5e4d3c2b1a09182736", "hex");

  beforeEach(() => {
    dir = mkdtempSync(join(tmpdir(), "a2-cmd-"));
    lines = [];
    asked = [];
    program = new FakeCommand();
    registerA2Commands(program, {
      stateDir: () => dir,
      listen: () => ({ host: "127.0.0.1", port: 8417 }),
      print: (l) => lines.push(l),
      readKey: async (prompt) => {
        asked.push(prompt);
        return typed;
      },
    });
    process.exitCode = undefined;
  });

  afterEach(() => {
    rmSync(dir, { recursive: true, force: true });
    process.exitCode = undefined;
  });

  it("adds the key the Apple shows, and says its device id", async () => {
    const key = deriveKey(master, "claw1");
    typed = formatKey(key).toLowerCase();
    await program.run(["a2", "add"], "Apple IIe");
    expect(asked).toHaveLength(1);
    expect(lines.join("\n")).toContain(deviceId(key));
    expect(lines.join("\n")).not.toContain(formatKey(key));
    const d = new DeviceStore(devicesPath(dir)).get(deviceId(key))!;
    expect(d.key.equals(key)).toBe(true);
    expect(d.name).toBe("Apple IIe");
    expect(statSync(devicesPath(dir)).mode & 0o777).toBe(0o600);
    expect(process.exitCode).toBeUndefined();
  });

  it("refuses a mistyped key and adds nothing", async () => {
    const w = formatKey(deriveKey(master, "claw1"));
    typed = w.slice(0, 3) + (w[3] === "Z" ? "Y" : "Z") + w.slice(4);
    await program.run(["a2", "add"], "Apple IIe");
    expect(lines.join("\n")).toMatch(/not a key/);
    expect(new DeviceStore(devicesPath(dir)).list()).toHaveLength(0);
    expect(process.exitCode).toBe(1);
  });

  it("refuses a key paired already", async () => {
    typed = formatKey(deriveKey(master, "mini"));
    await program.run(["a2", "add"], "first");
    lines = [];
    await program.run(["a2", "add"], "second");
    expect(lines.join("\n")).toMatch(/paired already/);
    const list = new DeviceStore(devicesPath(dir)).list();
    expect(list.map((d) => d.name)).toEqual(["first"]);
  });

  it("lists and revokes what it added like any pairing", async () => {
    const key = deriveKey(master, "relay");
    typed = formatKey(key);
    await program.run(["a2", "add"], "");
    lines = [];
    await program.run(["a2", "list"]);
    expect(lines.join("\n")).toContain(`${deviceId(key)}`);
    lines = [];
    await program.run(["a2", "revoke"], deviceId(key));
    expect(lines).toEqual([`revoked ${deviceId(key)}`]);
  });
});
