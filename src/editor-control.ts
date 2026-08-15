import * as fs from "node:fs";
import * as path from "node:path";
import { spawn, execFileSync } from "child_process";
import * as net from "net";
import { DEFAULT_BRIDGE_HOST, DEFAULT_BRIDGE_PORT, type ProjectContext } from "./project.js";
import { findEngineInstall } from "./deployer.js";

// editor-control relies on Windows-only tools (tasklist/taskkill, Build.bat).
// The MCP server itself is cross-platform; only process control is gated.
const IS_WINDOWS = process.platform === "win32";

const WINDOWS_ONLY_MSG =
  "editor start/stop/restart is Windows-only. On macOS/Linux, start and stop the Unreal Editor manually; ue-mcp will reconnect when the bridge is reachable.";

function findUEBuildTool(): string | null {
  const envPath = process.env.UE_BUILD_TOOL_PATH;
  if (envPath) return envPath;

  const versions = ["5.8", "5.7", "5.6", "5.5", "5.4", "5.3"];
  const scriptName = IS_WINDOWS ? "Build.bat" : "Build.sh";

  const searchRoots: string[] = IS_WINDOWS
    ? [
        "C:/Program Files/Epic Games",
        "D:/Program Files/Epic Games",
        "E:/Program Files/Epic Games",
        "C:/Epic Games",
        "D:/Epic Games",
        "E:/Epic Games",
      ]
    : process.platform === "darwin"
      ? ["/Users/Shared/Epic Games"]
      : [
          path.join(process.env.HOME ?? "/home", "UnrealEngine"),
          "/opt/UnrealEngine",
        ];

  for (const basePath of searchRoots) {
    for (const version of versions) {
      const buildToolPath = path.join(basePath, `UE_${version}`, "Engine", "Build", "BatchFiles", scriptName);
      if (fs.existsSync(buildToolPath)) {
        return buildToolPath;
      }
    }
  }

  // Linux source builds: ~/UnrealEngine/Engine/Build/BatchFiles/Build.sh (no version subdir)
  if (!IS_WINDOWS && process.platform !== "darwin") {
    const home = process.env.HOME ?? "/home";
    const sourceBuild = path.join(home, "UnrealEngine", "Engine", "Build", "BatchFiles", "Build.sh");
    if (fs.existsSync(sourceBuild)) return sourceBuild;
  }

  return null;
}

function findEditorExecutable(project?: ProjectContext): string | null {
  const envPath = process.env.UE_EDITOR_PATH;
  if (envPath) return envPath;

  const associatedEngineRoot = findEngineInstall(project?.engineAssociation ?? null);
  if (associatedEngineRoot) {
    const associatedEditorExe = path.join(associatedEngineRoot, "Engine", "Binaries", "Win64", "UnrealEditor.exe");
    if (fs.existsSync(associatedEditorExe)) {
      return associatedEditorExe;
    }
  }

  const buildTool = findUEBuildTool();
  if (!buildTool) return null;

  const engineRoot = path.resolve(buildTool, "..", "..", "..", "..");
  const editorExe = path.join(engineRoot, "Engine", "Binaries", "Win64", "UnrealEditor.exe");

  if (fs.existsSync(editorExe)) {
    return editorExe;
  }

  return null;
}

