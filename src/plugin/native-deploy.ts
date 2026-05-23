import * as fs from "node:fs";
import * as path from "node:path";

/**
 * Install-time deployer for plugin-supplied native UE modules.
 *
 * A plugin declares `nativeModule:` in ue-mcp.plugin.yml with a `source:`
 * directory inside its npm tarball. On install we copy that directory to
 * `<projectDir>/Plugins/<uePluginName>/` and record every copied file so
 * uninstall can clean up without nuking user edits.
 *
 * The tracking file lives at `<projectDir>/.ue-mcp/native-modules.json`:
 *
 *   {
 *     "<npm-package-name>": {
 *       "uePluginName": "VoxelPCGBridge",
 *       "pluginVersion": "0.1.0",
 *       "installedAt": "2026-05-22T17:30:00.000Z",
 *       "files": ["Plugins/VoxelPCGBridge/...", ...]   // relative to projectDir
 *     }
 *   }
 */

export interface NativeModuleRecord {
  uePluginName: string;
  pluginVersion: string;
  installedAt: string;
  files: string[];
}

export interface NativeModulesState {
  [npmPackageName: string]: NativeModuleRecord;
}

const STATE_DIR = ".ue-mcp";
const STATE_FILE = "native-modules.json";
const UE_PLUGIN_NAME_RE = /^[A-Za-z][A-Za-z0-9_]*$/;

function stateFilePath(projectDir: string): string {
  return path.join(projectDir, STATE_DIR, STATE_FILE);
}

function isInside(root: string, target: string): boolean {
  const relative = path.relative(root, target);
  return relative === "" || (
    relative !== ".." &&
    !relative.startsWith(`..${path.sep}`) &&
    !path.isAbsolute(relative)
  );
}

function resolveInside(root: string, child: string, label: string): string {
  const rootAbs = path.resolve(root);
  const target = path.resolve(rootAbs, child);
  if (!isInside(rootAbs, target)) {
    throw new Error(`${label} must stay inside ${rootAbs}`);
  }
  return target;
}

