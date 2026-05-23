import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import yaml from "js-yaml";
import { z } from "zod";

const PROJECT_ID_RE = /^[A-Za-z0-9][A-Za-z0-9_.-]*$/;
const SERVER_NAME_RE = /^[A-Za-z0-9][A-Za-z0-9_.-]*$/;

type PathSpec = string | {
  win32?: string;
  darwin?: string;
  linux?: string;
  default?: string;
};

const PlatformPathMapSchema = z
  .object({
    win32: z.string().optional(),
    darwin: z.string().optional(),
    linux: z.string().optional(),
    default: z.string().optional(),
  })
  .passthrough();

const PathSpecSchema = z.union([z.string(), PlatformPathMapSchema]);

const BridgeSchema = z
  .object({
    host: z.string().optional(),
    port: z.number().int().min(1).max(65535).optional(),
  })
  .passthrough();

const RegistryDefaultsSchema = z
  .object({
    serverPrefix: z.string().optional(),
    command: z.string().optional(),
    serverPath: PathSpecSchema.optional(),
    startupTimeoutSec: z.number().int().positive().optional(),
    bridgePortBase: z.number().int().min(1).max(65535).optional(),
  })
  .passthrough();

const RegistryProjectSchema = z
  .object({
    name: z.string().optional(),
    enabled: z.boolean().optional(),
    uproject: PathSpecSchema.optional(),
    projectPath: PathSpecSchema.optional(),
    path: PathSpecSchema.optional(),
    serverName: z.string().optional(),
    command: z.string().optional(),
    serverPath: PathSpecSchema.optional(),
    startupTimeoutSec: z.number().int().positive().optional(),
    bridge: BridgeSchema.optional(),
  })
  .passthrough();

const RegistrySchema = z
  .object({
    version: z.literal(1).default(1),
    defaults: RegistryDefaultsSchema.optional(),
    projects: z.record(RegistryProjectSchema),
  })
  .passthrough();

export type ProjectRegistryFile = z.infer<typeof RegistrySchema>;
export type RegistryProjectFile = z.infer<typeof RegistryProjectSchema>;

export interface ResolvedProject {
  id: string;
  name: string;
  enabled: boolean;
  serverName: string;
  command: string;
  serverPath: string;
  projectPath: string;
  startupTimeoutSec: number;
  bridgeHost: "localhost" | "127.0.0.1";
  bridgePort: number;
  exists: boolean;
}

export type DiagnosticSeverity = "error" | "warning";

export interface RegistryDiagnostic {
  severity: DiagnosticSeverity;
  code: string;
  message: string;
  projectId?: string;
}

export interface LoadProjectRegistryOptions {
  registryPath?: string;
  cwd?: string;
  platform?: NodeJS.Platform;
  command?: string;
  serverPath?: string;
}

export interface ProjectRegistryLoadResult {
  registryPath: string;
  registry: ProjectRegistryFile;
  projects: ResolvedProject[];
  diagnostics: RegistryDiagnostic[];
}

export type ClientKind = "codex" | "claude" | "cursor";

export interface EmitClientConfigOptions {
  client: ClientKind;
  includeDisabled?: boolean;
}

export function findProjectRegistry(startDir = process.cwd()): string | null {
  const envPath = process.env.UE_MCP_PROJECT_REGISTRY;
  if (envPath) return path.resolve(expandLeadingHome(envPath));

  let dir = path.resolve(startDir);
  while (true) {
    const candidate = path.join(dir, "ue-mcp.projects.yml");
    if (fs.existsSync(candidate)) return candidate;
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }

  const homeCandidate = path.join(os.homedir(), ".ue-mcp", "projects.yml");
  return fs.existsSync(homeCandidate) ? homeCandidate : null;
}

