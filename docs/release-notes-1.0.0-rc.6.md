## v1.0.0-rc.6

Code-health pass. No new actions. Internals refactor, fewer footguns, smoke-test reliability on World Partition projects.

### Bug Fixes

- `get_world_outliner` no longer hangs on World Partition levels. Default `limit` lowered from 500 to 50, and `LandscapeStreamingProxy` / `WorldPartitionHLOD` actors are skipped by default (opt in via `includeStreaming: true`). Result now reports `streamingSkipped`.
- `list_niagara_modules` no longer hangs on projects with hundreds of NiagaraScripts. Default `limit` 200, optional `pathFilter` substring filter, result now reports `totalAvailable`.
- `set_blueprint_node_default_value` now rejects malformed `DefaultValue` strings with a typed error instead of silently corrupting the node default. Returns the property type so the caller can format correctly.
- `set_blueprint_variable_default_value` now warns (instead of silently dropping) when the CDO mirror write fails. The authoritative serialised default is still written; the warning surfaces a probable type mismatch.
- `SetMaterialBaseColor` and `CreateMaterialFromTexture` now guard against `Material->GetEditorOnlyData()` returning null on unsupported material domains. Previously a null-deref crash.
- Empty `catch {}` blocks across `bridge`, `init`, `deployer`, `project`, `hook-handler`, `index`, `flow/http-server`, `resolve` replaced with structured `warn(...)` so config corruption, parse failures, and reconnect errors are no longer invisible.
- `resolveContentPath` treats a trailing `/` or `\` as a directory hint and routes through `resolveContentDir`, so `/Game/MyFolder/` is no longer rewritten to `/Game/MyFolder.uasset`.
- `editor-control` now returns a clear "Windows-only" message on macOS and Linux instead of failing with a cryptic `spawnSync` error from missing `tasklist`/`taskkill`. Engine search expanded from `C:\` only to also include `D:\`/`E:\` and `Epic Games`-without-`Program Files` layouts.
- `HandlerRegistry`'s stubbed Python-handler dispatch now returns a typed error instead of an empty `{}` JSON object (the empty object was indistinguishable from a real success).
- `LongPackageNameToFilename` boilerplate is now a single `SaveAssetPackage(asset)` helper. 42 inline copies migrated.

### Internals

- C++ god-file splits. The five biggest handler files now each under 2500 lines:
  - `BlueprintHandlers.cpp`: 4899 -> 2459 (Graph + Properties partitions extracted)
  - `AnimationHandlers.cpp`: 3376 -> 2247 (state-machine / IK / pose-search extracted)
  - `GameplayHandlers.cpp`: 3243 -> 2042 (input / IMC / PIE inspection extracted)
  - `AssetHandlers.cpp`: 3122 -> 2012 (importers extracted)
  - `MaterialHandlers.cpp`: 2900 -> 2427 (depth / preview / transactions extracted)
  - All splits keep the same class via translation-unit partitioning. Recipe-driven splitter at `scripts/split-handlers.mjs` for reproducibility.
- `FGCRootScope` RAII guard added to `HandlerUtils.h`. Eight `AddToRoot` / `RemoveFromRoot` pairs in `AssetHandlers` migrated; future early-returns in import handlers can no longer leak rooted UObjects.
- `SaveAssetPackage(UObject*)` helper added to `HandlerUtils.h`. 42 inline copies removed.
- `FindPropertyChecked` helper added for null-checked property lookup.
- Material property switch consolidated. The 15-case `switch (EMaterialProperty)` that was copied verbatim three times now lives in one `GetMaterialPropertyInput` helper. `ParseMaterialProperty` moved from 18 if-elses to a static `TMap`.
- GAS create-blueprint flow consolidated. Five `Create*` handlers (`Effect`/`Ability`/`AttributeSet`/`Cue`/`CueNotify`) shared a 40-line flow each; now share `CreateGasBlueprint(Params, defaultPath, parentClass, friendlyType, extraFields?)`.
- TypeScript: structured logging via new `src/log.ts` (debug/info/warn/error, `UE_MCP_LOG_LEVEL` env override). Empty-catch silent failures replaced project-wide.
- TypeScript: zod schemas for `.uproject`, `.ue-mcp.json`, `.uplugin` at every `JSON.parse` boundary. Wrong-typed config now logs and falls back to defaults instead of silently producing garbage.
- TypeScript: ANSI helpers and interactive selectors extracted from `init.ts` into `src/ui/ansi.ts` and `src/ui/select.ts`. `init.ts` 740 -> 496 lines.
- Schema-drift unit test: `tests/unit/drift.test.ts` enumerates every `bp("...", "bridge_method")` in the TS tool registry and hard-fails if any names a non-existent C++ handler. The reverse direction (orphan C++ handlers) is reported as a warning.
- Directive responses now carry an optional `machine` field with `{ kind, requiredActions, context }` so MCP clients that strip prose still see the directive intent. `execute_python` populates `kind: "workaround.feedback"`.
- New unit-test suite under `tests/unit/` (28 tests) for `log`, `schemas`, `project` path resolution, `directive`, and the drift guard. Runs in CI without an editor via `npm run test:unit`.
- CI: tag is now created **before** npm publish (preventing a stranded npm version with no git tag), workflow has a concurrency guard, default permissions narrowed to `contents: read` with per-job escalation, `npm audit --audit-level=high --omit=dev` and `npm run test:unit` added to the build job, and the publish job verifies `dist/index.js` / `init.js` / `update.js` are non-empty before pushing to npm.
- Smoke harness: per-method param overrides for known-heavy handlers (`list_assets`, `save_all`, `build_lighting`, etc.), and a post-failure WS reconnect with cooldown so one slow handler can't cascade into 50+ false failures.
- `.gitattributes` line-ending catch-all collision fixed. `.gitignore` source-map glob now recursive (matches `.npmignore`). `flowkit` dependency tightened from caret to tilde (0.x is breaking-on-minor per SemVer).
- Action count unified across all artifacts (`README.md`, `docs/index.md`, `docs/flows.md`, `docs/tool-reference.md`, `package.json`, `CLAUDE.md`) to **440+**, verified by `node scripts/count-handlers.mjs` (442 unique handlers, 5 aliases).
