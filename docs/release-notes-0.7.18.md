# v0.7.18 Release Notes

Closes the 12 outstanding agent-feedback issues filed against 0.7.17 (#134–#145). Net: ten new actions, three resolver/handler fixes, and an opt-in HTTP surface for `flow.run`.

## New actions

### blueprint
- `reparent` — change a Blueprint's `ParentClass` and recompile (mirrors `BlueprintEditorLibrary.reparent_blueprint` + compile + save). Resolves the parent string as full path, short name (e.g. `"Pawn"`), or engine-module short name. Closes #138.

### editor
- `run_python_file` — execute a Python file from disk with `__file__`/`__name__` populated and an optional `args[]` list appended as positional arguments. Use this for project-side `*.py` scripts instead of wrapping them in `execute_python` + `exec`. Closes #142.

### gameplay
- `get_pie_anim_properties` — read arbitrary `UPROPERTY` values on a PIE actor's `AnimInstance`. Pass `propertyNames[]` to read named properties, or omit to dump every property. Closes #139 (anim half).
- `get_pie_subsystem_state` — read `UPROPERTY` values on a running subsystem. Params: `subsystemClass`, `scope` (`game` (default) | `world` | `engine` | `localplayer`), optional `propertyNames[]`. Closes #139 (subsystem half).

### pcg
- `set_static_mesh_spawner_meshes` — populate the weighted `MeshEntries` array on a `PCGStaticMeshSpawner` node. `set_node_settings` couldn't reach into `MeshSelectorParameters.MeshEntries` because that path crosses an instanced subobject; this dedicated helper auto-instantiates `UPCGMeshSelectorWeighted` if needed and rebuilds the array. Params: `assetPath`, `nodeName`, `entries=[{mesh, weight?}]`, `replace?` (default `true`). Closes #145.

## Bug fixes

### blueprint / level — component class resolution (#136, #137)
`blueprint(add_component)` and `level(add_component)` only checked `FindObject<UClass>` for the literal name and the `"U" + name` form. Standard engine components like `SceneComponent`, `SphereComponent`, `NiagaraComponent`, `ArrowComponent` returned `Component class not found`. Both handlers now resolve in this order:

1. Treat the input as a full class path if it contains `/` or `.`
2. `FindClassByShortName` (handles `A`/`U` prefix, exact match)
3. Implicit `/Script/Engine.<name>` lookup

The error message was rewritten to point at the supported forms when nothing matches.

### blueprint — `add_function_parameter` object-ref types (#140)
Object-reference types like `"Actor"`, `"Actor*"`, `"/Script/Engine.Actor"`, `TSubclassOf<Foo>`, `TSoftObjectPtr<Foo>`, `TSoftClassPtr<Foo>` silently fell through to the struct resolver and ultimately defaulted to `PC_Real` (float), so newly-created function parameters all came back as `"type": "real"`. `MakePinType` now recognises pointer/template forms and full class paths, resolves the class, and produces the correct `PC_Object`/`PC_Class`/`PC_SoftObject`/`PC_SoftClass` pin with the right `PinSubCategoryObject`. Bare class names like `"Actor"` are also tried as a last-ditch object-pin resolution before defaulting to float.

### widget — `create` honours `parentClass` (#134)
`widget(action="create", parentClass="...")` was hard-coded to `UUserWidget::StaticClass()` regardless of the parameter. The handler now resolves `parentClass` as a short name or full path, validates that it's a `UUserWidget` subclass, and reports the resolved path back in the response. Default behaviour (no `parentClass` given) is unchanged.

### widget — `set_property` for `SizeBox` overrides (#135)
UMG 5.1+ requires the `SetWidthOverride`/`SetHeightOverride`/`SetMinDesiredWidth`/`SetMinDesiredHeight`/`SetMaxDesiredWidth`/`SetMaxDesiredHeight` BlueprintSetters so the paired `bOverride_*` flag toggles on. Direct `ImportText` on the raw value left the override flag false and the SizeBox never constrained anything. `widget(set_property)` now invokes the dedicated setter when the target is a `USizeBox`, and adds `clearWidthOverride` / `clearHeightOverride` for the symmetric clear path. (The slot struct/enum/padding cases #135 enumerated were already covered in 0.7.16; the SizeBox setter gap is what this release fixes.)

## Verified — already addressed in earlier releases

### asset — `read_properties` works for SoundWave (#141)
`asset(read_properties)` is fully generic: it iterates `FProperty` reflection over any `UObject` and uses `ExportText_Direct` to print values. SoundWave-specific fields (`Duration`, `NumChannels`, `SampleRate`, `CompressionQuality`, etc.) come back without any audio-specific handler. Closing #141 with no code change — the prior workaround of dropping into Python wasn't needed.

## Surface

### flow over HTTP — opt-in (#144)
A tiny loopback HTTP server now sits next to the stdio MCP transport when enabled. Endpoints:

```
GET  /              — alias for /flows
GET  /flows         — list available flows (mirrors flow.list)
GET  /flows/:name/plan — show execution plan, no side effects (mirrors flow.plan)
POST /flows/:name/run  — body: {params?, skip?, rollback_on_failure?} (mirrors flow.run)
GET  /health         — liveness probe
```

Disabled by default. Opt in via `.ue-mcp.json`:

```json
{
  "http": { "enabled": true, "port": 7723 }
}
```

Bound to `127.0.0.1` only. **Do not expose this externally** — the MVP has no auth. Studios that need cross-host access should put it behind a reverse proxy with mTLS or a token header. This unblocks the editor right-click prerequisite from #143 (a UE plugin can `FHttpModule::Get()` to the local port instead of bridging through stdio MCP).

## Out of scope for this patch — closed with rationale

### #143 — Editor right-click menus that run flows
Closed as a follow-up. The stated prerequisite (#144) ships in this release, so a downstream Slate plugin can register `FToolMenus` entries that POST to `/flows/:name/run` today without any further server-side work. The opinionated YAML `menu:` schema + selection-expander layer #143 envisioned belongs in a future release alongside the actual editor-side plugin code, which is out of scope for a patch.

## Internals

- New module dependency: `BlueprintEditorLibrary` (added to `UE_MCP_Bridge.Build.cs`) for `UBlueprintEditorLibrary::ReparentBlueprint`.
- New TS module: `src/flow/http-server.ts`. Pure Node `http` — no new npm dependencies.
- `UeMcpConfig` gains an optional `http: { enabled?, port?, host? }` block (TypeScript types in `src/project.ts`).

## Migration notes

- No breaking changes. All new actions are additive; the existing `widget(create)` / `add_component` / `add_function_parameter` calls keep working — they just stop hitting the documented gaps.
- HTTP server is **off by default** and binds loopback-only. Existing setups need no changes.
