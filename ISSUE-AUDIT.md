# Issue Audit — 2026-04-24

## Quick Wins (bug fixes / param additions)
| # | Title | Fix |
|---|-------|-----|
| 187 | move_actor ignores scale | Bridge handler not applying scale param (C++) |
| 162 | Bridge readiness gate never clears | `SetEditorReady()` has no call sites — add to `OnPostEngineInit` (C++) |
| 181 | add_variable coerces unknown varType to 'real' | Bridge should validate varType and error instead of defaulting (C++) |
| 180 | set_variable_properties missing exposeOnSpawn | Add `exposeOnSpawn` param mapping in TS + bridge handler support |

## Medium — new action + bridge handler
| # | Title | Fix |
|---|-------|-----|
| 188 | No way to read actor mesh bounds | New `get_actor_bounds` → `GetActorBounds()` |
| 193 | StaticMesh bounds inspection | New `get_mesh_bounds` → `GetBoundingBox()` |
| 177 | Static mesh collision inspection | New `get_mesh_collision` → read BodySetup |
| 197 | add_imc_mapping crashes on SavePackage | Fix save flags or use `MarkPackageDirty()` (C++) |
| 169 | set_mapping_modifiers drops modifiers | Fix modifier class resolution in GameplayHandlers.cpp |
| 178 | Runtime actor name → placed actor resolution | New `resolve_actor` action |
| 163 | RecastNavMesh property inspection | New `get_navmesh_details` in gameplay.ts |
| 182 | Can't set CDO props on native C++ classes | New `set_cdo_property` with className param |
| 192 | Move/rename whole Content folder | New `move_folder` batched rename action |
| 186 | No way to apply damage in PIE | New `call_pie_function` or `apply_damage_in_pie` |
| 183 | DeveloperSettings UPROPERTY read/write | New `get_cdo_properties` / `set_cdo_property` (overlaps #182) |

## Larger but well-scoped
| # | Title | Fix |
|---|-------|-----|
| 199/196 | Nested TArray\<FStruct\> with UObject refs | Fix C++ property deserializer — single fix, two issues |
| 189 | K2Node_CallDelegate not bound | Fix `DelegateReference` binding in add_node handler |
| 168 | Enum/struct/datatable editing | 3 new actions: `set_enum_display_name`, struct field ops, `set_datatable_row` |
| 198 | Invoke BlueprintCallable functions | New `call_function` → `ProcessEvent` on CDO |
| 185 | Niagara scratch module + material expression props | Test `set_expression_value` for Noise props; new `create_scratch_module` |
| 195 | Construction script component verification | New `run_construction_script` (spawn temp, inspect, destroy) |
| 161 | UMG delegate inspection on running EUW | New `get_runtime_delegates` + `invoke_runtime_delegate` |

## Duplicates / Overlaps
| Close | Subsumes | Reason |
|-------|----------|--------|
| 176 | → 178 | Same actor resolution gap |
| 174 | → 178 | Same + proxy spawning already possible via place_actor |
| 165 | → 166 | Same map inspection gap |
| 164 | → 177 + 163 | Collision = #177, nav queries = gameplay.project_to_nav |
| 172 | → 188 + 163 | Bounds = #188, nav relevance = #163 |
| 171 | → 175 + 177 | Components = #175, asset BodySetup = #177 |

## Already Working (test before closing)
| # | Title | Why |
|---|-------|-----|
| 194 | Blueprint asset search by partial name | `asset(search, query=...)` and `asset(search_fts)` exist |
| 184 | BP component defaults + StateTree links | `read_component_properties` exists — test with nested asset refs |
| 190 | BP instance variables + graph introspection | `get_actor_details(includeProperties=true)` + `read_graph` exist |
| 191 | Inspect live PIE BP actor state | `inspect_pie` + `get_actor_details(world="pie")` exist |
| 173 | GeoReferencingSystem config | `editor(set_property)` may work on actor UPROPERTYs — test |
| 175 | Component collision/walkability | `set_component_property` may handle these — test |
| 170 | Ontology query too large | Add pagination/filter — or may be moot if FTS search is used instead |
