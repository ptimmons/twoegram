import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import * as contract from "../doctor-contract-api.ts";

const manifest = JSON.parse(readFileSync(new URL("../openclaw.plugin.json", import.meta.url), "utf8"));
const pkg = JSON.parse(readFileSync(new URL("../package.json", import.meta.url), "utf8"));

// OpenClaw refuses a Doctor contract whose exported migrations differ from
// the manifest's list, and treats a2 as stateless only when both are empty.
describe("Doctor contract", () => {
  it("declares no state migrations, the same in the manifest and the module", () => {
    expect(manifest.doctorContract).toEqual({ stateMigrations: [] });
    expect(contract.stateMigrations).toEqual([]);
  });

  it("exports nothing that would make Doctor treat it as a config repair", () => {
    expect(Object.keys(contract)).toEqual(["stateMigrations"]);
  });

  it("backs up the a2 folder under the state directory", () => {
    expect(manifest.backupResources).toEqual([{ disposition: "include", scope: "state", relativePath: "a2" }]);
  });

  it("ships the module in the package", () => {
    expect(pkg.files).toContain("doctor-contract-api.ts");
  });
});