function normalizeWindowsCommandPath(value: string): string {
  return value.replace(/\//g, "\\").toLowerCase();
}

function commandLineContainsPathArgument(commandLine: string, projectPath: string): boolean {
  const normalizedCommandLine = normalizeWindowsCommandPath(commandLine);
  const normalizedProjectPath = normalizeWindowsCommandPath(path.resolve(projectPath));
  let searchFrom = 0;

  while (searchFrom < normalizedCommandLine.length) {
    const matchIndex = normalizedCommandLine.indexOf(normalizedProjectPath, searchFrom);
    if (matchIndex < 0) return false;

    const before = matchIndex === 0 ? "" : normalizedCommandLine[matchIndex - 1];
    const afterIndex = matchIndex + normalizedProjectPath.length;
    const after = afterIndex === normalizedCommandLine.length ? "" : normalizedCommandLine[afterIndex];
    const validBefore = before === "" || before === '"' || before === "'" || before === "=" || /\s/.test(before);
    const validAfter = after === "" || after === '"' || after === "'" || /\s/.test(after);
    if (validBefore && validAfter) return true;

    searchFrom = matchIndex + normalizedProjectPath.length;
  }

  return false;
}

/**
 * Match only an Unreal editor executable that is known to belong to the exact
 * project command line. Source-built project targets use <Target>.exe rather
 * than UnrealEditor.exe, so both forms are first-class process identities.
 */
export function isProjectEditorProcessCandidate(
  processName: string,
  commandLine: string,
  projectPath: string,
  editorTargets: readonly string[] = listEditorTargets(projectPath),
): boolean {
  const projectName = path.basename(projectPath, ".uproject");
  const allowedNames = new Set(
    ["UnrealEditor.exe", `${projectName}Editor.exe`, ...editorTargets.map((target) => `${target}.exe`)]
      .map((name) => name.toLowerCase()),
  );

  return allowedNames.has(processName.toLowerCase()) && commandLineContainsPathArgument(commandLine, projectPath);
}

function findEditorProcessIdsForProject(project: ProjectContext): number[] {
  if (!IS_WINDOWS || !project.projectPath) return [];

  const command = [
    "$items = @(Get-CimInstance Win32_Process | ",
    "Where-Object { $_.CommandLine -and ($_.Name -eq 'UnrealEditor.exe' -or $_.Name -like '*Editor.exe') } | ",
    "Select-Object ProcessId, Name, CommandLine); ",
    "ConvertTo-Json -InputObject $items -Compress",
  ].join("");

  try {
    const output = execFileSync("powershell", ["-NoProfile", "-Command", command], {
      stdio: "pipe",
      encoding: "utf-8",
    });
    const processes = JSON.parse(output || "[]") as Array<{
      ProcessId?: unknown;
      Name?: unknown;
      CommandLine?: unknown;
    }>;
    const editorTargets = listEditorTargets(project.projectPath);
    return processes
      .filter(
        (process) =>
          typeof process.Name === "string" &&
          typeof process.CommandLine === "string" &&
          isProjectEditorProcessCandidate(
            process.Name,
            process.CommandLine,
            project.projectPath!,
            editorTargets,
          ),
      )
      .map((process) => Number(process.ProcessId))
      .filter((pid) => Number.isInteger(pid) && pid > 0);
  } catch {
    return [];
  }
}

async function isBridgeAvailable(host = DEFAULT_BRIDGE_HOST, port = DEFAULT_BRIDGE_PORT, timeoutMs = 1000): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = new net.Socket();
    let resolved = false;

    const timer = setTimeout(() => {
      if (!resolved) {
        resolved = true;
        socket.destroy();
        resolve(false);
      }
    }, timeoutMs);

    socket.once("connect", () => {
      if (!resolved) {
        resolved = true;
        clearTimeout(timer);
        socket.destroy();
        resolve(true);
      }
    });

    socket.once("error", () => {
      if (!resolved) {
        resolved = true;
        clearTimeout(timer);
        resolve(false);
      }
    });

    socket.connect(port, host);
  });
}

async function waitForBridge(host: string, port: number, maxWaitSeconds = 120, checkIntervalMs = 2000): Promise<boolean> {
  const startTime = Date.now();
  const maxWaitMs = maxWaitSeconds * 1000;

  while (Date.now() - startTime < maxWaitMs) {
    if (await isBridgeAvailable(host, port)) {
      return true;
    }
    await new Promise((resolve) => setTimeout(resolve, checkIntervalMs));
  }

  return false;
}