function tryResolveInside(root: string, child: string): string | null {
  try {
    return resolveInside(root, child, "path");
  } catch {
    return null;
  }
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

function assertRealPathInside(root: string, target: string, label: string): string {
  const rootReal = fs.realpathSync(root);
  const targetReal = fs.realpathSync(target);
  if (!isInside(rootReal, targetReal)) {
    throw new Error(`${label} real path must stay inside ${rootReal}`);
  }
  return targetReal;
}

function assertDestinationNotSymlink(dest: string): void {
  if (fs.existsSync(dest) && fs.lstatSync(dest).isSymbolicLink()) {
    throw new Error(`nativeModule destination may not contain symbolic links or junctions: ${dest}`);
  }
}

export function readNativeModulesState(projectDir: string): NativeModulesState {
  const file = stateFilePath(projectDir);
  if (!fs.existsSync(file)) return {};
  try {
    const raw = JSON.parse(fs.readFileSync(file, "utf-8"));
    if (raw && typeof raw === "object") return raw as NativeModulesState;
    return {};
  } catch {
    return {};
  }
}

export function writeNativeModulesState(projectDir: string, state: NativeModulesState): void {
  const dir = path.join(projectDir, STATE_DIR);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(stateFilePath(projectDir), JSON.stringify(state, null, 2) + "\n");
}

export interface DeployNativeResult {
  destDir: string;
  filesCopied: number;
  fileList: string[];
}

/**
 * Recursive directory copy that tracks every written file path. Returns
 * paths relative to projectDir so the state file is portable across
 * machines and the entries match the user's checkout layout.
 */
export function deployNativeModule(
  pkgDir: string,
  sourceRel: string,
  uePluginName: string,
  projectDir: string,
): DeployNativeResult {
  if (!UE_PLUGIN_NAME_RE.test(uePluginName)) {
    throw new Error("nativeModule.uePluginName must be a simple Unreal plugin identifier");
  }

  const sourceAbs = resolveInside(pkgDir, sourceRel, "nativeModule.source");
  if (!fs.existsSync(sourceAbs)) {
    throw new Error(
      `nativeModule.source '${sourceRel}' not found in plugin package at ${sourceAbs}`,
    );
  }
  assertNoSymlinkSegments(pkgDir, sourceAbs, "nativeModule.source");
  assertRealPathInside(pkgDir, sourceAbs, "nativeModule.source");

  const pluginsRoot = path.resolve(projectDir, "Plugins");
  fs.mkdirSync(pluginsRoot, { recursive: true });
  assertNoSymlinkSegments(projectDir, pluginsRoot, "project Plugins directory");
  assertRealPathInside(projectDir, pluginsRoot, "project Plugins directory");

  const destAbs = resolveInside(pluginsRoot, uePluginName, "nativeModule destination");
  assertNoSymlinkSegments(pluginsRoot, destAbs, "nativeModule destination");
  fs.mkdirSync(destAbs, { recursive: true });
  assertNoSymlinkSegments(pluginsRoot, destAbs, "nativeModule destination");
  assertRealPathInside(pluginsRoot, destAbs, "nativeModule destination");

  const copied: string[] = [];
  preflightCopyRecursive(sourceAbs, destAbs);
  copyRecursive(sourceAbs, destAbs, projectDir, copied);
  return { destDir: destAbs, filesCopied: copied.length, fileList: copied };
}

function preflightCopyRecursive(src: string, dest: string): void {
  const stat = fs.lstatSync(src);
  if (stat.isSymbolicLink()) {
    throw new Error(`nativeModule.source may not contain symbolic links: ${src}`);
  }
  assertDestinationNotSymlink(dest);
  if (!stat.isDirectory()) return;
  for (const entry of fs.readdirSync(src)) {
    preflightCopyRecursive(path.join(src, entry), path.join(dest, entry));
  }
}

function copyRecursive(src: string, dest: string, projectDir: string, copied: string[]): void {
  const stat = fs.lstatSync(src);
  if (stat.isSymbolicLink()) {
    throw new Error(`nativeModule.source may not contain symbolic links: ${src}`);
  }
  assertDestinationNotSymlink(dest);
  if (stat.isDirectory()) {
    fs.mkdirSync(dest, { recursive: true });
    for (const entry of fs.readdirSync(src)) {
      copyRecursive(path.join(src, entry), path.join(dest, entry), projectDir, copied);
    }
    return;
  }
  fs.copyFileSync(src, dest);
  copied.push(path.relative(projectDir, dest).split(path.sep).join("/"));
}

/**
 * Delete the files recorded for `npmName` and prune the state entry.
 * Returns the count of files actually removed (missing files are tolerated
 * silently — they may have been moved/deleted by the user).
 *
 * Caller is responsible for refusing the operation when the editor still
 * has the plugin DLL loaded (Windows refuses to delete locked files).
 */
export function undeployNativeModule(projectDir: string, npmName: string): number {
  const state = readNativeModulesState(projectDir);
  const record = state[npmName];
  if (!record) return 0;

  const pluginsRoot = path.resolve(projectDir, "Plugins");
  const pluginRoot = UE_PLUGIN_NAME_RE.test(record.uePluginName)
    ? tryResolveInside(pluginsRoot, record.uePluginName)
    : null;
  let pluginRootReal: string | null = null;
  if (pluginRoot) {
    try {
      assertNoSymlinkSegments(projectDir, pluginsRoot, "project Plugins directory");
      if (fs.existsSync(pluginRoot)) {
        assertNoSymlinkSegments(pluginsRoot, pluginRoot, "nativeModule destination");
      }
      pluginRootReal = fs.existsSync(pluginRoot)
        ? assertRealPathInside(pluginsRoot, pluginRoot, "nativeModule destination")
        : null;
    } catch {
      pluginRootReal = null;
    }
  }

  const resolveRecordedFile = (rel: string): string | null => {
    if (!pluginRoot || !pluginRootReal) return null;
    const abs = tryResolveInside(projectDir, rel);
    if (!abs || !isInside(pluginRoot, abs)) return null;
    try {
      assertNoSymlinkSegments(pluginRoot, abs, "nativeModule recorded file");
      if (fs.existsSync(abs)) {
        if (fs.lstatSync(abs).isSymbolicLink()) return null;
        const real = fs.realpathSync(abs);
        if (!isInside(pluginRootReal, real)) return null;
      }
      return abs;
    } catch {
      return null;
    }
  };

  let removed = 0;
  for (const rel of record.files) {
    const abs = resolveRecordedFile(rel);
    if (!abs) continue;
    try {
      if (fs.existsSync(abs)) {
        fs.unlinkSync(abs);
        removed++;
      }
    } catch {
      // Locked or otherwise unwritable - leave for the user. They'll see
      // the dangling file count in the install-tracking output.
    }
  }

  // Best-effort: remove now-empty directories upward from the deepest path.
  const dirs = new Set<string>();
  for (const rel of record.files) {
    const abs = resolveRecordedFile(rel);
    if (!abs) continue;
    let dir = path.dirname(abs);
    while (pluginRoot && isInside(pluginRoot, dir) && dir !== path.dirname(pluginRoot)) {
      dirs.add(dir);
      dir = path.dirname(dir);
    }
  }
  // Sort by depth descending so children are pruned before parents.
  const sorted = [...dirs].sort((a, b) => b.length - a.length);
  for (const dir of sorted) {
    try {
      if (fs.existsSync(dir) && fs.readdirSync(dir).length === 0) {
        fs.rmdirSync(dir);
      }
    } catch {
      // ignore - directory wasn't empty or wasn't ours to remove
    }
  }

  delete state[npmName];
  writeNativeModulesState(projectDir, state);
  return removed;
}
