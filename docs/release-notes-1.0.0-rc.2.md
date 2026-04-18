# v1.0.0-rc.2 Release Notes

Closes four of the five open agent-feedback issues filed after rc.1 (#153, #154, #155, #157). The fifth (#156 — CustomizableObject) is kept open as `planned` while authoring-side Mutable editor APIs are scoped. Net: four new actions, one PCG persistence bug fixed, zero breaking changes.

## New actions

### animation
- `set_sequence_properties` — batch-set root-motion properties (`enableRootMotion`, `forceRootLock`, `useNormalizedRootMotionScale`, `rootMotionRootLock`) across many `AnimSequence` assets in one call. Accepts `AnimMontage` inputs and resolves each to its first anim reference via `GetFirstAnimReference()` when `resolveFromMontages=true` (default). Returns per-path `{status, ...}` so callers can diagnose mixed outcomes. Closes #153.
- `bake_root_motion_from_bone` — bakes the translation delta from a source bone (e.g. pelvis) onto the root bone across the whole sequence, and compensates the source bone locally so world-space pose is unchanged. Reads the source track via `IAnimationDataModel::EvaluateBoneTrackTransform`, writes both tracks via `IAnimationDataController::SetBoneTrackKeys`, flips `bEnableRootMotion=true`. Params: `assetPath`, `sourceBone`, `rootBone?` (default `root`), `axes?` (default `["x","y"]`), `interpolation?` (`linear`|`per_frame`, default `linear`). Closes #154.

### asset
- `set_sk_material_slots` — writes `USkeletalMesh.Materials` directly. Accepts `slotName` or `slotIndex` per entry. Closes the gap where `blueprint(set_component_property)` on `SkeletalMeshComponent.OverrideMaterials` reported success but UE's ICH / initialization pipeline silently reverted the write. Mirror of the existing `set_mesh_material` for static meshes. Params: `assetPath`, `slots[{slotName?|slotIndex?, materialPath}]`. Partial results + per-entry errors returned. Closes #155a.
- `diagnose_registry` — runs `ScanPathsSynchronous` + dual `GetAssets` filters (disk-only vs disk+memory) to surface in-memory pending-kill ghost entries that linger after `asset(delete)`. Returns `{onDiskCount, inMemoryIncludedCount, ghostCount, ghostPaths}`. `reconcile=true` forces a full rescan and evicts the ghosts. Closes #155c.

Counterpart for #155b (`blueprint(read_component_property)`) already shipped as `blueprint(get_component_property)` in a prior release — resolves the BP CDO + inheritable component template (handles SCS, parent BP chain, ICH) and returns the post-ICH effective value. #155 is closed in full.

## Bug fixes

### pcg — `add_node`, `connect_nodes`, `remove_node`, `set_node_settings` didn't persist to disk (#157)
Mutations appeared to succeed and `read_graph` echoed them back, but the real `UPCGGraph` asset on disk stayed empty. Two root causes:

1. `UEditorAssetLibrary::SaveAsset(path)` resolves the asset fresh by path. When the handler-held `UPCGGraph*` and the path-resolved instance were different objects, the save was a no-op relative to the mutations. Every PCG save site now calls `UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false)` on the exact instance we touched.
2. Mutations ran outside `FScopedTransaction`. The PCG graph editor tab binds through the transaction system for UI refresh, so a `Save All` from the UI could write the stale state back. Every mutation now runs inside a scoped transaction with `Graph->Modify()`.

`AddPCGNode` also switched from `AddNode(UPCGSettings*)` to `AddNodeInstance(UPCGSettings*)` to match the path the PCG graph editor itself uses (creates a `UPCGSettingsInstance` wrapper parented to the new node), and reparents the underlying settings to the node so duplicate/rename of the graph keeps settings attached.

Verified against the exact reproducer in the report: after `pcg(add_node)`, unloading + reloading the asset and reading `graph.get_editor_property("nodes")` returns the added node. No shadow state.

## Internals

- `PCGHandlers.cpp` now includes `ScopedTransaction.h` (already transitively available via `UnrealEd`). No new `Build.cs` dependencies.
- Handler count: **425 → 429**. All handlers pass smoke tests against the live editor.

## Out of scope for this patch — kept open as `planned`

### #156 — CustomizableObject graph introspection + authoring
Splitting the ask:

- **Introspection and compile (next patch)** — `list_parameters`, `compile` (sync/async), `is_compiled`, `get_parameter_options`. Every API needed is a BlueprintCallable `UFUNCTION` on `UCustomizableObject`. Reachable without a hard `Build.cs` dep on the Mutable plugin via `LoadClass` + `UFunction::ProcessEvent`, matching the plugin's existing convention for optional-module handlers (Water, WaterSpline).
- **Authoring (planned, not scheduled)** — `add_object_group_param`, `add_enum_param`, `add_float_param`, `add_clip_mesh_node`, `connect_mesh`, `set_parameter_default` need node / pin editor APIs under the editor-only `CustomizableObjectEditor` module. These are not BlueprintCallable so reflection is not enough. Needs a design pass on whether to add the editor module as a conditional dep or expose a narrower native subset.

### #143 — Editor right-click menus that run flows
Still `planned`. Unchanged from rc.1.

## Migration notes

- No breaking changes. All new actions are additive.
- Existing PCG graphs mutated by pre-rc.2 handlers that appeared to lose nodes on disk are unaffected going forward: run the mutation again (or the proven Python `save_loaded_asset` recipe once) to flush the in-memory state.
