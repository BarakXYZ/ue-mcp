import { describe, it, expect } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { saveCatalogCache, loadCatalogCache, loadBakedCatalog } from "../../src/epic-cache.js";
import type { EpicCatalog } from "../../src/epic-enrich.js";

const CATALOG: EpicCatalog = {
  toolsets: [{ name: "GASToolsets.X", tools: [{ name: "GASToolsets.X.Do" }] }],
};

describe("epic-cache", () => {
  it("round-trips a catalog through the project cache file", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-epic-"));
    saveCatalogCache(dir, CATALOG, "5.8");
    expect(fs.existsSync(path.join(dir, "Saved", "UE_MCP_Bridge", "epic-catalog.json"))).toBe(true);
    expect(loadCatalogCache(dir)).toEqual(CATALOG);
  });

  it("does not write through a Saved symlink or junction", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-epic-link-"));
    const outside = fs.mkdtempSync(path.join(os.tmpdir(), "ue-epic-outside-"));
    try {
      fs.symlinkSync(outside, path.join(dir, "Saved"), process.platform === "win32" ? "junction" : "dir");
    } catch {
      return;
    }

    saveCatalogCache(dir, CATALOG, "5.8");

    expect(fs.existsSync(path.join(outside, "UE_MCP_Bridge", "epic-catalog.json"))).toBe(false);
    expect(loadCatalogCache(dir)).toBeNull();
  });

  it("does not write through an existing cache-file symlink", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-epic-file-link-"));
    const outside = fs.mkdtempSync(path.join(os.tmpdir(), "ue-epic-file-outside-"));
    const cacheDir = path.join(dir, "Saved", "UE_MCP_Bridge");
    fs.mkdirSync(cacheDir, { recursive: true });
    try {
      fs.symlinkSync(
        path.join(outside, "epic-catalog.json"),
        path.join(cacheDir, "epic-catalog.json"),
        "file",
      );
    } catch {
      return;
    }

    saveCatalogCache(dir, CATALOG, "5.8");

    expect(fs.existsSync(path.join(outside, "epic-catalog.json"))).toBe(false);
    expect(loadCatalogCache(dir)).toBeNull();
  });

  it("returns null when no cache exists", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-epic-none-"));
    expect(loadCatalogCache(dir)).toBeNull();
  });

  it("returns null for an undefined project dir (never throws)", () => {
    expect(loadCatalogCache(undefined)).toBeNull();
    expect(() => saveCatalogCache(undefined, CATALOG)).not.toThrow();
  });

  it("loads the baked snapshot shipped in assets/", () => {
    const baked = loadBakedCatalog();
    expect(baked).not.toBeNull();
    expect(Array.isArray(baked?.toolsets)).toBe(true);
    expect((baked?.toolsets?.length ?? 0)).toBeGreaterThan(0);
  });
});
