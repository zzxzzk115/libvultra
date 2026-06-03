# Unified Material Asset Phase 1 Handoff

Date: 2026-06-02

## Completed

- Reframed `ai/specs/unified-material-assets.md` around unified `.vmat.json`
  material assets.
- Updated `ai/tasks/unified-material-assets.md` with the phase-by-phase plan and
  the explicit no-migration import policy.
- Recorded `.vmat.json` and `.vmatgraph.json` roles in
  `ai/knowledge/vultra-formats.md`.
- Added the initial default material as a builtin asset at
  `builtin://materials/default.vmat.json`. New projects should not copy this
  default material unless the user forks it into project content.
- Added shared material asset data types in
  `vultra/function/material/material_asset.hpp`.
- Added `MaterialSlotOverride::material` while preserving legacy
  `materialGraph` scene loading.
- Made `.vmat.json` visible/editable in Content Browser and selectable in Mesh
  material overrides.
- Added a narrow builtin-PBR `.vmat.json` render resolver that packs
  baseColor/metallic/roughness/alphaCutoff/doubleSided into the existing GPU
  material path.
- Builtin primitive meshes with no explicit material override now use
  `builtin://materials/default.vmat.json` as a slot 0 override, while preserving
  `MeshComponent::materialColor` as the instance base-color override.
- Explicit asset reimport now emits imported mesh material assets under
  `res://materials/imported/<mesh>/<slot>_<material>.vmat.json`.
- Mesh GPU material creation now prefers a generated builtin-PBR `.vmat.json`
  when present and falls back to the existing embedded `VMaterial` path for old
  imported output.
- Added a shared material source schema query path. Builtin PBR exposes its
  editable parameter schema now; shader reflection and material graph blackboard
  schemas are the intended later providers.
- Source Asset Inspector can edit project `.vmat.json` source, source ref, and
  builtin PBR properties through the resolved schema and save back to JSON.
- `.vmatgraph.json` now has optional blackboard load/save data for exposed
  parameters, and Material Graph Window has a minimal blackboard editor.
- Graph-backed `.vmat.json` assets now read the graph blackboard and display
  those exposed parameters in the Source Asset Inspector through the same schema
  path as builtin materials.
- Graph-backed `.vmat.json` mesh slot overrides now route through the material
  graph GPU material path. The current CPU-side graph surface evaluation uses
  `.vmat.json.properties` as blackboard overrides for matching param nodes and
  unlinked surface input fallbacks.
- Clarified that `.vimport` is source/import sidecar metadata, while runtime
  material parameter values live in `.vmat.json`.
- Added first-version per-slot MaterialPropertyBlock data on mesh material
  overrides, supporting float, color/vec4, and texture URI values.
- Scene serialization preserves the old compact `slot=uri` material override
  text when no property blocks exist, and switches to JSON only when block data
  is present.
- Inspector exposes a minimal Property Block editor under each Mesh material
  override, and render cooking applies the block on top of shared `.vmat.json`
  properties for builtin-PBR and graph material paths.
- Fixed graph material parameter refresh so updates to an existing graph-backed
  GPU material block upload the changed CPU mirror back to the GPU parameter
  buffer. This addresses the case where multiple materials share one material
  graph but later resolved material parameters appeared not to take effect.
- Fixed Runtime MCP/editor command mesh updates so `scene.update_component` can
  set `MeshComponent::materialOverrides`, including material asset URIs and
  first-version property block entries.
- Fixed render-time material asset creation to upload the GPU material table
  immediately when a dynamic `.vmat.json` material entry is created or moved to
  a new parameter-block offset.
- Fixed builtin primitive rendering so `MeshComponent::materialColor` only
  overrides material base color when the primitive has no explicit material slot
  override. Explicit `.vmat.json` slot overrides now remain authoritative.

## Verification

- `rg "vmat.json|vmatgraph|Shader-Bound Materials" ai doc source`
- `xmake build -y vultra-app`
- `xmake build -y vultra-app` after moving the default material to
  `builtin://materials/default.vmat.json` and wiring primitive fallback.
- `xmake build -y vultra-app` after wiring explicit reimport material emission
  and generated `.vmat.json` GPU packing.
- `xmake build -y vultra-app` after adding source-schema-driven `.vmat.json`
  Inspector editing for builtin PBR materials.
- `xmake build -y vultra-app` after adding material graph blackboard load/save,
  UI, and graph-backed material schema display.
- `xmake build -y vultra-app` after routing graph-backed `.vmat.json` assets
  into graph material rendering and applying property overrides.
- `xmake build -y vultra-app` after adding per-slot MaterialPropertyBlock data,
  scene serialization, Inspector UI, and render overrides.
- `xmake build -y vultra-app` final verification after the property block work.
- Runtime MCP smoke using the repository command:
  `xmake run vultra-app --editor --mcp --project example.vproject --no-xr`.
  `vultra.runtime.status` returned `ok: true` with Vulkan backend, editor mode,
  and project `example`.
- Runtime MCP asset read smoke:
  `builtin://materials/default.vmat.json`,
  `res://materials/default.vmatgraph.json`, and the generated DamagedHelmet
  `.vmat.json` all read successfully through `vultra.assets.read`.
- Runtime MCP import smoke:
  `vultra.assets.import` with
  `res://models/DamagedHelmet/DamagedHelmet.gltf` and `force=true` returned
  `ok: true` with no diagnostics, and generated
  `resources/materials/imported/DamagedHelmet_0_node_damagedHelmet_-6514_mesh_0_mesh_helmet_LP_13930damagedHelmet/0_Material_MR.vmat.json`.
- Runtime MCP graph material smoke:
  launched with `xmake run vultra-app --editor --mcp --project example.vproject --no-xr`,
  wrote two temporary `.vmat.json` assets that both referenced
  `res://materials/default.vmatgraph.json` with different color properties,
  assigned them to two temporary cube entities, stepped/status-checked the
  runtime successfully, then closed the editor without saving the dirty scene
  and removed the temporary material assets.
- Runtime screenshot verification after the MCP/render fixes:
  captured `C:/tmp/vultra_builtin_material_smoke_after_override_fix.png` and
  visually confirmed two builtin-PBR `.vmat.json` slot overrides render as
  separate red and green cubes. Then switched the same two cubes to two
  `.vmat.json` assets that share one `res://materials/default.vmatgraph.json`
  source, captured `C:/tmp/vultra_graph_material_smoke_verified.png`, and
  visually confirmed they render as separate red and green graph-backed
  materials.

All checks passed.

## Remaining

- Multi-material import smoke should still be run on a smaller targeted source,
  or Sponza if we explicitly want the heavier coverage.
- Graph-backed `.vmat.json` properties affect the current graph surface eval
  path, but shader compiler/uniform codegen still needs a first-class
  blackboard implementation.
- Shader reflection still needs to feed the material source schema path.
- Single-shader source reflection, Material Graph custom nodes, and Lua API
  parity have not been fully implemented yet.
