import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import type { EpicCatalog } from "./epic-enrich.js";

/**
 * Disk cache for Epic's toolset catalog.
 *
 * The catalog only exists live when the editor is running, but the ue-mcp tool
 * surface should be stable and inspectable across sessions - including when the
 * editor is offline and for deterministic unit tests. So whenever we fetch the
 * live catalog we persist it here, and when the editor is not reachable at
 * startup we fall back to the cached copy to surface the same first-class Epic
 * actions. The cache lives under the project's Saved/ (gitignored in UE
 * projects), keyed implicitly by project since the enabled toolsets vary.
 */
export interface CatalogCacheEnvelope {
  savedAt: string;
  engineAssociation?: string | null;
  toolsetCount: number;
  catalog: EpicCatalog;
}

function cacheFile(projectDir?: string): string | null {
  if (!projectDir) return null;
  return path.join(projectDir, "Saved", "UE_MCP_Bridge", "epic-catalog.json");
}

function isInside(root: string, target: string): boolean {
  const relative = path.relative(root, target);
  return relative === "" || (
    relative !== ".." &&
    !relative.startsWith(`..${path.sep}`) &&
    !path.isAbsolute(relative)
  );
}

function assertNoSymlinkSegments(root: string, target: string, label: string): void {
  const rootAbs = path.resolve(root);
  const targetAbs = path.resolve(target);
  if (!isInside(rootAbs, targetAbs)) {
    throw new Error(`${label} must stay inside ${rootAbs}`);
  }

  const relative = path.relative(rootAbs, targetAbs);
  if (!relative) return;

  let current = rootAbs;
  for (const part of relative.split(path.sep)) {
    current = path.join(current, part);
    if (!fs.existsSync(current)) return;
    if (fs.lstatSync(current).isSymbolicLink()) {
      throw new Error(`${label} may not contain symbolic links or junctions: ${current}`);
    }
  }
}

function assertExistingRealPathInside(root: string, target: string, label: string): void {
  const rootReal = fs.realpathSync(root);
  const targetReal = fs.realpathSync(target);
  if (!isInside(rootReal, targetReal)) {
    throw new Error(`${label} real path must stay inside ${rootReal}`);
  }
}

function tryLstat(file: string): fs.Stats | null {
  try {
    return fs.lstatSync(file);
  } catch {
    return null;
  }
}

function writableCacheFile(projectDir?: string): string | null {
  const file = cacheFile(projectDir);
  if (!file || !projectDir) return null;

  const projectAbs = path.resolve(projectDir);
  const dir = path.dirname(path.resolve(file));
  assertNoSymlinkSegments(projectAbs, dir, "Epic catalog cache directory");
  fs.mkdirSync(dir, { recursive: true });
  assertNoSymlinkSegments(projectAbs, dir, "Epic catalog cache directory");
  assertExistingRealPathInside(projectAbs, dir, "Epic catalog cache directory");

  const fileStat = tryLstat(file);
  if (fileStat) {
    if (fileStat.isSymbolicLink()) {
      throw new Error(`Epic catalog cache file may not be a symbolic link or junction: ${file}`);
    }
    assertExistingRealPathInside(projectAbs, file, "Epic catalog cache file");
  }

  return file;
}

function readableCacheFile(projectDir?: string): string | null {
  const file = cacheFile(projectDir);
  if (!file || !projectDir || !fs.existsSync(file)) return null;

  const projectAbs = path.resolve(projectDir);
  assertNoSymlinkSegments(projectAbs, file, "Epic catalog cache file");
  if (fs.lstatSync(file).isSymbolicLink()) return null;
  assertExistingRealPathInside(projectAbs, file, "Epic catalog cache file");
  return file;
}

/** Persist the live catalog. Best-effort: never throws. */
export function saveCatalogCache(
  projectDir: string | undefined,
  catalog: EpicCatalog,
  engineAssociation?: string | null,
): void {
  try {
    const file = writableCacheFile(projectDir);
    if (!file) return;
    const envelope: CatalogCacheEnvelope = {
      savedAt: new Date().toISOString(),
      engineAssociation: engineAssociation ?? null,
      toolsetCount: catalog?.toolsets?.length ?? 0,
      catalog,
    };
    fs.writeFileSync(file, JSON.stringify(envelope, null, 2));
  } catch {
    // Cache is an optimization; a write failure must never break startup.
  }
}

/** Load the cached catalog, or null if absent/unreadable. */
export function loadCatalogCache(projectDir?: string): EpicCatalog | null {
  try {
    const file = readableCacheFile(projectDir);
    if (!file) return null;
    const envelope = JSON.parse(fs.readFileSync(file, "utf-8")) as CatalogCacheEnvelope;
    return envelope?.catalog ?? null;
  } catch {
    return null;
  }
}

/**
 * Load the catalog snapshot baked into the shipped package
 * (assets/epic-catalog.snapshot.json). This is the deterministic default source
 * so the Epic tool surface appears on the very first startup, with no editor and
 * no prior cache, and matches the generated docs (same snapshot feeds both).
 * Returns null if the asset is missing (e.g. a dev build before the snapshot is
 * generated).
 */
export function loadBakedCatalog(): EpicCatalog | null {
  // dist/epic-cache.js -> package root is one level up; assets/ ships there.
  const here = path.dirname(fileURLToPath(import.meta.url));
  const candidates = [
    path.join(here, "..", "assets", "epic-catalog.snapshot.json"),
    path.join(here, "..", "..", "assets", "epic-catalog.snapshot.json"),
  ];
  for (const file of candidates) {
    if (!fs.existsSync(file)) continue;
    try {
      return JSON.parse(fs.readFileSync(file, "utf-8")) as EpicCatalog;
    } catch {
      return null;
    }
  }
  return null;
}