export function loadProjectRegistry(options: LoadProjectRegistryOptions = {}): ProjectRegistryLoadResult {
  const cwd = options.cwd ? path.resolve(options.cwd) : process.cwd();
  const platform = options.platform ?? process.platform;
  const registryPath = options.registryPath
    ? path.resolve(cwd, expandLeadingHome(options.registryPath))
    : findProjectRegistry(cwd);
  if (!registryPath) {
    throw new Error(
      "No project registry found. Create ue-mcp.projects.yml in this tree, set UE_MCP_PROJECT_REGISTRY, or use --registry <path>.",
    );
  }

  const raw = yaml.load(fs.readFileSync(registryPath, "utf-8"));
  const parsed = RegistrySchema.safeParse(raw);
  if (!parsed.success) {
    throw new Error(`Invalid project registry ${registryPath}: ${parsed.error.message}`);
  }

  const registry = parsed.data;
  const registryDir = path.dirname(registryPath);
  const diagnostics: RegistryDiagnostic[] = [];
  const projects: ResolvedProject[] = [];
  const defaults = registry.defaults ?? {};
  const serverPrefix = defaults.serverPrefix ?? "ue-mcp";
  const bridgePortBase = defaults.bridgePortBase ?? 9877;
  let autoPortOffset = 0;

  for (const [id, project] of Object.entries(registry.projects)) {
    const enabled = project.enabled !== false;
    const projectId = id;
    if (!PROJECT_ID_RE.test(projectId)) {
      diagnostics.push({
        severity: "error",
        code: "invalid-project-id",
        projectId,
        message: `Project id "${projectId}" must match ${PROJECT_ID_RE.source}.`,
      });
    }

    const pathSpec = project.uproject ?? project.projectPath ?? project.path;
    if (!pathSpec) {
      diagnostics.push({
        severity: "error",
        code: "missing-project-path",
        projectId,
        message: `Project "${projectId}" must set uproject, projectPath, or path.`,
      });
      continue;
    }

    let projectPath: string;
    try {
      projectPath = resolvePathSpec(pathSpec, { registryDir, cwd, platform });
    } catch (e) {
      diagnostics.push({
        severity: "error",
        code: "unresolved-project-path",
        projectId,
        message: e instanceof Error ? e.message : String(e),
      });
      continue;
    }

    const explicitPort = project.bridge?.port;
    const bridgePort = explicitPort ?? bridgePortBase + autoPortOffset;
    if (enabled) autoPortOffset += 1;
    if (bridgePort > 65535) {
      diagnostics.push({
        severity: "error",
        code: "invalid-bridge-port",
        projectId,
        message: `Bridge port ${bridgePort} is outside the valid TCP port range.`,
      });
    }

    const bridgeHost = normalizeBridgeHost(project.bridge?.host, diagnostics, projectId);
    const serverName = project.serverName ?? `${serverPrefix}-${projectId}`;
    if (!SERVER_NAME_RE.test(serverName)) {
      diagnostics.push({
        severity: "error",
        code: "invalid-server-name",
        projectId,
        message: `Server name "${serverName}" must match ${SERVER_NAME_RE.source}.`,
      });
    }

    const command = options.command ?? project.command ?? defaults.command ?? "node";
    let serverPath: string;
    try {
      serverPath = options.serverPath
        ? resolveConfigPath(options.serverPath, { registryDir, cwd })
        : project.serverPath
          ? resolvePathSpec(project.serverPath, { registryDir, cwd, platform })
          : defaults.serverPath
            ? resolvePathSpec(defaults.serverPath, { registryDir, cwd, platform })
            : defaultServerPath();
    } catch (e) {
      diagnostics.push({
        severity: "error",
        code: "unresolved-server-path",
        projectId,
        message: e instanceof Error ? e.message : String(e),
      });
      continue;
    }

    if (path.extname(projectPath) !== ".uproject") {
      diagnostics.push({
        severity: "error",
        code: "project-path-not-uproject",
        projectId,
        message: `Project path must point to a .uproject file: ${projectPath}`,
      });
    }

    const exists = fs.existsSync(projectPath);
    if (enabled && !exists) {
      diagnostics.push({
        severity: "error",
        code: "project-path-missing",
        projectId,
        message: `Project path does not exist on this machine: ${projectPath}`,
      });
    }

    if (enabled && isNodeCommand(command)) {
      try {
        const stat = fs.statSync(serverPath);
        if (!stat.isFile()) {
          diagnostics.push({
            severity: "error",
            code: "server-path-not-file",
            projectId,
            message: `Server path must point to a file when command is node: ${serverPath}`,
          });
        }
      } catch {
        diagnostics.push({
          severity: "error",
          code: "server-path-missing",
          projectId,
          message: `Server path does not exist on this machine: ${serverPath}`,
        });
      }
    }

    projects.push({
      id: projectId,
      name: project.name ?? projectId,
      enabled,
      serverName,
      command,
      serverPath,
      projectPath,
      startupTimeoutSec: project.startupTimeoutSec ?? defaults.startupTimeoutSec ?? 30,
      bridgeHost,
      bridgePort,
      exists,
    });
  }

  diagnostics.push(...collectCrossProjectDiagnostics(projects));
  return { registryPath, registry, projects, diagnostics };
}

