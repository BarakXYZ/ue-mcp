# v0.7.11 Release Notes

A 0.7.11 is a broad "close every outstanding GitHub issue" release. 35 issues addressed, 20+ new actions.

## New Actions

### asset
- `delete_batch` - delete many asset paths in one call, returning per-path `{deleted|absent|failed}` plus totals. Closes #109.
- `create_data_asset` - create a `UDataAsset` subclass instance of any custom type. Params: `name`, `className` (`/Script/Module.ClassName` or loaded name), `packagePath?`, `properties?`. Closes #123.

### animation
- `create_ik_rig` now accepts `retargetRoot?` and `chains[]: [{name, startBone, endBone, goal?}]`. Uses `UIKRigController` so the rig is configured atomically before save (fixes #103 race). Closes #97, #103.
- `create_ik_retargeter` - new. Create an `IKRetargeter` with optional `sourceRig` / `targetRig`. Closes #98.
- `set_anim_blueprint_skeleton` - set the target skeleton on an `AnimBlueprint` and recompile. Closes #99.
- `read_bone_track` - per-frame location/rotation/scale samples via `IAnimationDataModel::EvaluateBoneTrackTransform`. Closes #112.

### blueprint
- `add_component` now accepts `parentComponent` for SCS hierarchy placement. Closes #115.
- `set_component_property` now walks the parent-class chain's SimpleConstructionScript to find inherited components. Closes #100.
- `read_blueprint` returns `actorTick { bCanEverTick, bStartWithTickEnabled, TickInterval }` from the generated class CDO. Closes #116.
- `read_component_properties` - dumps every `UPROPERTY` on a component template incl. array contents (covers AuraInput template arrays). Closes #105.
- `read_node_property` - verify a node pin default or reflected node property actually persisted. Closes #102.
- `reparent_component` - move an SCS node under a new parent. Closes #115.
- `set_actor_tick_settings` - set `bCanEverTick` / `bStartWithTickEnabled` / `TickInterval` on the actor CDO. Closes #116.
- `add_node` with `K2Node_DynamicCast` resolves Blueprint-generated class paths (`/Game/.../BP_X.BP_X_C`) and force-reconstructs so the typed "As ClassName" output pin appears. Closes #101, #118.
- `add_node` with `K2Node_VariableGet` accepts `ownerClass` for cross-class member getters (typed Target input pin). Closes #118.
- `add_node` now calls `ReconstructNode` on every K2Node after placement - math/commutative binary operator nodes now get their pins correctly. Closes #114.
- `FindGraph` accepts `Transition[4]` index-based addressing for disambiguating duplicate graph names (AnimBP state-machine transitions). Closes #119.

### editor
- `set_pie_time_scale` - fast-forward PIE game time. Raises `WorldSettings` dilation caps and calls `SetGlobalTimeDilation` on the PIE world. Closes #126.
- `get_sequence_info` accepts `includeSectionDetails=true` - emits attach socket/component for `MovieScene3DAttachSection` and first-key values for all 9 transform channels. Closes #52, #113.

### gameplay
- `list_input_mappings` - alias for `read_imc` under a more discoverable name. Closes #110.
- `read_behavior_tree_graph` - walks the BT runtime tree (composites / tasks / decorators / services with blackboard keys). Closes #124.

### level
- `get_outliner` and `get_actor_details` accept `world` param (`editor` | `pie` | `auto`). Closes #111.
- `get_actor_details` now returns rotation, scale, attachParent, components[], and optionally reflected `UPROPERTY` values with `includeProperties=true`. Accepts `actorPath` as alternative to `actorLabel`. Closes #125.
- `set_component_property` pre-scans value strings for bare identifiers like `TargetActor=BP_Portcullis` and resolves actor labels to full object paths. Closes #121.
- `set_light_properties` accepts `rotation` for sun-angle control on `DirectionalLight`. `SkyLight` components auto-recapture after changes. Closes #94.
- `set_fog_properties` - new. Edit `ExponentialHeightFog` (density, falloff, inscattering). Closes #94.
- `get_actors_by_class` - new. Bulk-lookup actors by class name, editor or PIE. Closes #94.

### widget
- `get_widget_details` now dumps every reflected `Slot` property (anchors, position, size, alignment, padding, size rule, fill). Closes #107.
- `add_widget` handler is verified functional (was already registered); #120 was a stale-deploy issue - no Python/ctypes workaround needed. Closes #120.

### pcg
- `add_pcg_node`, `remove_pcg_node`, `connect_pcg_nodes`, `set_pcg_node_settings` now call `NotifyGraphChanged` and `MarkPackageDirty` after mutations. Ctrl+S and autosave pick up the change; open PCG editor tabs refresh. Closes #108.

### project
- `set_config` verifies persistence on disk after `GConfig->Flush` and falls back to a direct file write when `GConfig` fails to create new sections (typical for `DeveloperSettings`-backed INIs). Closes #106.

### flow
- Tool description now explicitly documents `shell` step support (`{ task: shell, options: { command: "npm run up:build" } }`). The `shell` task was already registered via `@db-lyon/flowkit`. Closes #104, #122.

## Known Limitations (Closed as `limitation`)

- **#65** - Niagara module-stage authoring (Spawn/Update) still requires `execute_python`. Renderers, emitter properties, and system specs are covered.
- **#76** - Nested `ChooserTable` row editing requires `execute_python`. The UE Chooser API does not expose `columns_structs` row value writes through a stable blueprint-callable surface.

## Tagging

All 35 previously-untagged agent-feedback issues now carry appropriate category labels (`animation`, `blueprint`, `level`, `asset`, `widget`, `pcg`, `project`, `gameplay`, `editor`, `flow`) plus `bug` or `enhancement` as applicable.
