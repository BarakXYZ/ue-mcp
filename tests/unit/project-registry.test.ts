import { describe, it, expect, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import yaml from "js-yaml";
import {
  diagnosticsHaveErrors,
  emitClientConfig,
  loadProjectRegistry,
} from "../../src/project-registry.js";

function makeTempDir(): string {
  return fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-registry-test-"));
}

function writeProject(dir: string, name: string): string {
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.7" }));
  return uproject;
}

function writeServer(dir: string): string {
  const serverDir = path.join(dir, "dist");
  fs.mkdirSync(serverDir, { recursive: true });
  const serverPath = path.join(serverDir, "index.js");
  fs.writeFileSync(serverPath, "#!/usr/bin/env node\n");
  return serverPath;
}

const oldEnv = { ...process.env };

afterEach(() => {
  process.env = { ...oldEnv };
});

describe("project registry", () => {
  it("resolves platform paths, templates, defaults, and auto bridge ports", () => {
    const root = makeTempDir();
    const desktopDir = path.join(root, "unreal");
    fs.mkdirSync(desktopDir);
    const desktopProject = writeProject(desktopDir, "DesktopAvatar");
    const vrRoot = makeTempDir();
    const vrProject = writeProject(vrRoot, "VR");
    const serverPath = writeServer(root);
    process.env.UE_MCP_TEST_VR = vrRoot;

    const registryPath = path.join(root, "ue-mcp.projects.yml");
    fs.writeFileSync(
      registryPath,
      yaml.dump({
        version: 1,
        defaults: {
          serverPrefix: "cast",
          serverPath,
          bridgePortBase: 9910,
        },
        projects: {
          desktop: {
            uproject: "./unreal/DesktopAvatar.uproject",
          },
          vr: {
            uproject: {
              [process.platform]: "${env:UE_MCP_TEST_VR}/VR.uproject",
              default: desktopProject,
            },
            serverName: "ue-mcp-vr",
            bridge: { port: 9950 },
          },
        },
      }),
    );

    const result = loadProjectRegistry({ registryPath });
    expect(diagnosticsHaveErrors(result.diagnostics)).toBe(false);
    expect(result.projects.map((p) => p.serverName)).toEqual(["cast-desktop", "ue-mcp-vr"]);
    expect(result.projects[0].projectPath).toBe(path.resolve(desktopProject));
    expect(result.projects[0].bridgePort).toBe(9910);
    expect(result.projects[1].projectPath).toBe(path.resolve(vrProject));
    expect(result.projects[1].bridgePort).toBe(9950);
  });

  it("reports duplicate enabled bridge ports and server names", () => {
    const root = makeTempDir();
    const projectA = writeProject(root, "A");
    const projectB = writeProject(root, "B");
    const serverPath = writeServer(root);
    const registryPath = path.join(root, "ue-mcp.projects.yml");
    fs.writeFileSync(
      registryPath,
      yaml.dump({
        version: 1,
        defaults: { serverPath },
        projects: {
          a: {
            uproject: projectA,
            serverName: "same",
            bridge: { port: 9877 },
          },
          b: {
            uproject: projectB,
            serverName: "same",
            bridge: { port: 9877 },
          },
        },
      }),
    );

    const result = loadProjectRegistry({ registryPath });
    expect(result.diagnostics.map((d) => d.code)).toContain("duplicate-server-name");
    expect(result.diagnostics.map((d) => d.code)).toContain("duplicate-bridge-port");
    expect(diagnosticsHaveErrors(result.diagnostics)).toBe(true);
  });

  it("emits Codex TOML with isolated project and bridge args", () => {
    const root = makeTempDir();
    const project = writeProject(root, "A");
    const serverPath = writeServer(root);
    const registryPath = path.join(root, "ue-mcp.projects.yml");
    fs.writeFileSync(
      registryPath,
      yaml.dump({
        version: 1,
        projects: {
          a: {
            uproject: project,
            bridge: { host: "localhost", port: 9901 },
          },
        },
      }),
    );

    const result = loadProjectRegistry({
      registryPath,
      serverPath,
    });
    const emitted = emitClientConfig(result.projects, { client: "codex" });
    expect(emitted).toContain('[mcp_servers."ue-mcp-a"]');
    expect(emitted).toContain('"--bridge-port", "9901"');
    expect(emitted).toContain('"--bridge-host", "localhost"');
    expect(emitted).toContain("startup_timeout_sec = 30");
  });

  it("emits MCP JSON for Claude and Cursor style clients", () => {
    const root = makeTempDir();
    const project = writeProject(root, "A");
    const serverPath = writeServer(root);
    const registryPath = path.join(root, "ue-mcp.projects.yml");
    fs.writeFileSync(
      registryPath,
      yaml.dump({
        version: 1,
        projects: {
          a: { uproject: project },
        },
      }),
    );

    const result = loadProjectRegistry({
      registryPath,
      serverPath,
    });
    const emitted = JSON.parse(emitClientConfig(result.projects, { client: "claude" })) as {
      mcpServers: Record<string, { command: string; args: string[] }>;
    };
    expect(emitted.mcpServers["ue-mcp-a"].command).toBe("node");
    expect(emitted.mcpServers["ue-mcp-a"].args).toContain(project);
    expect(emitted.mcpServers["ue-mcp-a"].args).toContain("9877");
  });

  it("treats missing enabled project paths as fatal diagnostics", () => {
    const root = makeTempDir();
    const serverPath = writeServer(root);
    const registryPath = path.join(root, "ue-mcp.projects.yml");
    fs.writeFileSync(
      registryPath,
      yaml.dump({
        version: 1,
        defaults: { serverPath },
        projects: {
          missing: { uproject: path.join(root, "Missing.uproject") },
        },
      }),
    );

    const result = loadProjectRegistry({ registryPath });
    expect(result.diagnostics.map((d) => d.code)).toContain("project-path-missing");
    expect(diagnosticsHaveErrors(result.diagnostics)).toBe(true);
  });

  it("treats missing node server paths as fatal diagnostics", () => {
    const root = makeTempDir();
    const project = writeProject(root, "A");
    const registryPath = path.join(root, "ue-mcp.projects.yml");
    fs.writeFileSync(
      registryPath,
      yaml.dump({
        version: 1,
        defaults: { serverPath: path.join(root, "dist", "missing.js") },
        projects: {
          a: { uproject: project },
        },
      }),
    );

    const result = loadProjectRegistry({ registryPath });
    expect(result.diagnostics.map((d) => d.code)).toContain("server-path-missing");
    expect(diagnosticsHaveErrors(result.diagnostics)).toBe(true);
  });
});