export function emitClientConfig(projects: ResolvedProject[], options: EmitClientConfigOptions): string {
  const selected = options.includeDisabled ? projects : projects.filter((p) => p.enabled);
  if (options.client === "codex") return emitCodexToml(selected);
  return emitMcpJson(selected);
}

export function formatProjectList(projects: ResolvedProject[]): string {
  const rows = projects.map((project) => {
    const status = project.enabled ? (project.exists ? "ready" : "missing") : "disabled";
    return [
      project.id,
      project.serverName,
      status,
      `${project.bridgeHost}:${project.bridgePort}`,
      project.projectPath,
    ];
  });
  const widths = [2, 6, 6, 6, 4].map((min, index) =>
    Math.max(min, ...rows.map((row) => row[index].length)),
  );
  const header = ["id", "server", "status", "bridge", "path"]
    .map((value, index) => value.padEnd(widths[index]))
    .join("  ");
  const body = rows.map((row) => row.map((value, index) => value.padEnd(widths[index])).join("  "));
  return [header, ...body].join("\n");
}

export function diagnosticsHaveErrors(diagnostics: RegistryDiagnostic[]): boolean {
  return diagnostics.some((d) => d.severity === "error");
}

export function formatDiagnostics(diagnostics: RegistryDiagnostic[]): string {
  return diagnostics
    .map((d) => {
      const scope = d.projectId ? ` [${d.projectId}]` : "";
      return `${d.severity.toUpperCase()} ${d.code}${scope}: ${d.message}`;
    })
    .join("\n");
}

function collectCrossProjectDiagnostics(projects: ResolvedProject[]): RegistryDiagnostic[] {
  const diagnostics: RegistryDiagnostic[] = [];
  const enabled = projects.filter((p) => p.enabled);
  const serverNames = new Map<string, ResolvedProject[]>();
  const bridgePorts = new Map<number, ResolvedProject[]>();

  for (const project of enabled) {
    pushMap(serverNames, project.serverName, project);
    pushMap(bridgePorts, project.bridgePort, project);
  }

  for (const [serverName, dupes] of serverNames) {
    if (dupes.length > 1) {
      diagnostics.push({
        severity: "error",
        code: "duplicate-server-name",
        message: `Enabled projects share MCP server name "${serverName}": ${dupes.map((p) => p.id).join(", ")}`,
      });
    }
  }

  for (const [port, dupes] of bridgePorts) {
    if (dupes.length > 1) {
      diagnostics.push({
        severity: "error",
        code: "duplicate-bridge-port",
        message: `Enabled projects share bridge port ${port}: ${dupes.map((p) => p.id).join(", ")}`,
      });
    }
  }

  return diagnostics;
}