export async function startEditor(project: ProjectContext): Promise<{ success: boolean; message: string }> {
  if (!IS_WINDOWS) return { success: false, message: WINDOWS_ONLY_MSG };

  if (!project.projectPath) {
    return { success: false, message: "No project loaded. Use project(action='set_project') first." };
  }

  if (await isBridgeAvailable(project.bridgeHost, project.bridgePort)) {
    return { success: false, message: `Editor bridge is already available on ${project.bridgeHost}:${project.bridgePort}` };
  }

  if (findEditorProcessIdsForProject(project).length > 0) {
    return {
      success: false,
      message: `Editor is already running for this project, but the bridge is not available on ${project.bridgeHost}:${project.bridgePort}`,
    };
  }

  const editorExe = findEditorExecutable(project);
  if (!editorExe) {
    return {
      success: false,
      message: "Unreal Editor executable not found. Set UE_EDITOR_PATH environment variable or install UE5.3+ to default location.",
    };
  }

  try {
    const editorProcess = spawn(editorExe, [project.projectPath, `-MCPBridgePort=${project.bridgePort}`], {
      stdio: "ignore",
      detached: true,
    });

    editorProcess.unref();

    // Wait for bridge to become available (editor fully started)
    const bridgeAvailable = await waitForBridge(project.bridgeHost, project.bridgePort, 120, 2000);
    if (!bridgeAvailable) {
      return {
        success: false,
        message: `Editor launched but bridge did not become available on ${project.bridgeHost}:${project.bridgePort} within 120 seconds. Editor may still be starting up.`,
      };
    }

    return { success: true, message: `Editor launched and bridge available on ${project.bridgeHost}:${project.bridgePort}: ${editorExe}` };
  } catch (error) {
    return {
      success: false,
      message: `Failed to launch editor: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
}

export async function stopEditor(project: ProjectContext, force = false): Promise<{ success: boolean; message: string }> {
  if (!IS_WINDOWS) return { success: false, message: WINDOWS_ONLY_MSG };
  if (!project.projectPath) {
    return { success: false, message: "No project loaded. Use project(action='set_project') first." };
  }

  const bridgeHost = project.bridgeHost ?? DEFAULT_BRIDGE_HOST;
  const bridgePort = project.bridgePort ?? DEFAULT_BRIDGE_PORT;
  const projectProcessIds = findEditorProcessIdsForProject(project);
  const processRunning = projectProcessIds.length > 0;
  const bridgeUp = await isBridgeAvailable(bridgeHost, bridgePort);

  if (!processRunning && !bridgeUp) {
    return { success: false, message: "Editor is not running" };
  }

  if (projectProcessIds.length === 0) {
    return {
      success: false,
      message: `Editor bridge is available on ${bridgeHost}:${bridgePort}, but the project editor process could not be resolved. Close it manually or restart it through ue-mcp.`,
    };
  }

  try {
    const killProjectProcesses = (forceKill: boolean) => {
      if (projectProcessIds.length === 0) return false;
      for (const pid of projectProcessIds) {
        const args = forceKill ? ["/F", "/PID", String(pid)] : ["/PID", String(pid)];
        execFileSync("taskkill", args, { stdio: "pipe" });
      }
      return true;
    };

    if (force) {
      if (killProjectProcesses(true)) {
        return { success: true, message: `Editor force-killed for project ${project.projectName ?? ""}`.trim() };
      }
      return { success: false, message: "Editor process could not be resolved for project-scoped force kill." };
    }

    // Graceful close - sends WM_CLOSE, allows save dialogs
    if (!killProjectProcesses(false)) {
      return { success: false, message: "Editor process could not be resolved for project-scoped graceful close." };
    }

    // Wait up to 10 seconds for editor to close gracefully
    for (let i = 0; i < 10; i++) {
      await new Promise((resolve) => setTimeout(resolve, 1000));
      const stillRunning = findEditorProcessIdsForProject(project).length > 0;
      if (!stillRunning) {
        return { success: true, message: "Editor closed successfully" };
      }
    }

    // Graceful close failed — force kill
    if (!killProjectProcesses(true)) {
      return { success: false, message: "Editor process could not be resolved for project-scoped force kill." };
    }
    await new Promise((resolve) => setTimeout(resolve, 2000));

    const stillRunning = findEditorProcessIdsForProject(project).length > 0;
    if (!stillRunning) {
      return { success: true, message: "Editor force-killed after graceful close timed out" };
    }

    return {
      success: false,
      message: "Editor still running after force kill attempt. Close manually.",
    };
  } catch (error) {
    return {
      success: false,
      message: `Failed to stop editor: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
}

export async function restartEditor(project: ProjectContext, bridge?: { connect: (timeoutMs?: number) => Promise<void> }): Promise<{ success: boolean; message: string }> {
  const stopResult = await stopEditor(project);
  if (!stopResult.success && stopResult.message !== "Editor is not running") {
    return { success: false, message: `Failed to stop editor: ${stopResult.message}` };
  }

  // Wait for process to fully terminate and release locks
  await new Promise((resolve) => setTimeout(resolve, 3000));

  const startResult = await startEditor(project);
  if (!startResult.success) {
    return startResult;
  }

  // Reconnect the bridge if provided
  if (bridge) {
    try {
      await bridge.connect(5000);
    } catch {
      // Bridge reconnect timer will handle it
    }
  }

  return startResult;
}

export interface BuildResult {
  success: boolean;
  message: string;
  exitCode: number | null;
}

function listEditorTargets(projectPath: string): string[] {
  const sourceDir = path.join(path.dirname(projectPath), "Source");
  if (!fs.existsSync(sourceDir)) return [];

  return fs
    .readdirSync(sourceDir, { withFileTypes: true })
    .filter((entry) => entry.isFile() && /Editor\.Target\.cs$/i.test(entry.name))
    .map((entry) => entry.name.slice(0, -".Target.cs".length))
    .sort((left, right) => left.localeCompare(right));
}

/** Resolve the UBT editor target without assuming it matches the .uproject filename. */
export function resolveEditorTarget(projectPath: string, requestedTarget?: string): string {
  if (requestedTarget?.trim()) return requestedTarget.trim();

  const resolvedPath = path.resolve(projectPath);
  const projectName = path.basename(resolvedPath, ".uproject");
  const candidates = listEditorTargets(resolvedPath);
  const filenameMatch = candidates.find(
    (candidate) => candidate.toLowerCase() === `${projectName}Editor`.toLowerCase(),
  );
  if (filenameMatch) return filenameMatch;

  try {
    const descriptor = JSON.parse(fs.readFileSync(resolvedPath, "utf8")) as {
      Modules?: Array<{ Name?: unknown }>;
    };
    const moduleNames = new Set(
      (descriptor.Modules ?? [])
        .map((module) => (typeof module.Name === "string" ? module.Name.toLowerCase() : ""))
        .filter(Boolean),
    );
    const moduleMatch = candidates.find((candidate) => {
      const baseName = candidate.replace(/Editor$/i, "").toLowerCase();
      return moduleNames.has(baseName);
    });
    if (moduleMatch) return moduleMatch;
  } catch {
    // A malformed descriptor is reported by UBT; target discovery can still
    // use an unambiguous Target.cs filename or the compatibility fallback.
  }

  if (candidates.length === 1) return candidates[0];
  if (candidates.length > 1) {
    throw new Error(
      `Multiple editor targets found (${candidates.join(", ")}). Pass target explicitly.`,
    );
  }

  return `${projectName}Editor`;
}

function quoteWindowsBatchArgument(value: string): string {
  if (value.includes("\0") || value.includes('"')) {
    throw new Error("Windows batch arguments cannot contain NUL or double-quote characters.");
  }
  return `"${value}"`;
}

function getPlatformString(): string {
  if (IS_WINDOWS) return "Win64";
  if (process.platform === "darwin") return "Mac";
  return "Linux";
}

export async function buildProject(
  projectPath: string,
  opts: { onOutput?: (line: string) => void; target?: string } = {},
): Promise<BuildResult> {
  const buildTool = findUEBuildTool();
  if (!buildTool) {
    return {
      success: false,
      exitCode: null,
      message:
        "Unreal Engine build tool not found. Set UE_BUILD_TOOL_PATH or install UE5.3+ to a default location.",
    };
  }

  const resolvedPath = path.resolve(projectPath);
  if (!fs.existsSync(resolvedPath)) {
    return { success: false, exitCode: null, message: `Project file not found: ${resolvedPath}` };
  }

  let target: string;
  try {
    target = resolveEditorTarget(resolvedPath, opts.target);
  } catch (error) {
    return {
      success: false,
      exitCode: null,
      message: `Could not resolve editor target: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
  const platform = getPlatformString();

  const buildArgs = [target, platform, "Development", `-Project=${resolvedPath}`, "-WaitMutex", "-FromMsBuild"];

  return new Promise((resolve) => {
    let proc;
    if (IS_WINDOWS) {
      const command = [buildTool, ...buildArgs]
        .map(quoteWindowsBatchArgument)
        .join(" ");
      // Batch files require cmd.exe. Passing one fully quoted command string
      // prevents a spaced Engine or project path from being split by cmd.
      proc = spawn("cmd.exe", ["/d", "/v:off", "/s", "/c", `"${command}"`], {
        shell: false,
        stdio: "pipe",
        windowsVerbatimArguments: true,
      });
    } else {
      proc = spawn(buildTool, buildArgs, { shell: false, stdio: "pipe" });
    }

    const forward = (data: Buffer) => {
      const text = data.toString();
      if (opts.onOutput) opts.onOutput(text);
      else process.stdout.write(text);
    };

    if (proc.stdout) proc.stdout.on("data", forward);
    if (proc.stderr) proc.stderr.on("data", forward);

    proc.on("close", (code) => {
      resolve(
        code === 0
          ? { success: true, exitCode: 0, message: "Build succeeded" }
          : { success: false, exitCode: code, message: `Build failed with exit code ${code}` },
      );
    });

    proc.on("error", (err) => {
      resolve({ success: false, exitCode: null, message: `Build error: ${err.message}` });
    });
  });
}
