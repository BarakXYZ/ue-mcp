# v0.7.16 Release Notes

Claude Code skills bundle. `ue-mcp init` and `ue-mcp update` now drop four workflow-oriented skill guides into the project's `.claude/skills/` directory so agents can auto-load ue-mcp-specific recipes on demand.

## What ships

Four skills are bundled under the npm tarball at `skills/<name>/SKILL.md`:

- **`ue-mcp-workflow`** — required order of operations (status check first), editor lifecycle, project scoping, mutation recipe, handling "still initializing" from the bridge.
- **`ue-mcp-blueprint`** — read-then-write discipline, node/pin wiring, SCS component hierarchy, variables, interfaces, compilation vs validation.
- **`ue-mcp-native-cpp`** — the `create_cpp_class` → `write_cpp_file` → `live_coding_compile` loop, when to use `build` vs Live Coding, `add_module_dependency`, engine source lookup.
- **`ue-mcp-niagara`** — system/emitter creation, renderer stack, module inputs + static switches (v0.7.14 surface), HLSL modules, batching.

Each skill is a standard Claude Code skill file with frontmatter `name` + `description` for auto-trigger, plus a focused markdown body. Claude Code reads them from `.claude/skills/` without any MCP involvement — they're pure context, not tool calls.

## Install path

- **`npx ue-mcp init`** — if the user opts into Claude Code hooks, skills are copied automatically into `<project>/.claude/skills/<name>/SKILL.md`.
- **`npx ue-mcp update`** — whenever `.claude/` already exists in the project, skills are refreshed alongside the plugin update. This means pulling a newer ue-mcp version and running `update` keeps the bundled skills current.

Skills are only copied when Claude Code is being configured — the install is non-invasive for users of other MCP clients.

## Internals
- New `src/skills.ts` exports `installSkills(projectDir)` which walks the packaged `skills/` directory and copies every `SKILL.md` into `<projectDir>/.claude/skills/`. Existing ue-mcp skills are overwritten (so updates propagate); unrelated files in the destination are left alone.
- `package.json` `files` now includes `skills/` so the directory is published with the npm tarball.
- `src/init.ts` invokes `installSkills` inside the existing Claude Code configuration branch (right after the hooks prompt). `src/update.ts` re-runs it unconditionally when the project has a `.claude/` directory.

## Known constraints
- If you have project-local modifications to any of the four bundled skills, `update` will overwrite them. Rename the skill directory (e.g. `ue-mcp-workflow-local`) if you need to diverge.
- The skills describe action surface as of v0.7.16. Actions added in later patches won't be reflected until the next release of this bundle.
