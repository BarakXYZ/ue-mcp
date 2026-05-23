import {
  diagnosticsHaveErrors,
  emitClientConfig,
  formatDiagnostics,
  formatProjectList,
  loadProjectRegistry,
  type ClientKind,
  type RegistryDiagnostic,
} from "./project-registry.js";

interface ParsedArgs {
  command: "list" | "emit" | "doctor" | "help";
  registryPath?: string;
  client?: ClientKind;
  strict: boolean;
  includeDisabled: boolean;
  serverPath?: string;
  serverCommand?: string;
}

const args = parseArgs(process.argv.slice(2));

try {
  if (args.command === "help") {
    printUsage();
    process.exit(0);
  }

  const result = loadProjectRegistry({
    registryPath: args.registryPath,
    serverPath: args.serverPath,
    command: args.serverCommand,
  });
  const shouldFail = diagnosticsHaveErrors(result.diagnostics) || (args.strict && result.diagnostics.length > 0);

  if (args.command === "list") {
    printDiagnostics(result.diagnostics);
    console.log(formatProjectList(result.projects));
    process.exit(shouldFail ? 1 : 0);
  }

  if (args.command === "doctor") {
    if (result.diagnostics.length === 0) {
      console.log(`OK ${result.registryPath}`);
    } else {
      console.error(formatDiagnostics(result.diagnostics));
    }
    process.exit(shouldFail ? 1 : 0);
  }

  if (!args.client) {
    throw new Error("projects emit requires --client codex|claude|cursor");
  }
  printDiagnostics(result.diagnostics);
  if (shouldFail) process.exit(1);
  console.log(emitClientConfig(result.projects, {
    client: args.client,
    includeDisabled: args.includeDisabled,
  }));
} catch (e) {
  console.error(`[ue-mcp] ${e instanceof Error ? e.message : e}`);
  printUsage();
  process.exit(1);
}

function parseArgs(argv: string[]): ParsedArgs {
  if (argv.length === 0 || argv[0] === "--help" || argv[0] === "-h") {
    return {
      command: "help",
      strict: false,
      includeDisabled: false,
    };
  }

  const command = (argv[0] ?? "help") as ParsedArgs["command"];
  if (!["list", "emit", "doctor", "help"].includes(command)) {
    throw new Error(`Unknown projects command "${command}".`);
  }

  const parsed: ParsedArgs = {
    command,
    strict: false,
    includeDisabled: false,
  };

  for (let i = 1; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--registry") {
      parsed.registryPath = requireValue(argv, ++i, "--registry");
    } else if (arg.startsWith("--registry=")) {
      parsed.registryPath = arg.slice("--registry=".length);
    } else if (arg === "--client") {
      parsed.client = parseClient(requireValue(argv, ++i, "--client"));
    } else if (arg.startsWith("--client=")) {
      parsed.client = parseClient(arg.slice("--client=".length));
    } else if (arg === "--strict") {
      parsed.strict = true;
    } else if (arg === "--include-disabled") {
      parsed.includeDisabled = true;
    } else if (arg === "--server-path") {
      parsed.serverPath = requireValue(argv, ++i, "--server-path");
    } else if (arg.startsWith("--server-path=")) {
      parsed.serverPath = arg.slice("--server-path=".length);
    } else if (arg === "--command") {
      parsed.serverCommand = requireValue(argv, ++i, "--command");
    } else if (arg.startsWith("--command=")) {
      parsed.serverCommand = arg.slice("--command=".length);
    } else if (arg === "--help" || arg === "-h") {
      parsed.command = "help";
    } else {
      throw new Error(`Unknown argument "${arg}".`);
    }
  }

  return parsed;
}

function requireValue(argv: string[], index: number, flag: string): string {
  const value = argv[index];
  if (!value || value.startsWith("--")) {
    throw new Error(`${flag} requires a value.`);
  }
  return value;
}

function parseClient(value: string): ClientKind {
  if (value === "codex" || value === "claude" || value === "cursor") return value;
  throw new Error(`Unsupported client "${value}". Use codex, claude, or cursor.`);
}

function printDiagnostics(diagnostics: RegistryDiagnostic[]): void {
  if (diagnostics.length === 0) return;
  console.error(formatDiagnostics(diagnostics));
}

function printUsage(): void {
  console.error(`Usage:
  ue-mcp projects list [--registry <path>] [--strict]
  ue-mcp projects doctor [--registry <path>] [--strict]
  ue-mcp projects emit --client codex|claude|cursor [--registry <path>] [--strict]

Options:
  --registry <path>       Registry file. Defaults to UE_MCP_PROJECT_REGISTRY, nearest ue-mcp.projects.yml, then ~/.ue-mcp/projects.yml.
  --server-path <path>    Override the emitted server entry path.
  --command <command>     Override the emitted command. Default: node.
  --include-disabled      Include disabled projects in emitted config.
  --strict                Treat warnings as fatal.`);
}