function pushMap<K, V>(map: Map<K, V[]>, key: K, value: V): void {
  const current = map.get(key) ?? [];
  current.push(value);
  map.set(key, current);
}

function emitCodexToml(projects: ResolvedProject[]): string {
  const blocks: string[] = [];
  for (const project of projects) {
    blocks.push(
      [
        `[mcp_servers.${tomlString(project.serverName)}]`,
        `command = ${tomlString(project.command)}`,
        `args = ${tomlArray(serverArgs(project))}`,
        `startup_timeout_sec = ${project.startupTimeoutSec}`,
      ].join("\n"),
    );
  }
  return blocks.join("\n\n");
}

function emitMcpJson(projects: ResolvedProject[]): string {
  const mcpServers: Record<string, { command: string; args: string[] }> = {};
  for (const project of projects) {
    mcpServers[project.serverName] = {
      command: project.command,
      args: serverArgs(project),
    };
  }
  return JSON.stringify({ mcpServers }, null, 2);
}

function serverArgs(project: ResolvedProject): string[] {
  const args = [
    project.serverPath,
    project.projectPath,
    "--bridge-port",
    String(project.bridgePort),
  ];
  if (project.bridgeHost !== "127.0.0.1") {
    args.push("--bridge-host", project.bridgeHost);
  }
  return args;
}

function isNodeCommand(command: string): boolean {
  const base = path.basename(command).toLowerCase();
  return base === "node" || base === "node.exe";
}

function tomlString(value: string): string {
  return JSON.stringify(value);
}

function tomlArray(values: string[]): string {
  return `[${values.map(tomlString).join(", ")}]`;
}

function resolvePathSpec(
  spec: PathSpec,
  context: { registryDir: string; cwd: string; platform: NodeJS.Platform },
): string {
  if (typeof spec === "string") return resolveConfigPath(spec, context);
  const selected = spec[context.platform as keyof PathSpec] ?? spec.default;
  if (!selected) {
    throw new Error(`No path entry for platform "${context.platform}" and no default fallback.`);
  }
  return resolveConfigPath(selected, context);
}

function resolveConfigPath(value: string, context: { registryDir: string; cwd: string }): string {
  const expanded = expandTemplate(value, context);
  return path.isAbsolute(expanded)
    ? path.normalize(expanded)
    : path.resolve(context.registryDir, expanded);
}

function expandTemplate(value: string, context: { registryDir: string; cwd: string }): string {
  const withHome = expandLeadingHome(value)
    .replace(/\$\{home\}/g, os.homedir())
    .replace(/\$\{cwd\}/g, context.cwd)
    .replace(/\$\{registryDir\}/g, context.registryDir);

  return withHome.replace(/\$\{env:([A-Za-z_][A-Za-z0-9_]*)\}/g, (_match, name: string) => {
    const envValue = process.env[name];
    if (!envValue) throw new Error(`Environment variable ${name} is not set.`);
    return envValue;
  });
}

function expandLeadingHome(value: string): string {
  if (value === "~") return os.homedir();
  if (value.startsWith("~/") || value.startsWith("~\\")) {
    return path.join(os.homedir(), value.slice(2));
  }
  return value;
}

function normalizeBridgeHost(
  host: string | undefined,
  diagnostics: RegistryDiagnostic[],
  projectId: string,
): "localhost" | "127.0.0.1" {
  if (!host || host === "localhost" || host === "127.0.0.1") {
    return host === "localhost" ? "localhost" : "127.0.0.1";
  }
  diagnostics.push({
    severity: "warning",
    code: "bridge-host-not-loopback",
    projectId,
    message: `Bridge host "${host}" is ignored; UE-MCP only supports localhost or 127.0.0.1 because the bridge has no network auth.`,
  });
  return "127.0.0.1";
}

function defaultServerPath(): string {
  return path.join(path.dirname(fileURLToPath(import.meta.url)), "index.js");
}
