import * as fs from "node:fs";
import * as path from "node:path";
import { execSync } from "node:child_process";
import type { ProjectContext } from "./project.js";

export interface DeployResult {
  pythonPluginEnabled: boolean;
  cppPluginDeployed: boolean;
  cppPluginEnabled: boolean;
  error?: string;
}

/**
 * Deploy the C++ bridge plugin to the target UE project.
 *
 * Copies plugin source from plugin/ue_mcp_bridge/ into the target
 * project's Plugins/UE_MCP_Bridge/ directory (skipping build artifacts).
 * Also enables PythonScriptPlugin in the .uproject because the C++
 * bridge's `execute_python` handler calls into it at runtime.
 */
export function deploy(context: ProjectContext): DeployResult {
  const result: DeployResult = {
    pythonPluginEnabled: false,
    cppPluginDeployed: false,
    cppPluginEnabled: false,
  };

  try {
    result.pythonPluginEnabled = ensurePythonPlugin(context.projectPath!);
    result.cppPluginDeployed = deployCppPlugin(context.projectPath!);
    result.cppPluginEnabled = ensureCppPluginEnabled(context.projectPath!);
  } catch (e) {
    result.error = e instanceof Error ? e.message : String(e);
  }

  return result;
}

export function deploySummary(r: DeployResult): string {
  if (r.error) return `Bridge deployment failed: ${r.error}`;
  const changes: string[] = [];
  if (r.pythonPluginEnabled) changes.push("enabled PythonScriptPlugin");
  if (r.cppPluginDeployed) changes.push("deployed C++ bridge plugin");
  if (r.cppPluginEnabled) changes.push("enabled UE_MCP_Bridge in .uproject");
  if (changes.length === 0) return "Bridge already configured";
  return "Bridge setup: " + changes.join(", ");
}

/* ------------------------------------------------------------------ */
/*  PythonScriptPlugin — still needed for execute_python escape hatch */
/* ------------------------------------------------------------------ */

function ensurePythonPlugin(uprojectPath: string): boolean {
  const raw = fs.readFileSync(uprojectPath, "utf-8");
  const root = JSON.parse(raw);

  if (!root.Plugins) root.Plugins = [];

  const already = root.Plugins.some(
    (p: { Name?: string }) =>
      p.Name?.toLowerCase() === "pythonscriptplugin",
  );
  if (already) return false;

  root.Plugins.unshift({ Name: "PythonScriptPlugin", Enabled: true });
  fs.writeFileSync(uprojectPath, JSON.stringify(root, null, "\t"));
  return true;
}

/* ------------------------------------------------------------------ */
/*  C++ Plugin deployment                                             */
/* ------------------------------------------------------------------ */

function deployCppPlugin(uprojectPath: string): boolean {
  const projectDir = path.dirname(uprojectPath);
  const pluginsDir = path.join(projectDir, "Plugins");

  const sourcePluginDir = path.resolve(
    import.meta.dirname ?? path.dirname(new URL(import.meta.url).pathname),
    "..",
    "plugin",
    "ue_mcp_bridge",
  );

  if (!fs.existsSync(sourcePluginDir)) {
    console.error(`[ue-mcp] C++ plugin source not found at ${sourcePluginDir}`);
    return false;
  }

  const targetPluginDir = path.join(pluginsDir, "UE_MCP_Bridge");
  let anyDeployed = false;

  function copyRecursive(src: string, dest: string): void {
    if (!fs.existsSync(dest)) {
      fs.mkdirSync(dest, { recursive: true });
    }
    for (const entry of fs.readdirSync(src, { withFileTypes: true })) {
      const srcPath = path.join(src, entry.name);
      const destPath = path.join(dest, entry.name);

      // Skip build artifacts
      if (
        entry.name === "Binaries" ||
        entry.name === "Intermediate" ||
        entry.name === "Saved"
      ) {
        continue;
      }

      if (entry.isDirectory()) {
        copyRecursive(srcPath, destPath);
      } else {
        const srcBytes = fs.readFileSync(srcPath);
        let shouldWrite = true;
        if (fs.existsSync(destPath)) {
          const destBytes = fs.readFileSync(destPath);
          shouldWrite = !srcBytes.equals(destBytes);
        }
        if (shouldWrite) {
          fs.writeFileSync(destPath, srcBytes);
          anyDeployed = true;
        }
      }
    }
  }

  copyRecursive(sourcePluginDir, targetPluginDir);
  return anyDeployed;
}

function ensureCppPluginEnabled(uprojectPath: string): boolean {
  const raw = fs.readFileSync(uprojectPath, "utf-8");
  const root = JSON.parse(raw);

  if (!root.Plugins) root.Plugins = [];

  const already = root.Plugins.some(
    (p: { Name?: string }) => p.Name === "UE_MCP_Bridge",
  );
  if (already) return false;

  root.Plugins.push({
    Name: "UE_MCP_Bridge",
    Enabled: true,
  });
  fs.writeFileSync(uprojectPath, JSON.stringify(root, null, "\t"));
  return true;
}

/* ------------------------------------------------------------------ */
/*  Engine discovery (used by editor-control)                         */
/* ------------------------------------------------------------------ */

export function findEngineInstall(
  engineAssociation: string | null,
): string | null {
  if (!engineAssociation) return null;

  const guidRegex =
    /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  if (guidRegex.test(engineAssociation)) {
    return findEngineByGuid(engineAssociation);
  }

  return findLauncherEngine(engineAssociation);
}

function findEngineByGuid(guid: string): string | null {
  try {
    const output = execSync(
      `reg query "HKCU\\SOFTWARE\\Epic Games\\Unreal Engine\\Builds" /v "${guid}"`,
      { stdio: "pipe", encoding: "utf-8" },
    );
    const match = output.match(/REG_SZ\s+(.+)/);
    if (match) {
      const p = match[1].trim();
      if (fs.existsSync(p)) return p;
    }
  } catch {
    // registry key not found
  }
  return null;
}

function findLauncherEngine(association: string): string | null {
  const launcherDat = path.join(
    process.env.PROGRAMDATA || "C:\\ProgramData",
    "Epic",
    "UnrealEngineLauncher",
    "LauncherInstalled.dat",
  );

  if (fs.existsSync(launcherDat)) {
    try {
      const data = JSON.parse(fs.readFileSync(launcherDat, "utf-8"));
      for (const entry of data.InstallationList ?? []) {
        if (
          entry.AppName?.toLowerCase() ===
          `ue_${association}`.toLowerCase()
        ) {
          if (fs.existsSync(entry.InstallLocation)) {
            return entry.InstallLocation;
          }
        }
      }
    } catch {
      // malformed manifest
    }
  }

  for (const root of [
    "C:\\Program Files\\Epic Games",
    "D:\\Program Files\\Epic Games",
    "C:\\Epic Games",
    "D:\\Epic Games",
  ]) {
    const candidate = path.join(root, `UE_${association}`);
    if (fs.existsSync(candidate)) return candidate;
  }

  return null;
}
