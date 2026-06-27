import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");

const bridgeSources = [
  "plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/BridgeServer.cpp",
  "tests/ue_mcp/Content/Python/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/BridgeServer.cpp",
];

describe("bridge source hardening", () => {
  it("does not bind editor bridge sockets to every interface", () => {
    for (const relativePath of bridgeSources) {
      const source = readFileSync(resolve(repoRoot, relativePath), "utf8");

      expect(source, relativePath).not.toMatch(/\bsin_addr\.s_addr\s*=\s*(?:htonl\s*\(\s*)?INADDR_ANY\b/);
      expect(source, relativePath).toMatch(/\bINADDR_LOOPBACK\b/);
    }
  });
});
