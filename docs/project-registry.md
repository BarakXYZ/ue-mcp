# Project Registry

Use a project registry when one machine or repo needs UE-MCP access to more than one Unreal project. The registry is a local routing file that resolves project paths for the current OS and emits MCP client config with one isolated server process per project.

This is the recommended production posture for multi-project setups:

1. Keep each MCP server process bound to one `.uproject`.
2. Give each project a unique bridge port.
3. Generate client config from a registry instead of hand-maintaining duplicated MCP blocks.
4. Use `project(action="set_project")` only for temporary interactive switching; plugin, flow, disabled-tool, and HTTP state are bound at server startup.

## Registry Discovery

`ue-mcp projects ...` searches in this order:

1. `--registry <path>`
2. `UE_MCP_PROJECT_REGISTRY`
3. nearest `ue-mcp.projects.yml` walking upward from the current directory
4. `~/.ue-mcp/projects.yml`

## Example

```yaml
version: 1

defaults:
  serverPrefix: ue-mcp
  command: node
  serverPath: ${registryDir}/unreal/External/ue-mcp/dist/index.js
  startupTimeoutSec: 30
  bridgePortBase: 9877

projects:
  desktop-avatar:
    name: Desktop Avatar
    uproject:
      win32: ${env:XYZ_ROOT}/unreal/DesktopAvatar.uproject
      darwin: ${env:XYZ_ROOT}/unreal/DesktopAvatar.uproject
      linux: ${env:XYZ_ROOT}/unreal/DesktopAvatar.uproject

  vr-prototype:
    name: VR Prototype
    serverName: ue-mcp-vr
    uproject:
      win32: D:/Projects/VR/VR.uproject
      default: /Users/me/Projects/VR/VR.uproject
    bridge:
      port: 9901
```

If a project omits `bridge.port`, UE-MCP assigns a deterministic port from `defaults.bridgePortBase` plus the enabled project index. Explicit ports are still recommended when the registry is shared by a team.

## Path Templates

Registry paths support:

| Token | Meaning |
|-------|---------|
| `${registryDir}` | Directory containing the registry file |
| `${cwd}` | Current working directory |
| `${home}` or `~` | Current user's home directory |
| `${env:NAME}` | Required environment variable |

Relative paths resolve from `${registryDir}`. Project paths must point to `.uproject` files.

## Commands

List projects and diagnostics:

```bash
npx ue-mcp projects list --registry ue-mcp.projects.yml
```

Fail on any warning or error:

```bash
npx ue-mcp projects doctor --strict
```

Emit Codex config:

```bash
npx ue-mcp projects emit --client codex
```

Emit Claude or Cursor style JSON:

```bash
npx ue-mcp projects emit --client claude
npx ue-mcp projects emit --client cursor
```

## Generated Config

For Codex, each enabled project becomes its own MCP server:

```toml
[mcp_servers."ue-mcp-desktop-avatar"]
command = "node"
args = ["<ue-mcp-root>/dist/index.js", "<project-root>/DesktopAvatar.uproject", "--bridge-port", "9877"]
startup_timeout_sec = 30

[mcp_servers."ue-mcp-vr"]
command = "node"
args = ["<ue-mcp-root>/dist/index.js", "<project-root>/VR.uproject", "--bridge-port", "9901"]
startup_timeout_sec = 30
```

The bridge port is passed as a server startup override. `editor(action="start_editor")` uses that same port when launching Unreal. If you launch Unreal manually, make sure the matching project `ue-mcp.yml` also sets the same `ue-mcp.bridge.port`.
