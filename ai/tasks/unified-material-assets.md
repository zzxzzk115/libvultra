# Unified Material Asset Task

Spec: `ai/specs/unified-material-assets.md`

## Current Goal

Implement material assets in steady phases so mesh slots can reference
forkable/editable `.vmat.json` assets while preserving existing material graph
and imported mesh behavior.

## Phase Plan

1. Spec and baseline. Status: complete.
   - Record `.vmat.json`, material sources, graph blackboard, property
     precedence, and `.vimport` boundaries in AI docs.
   - Verify terminology with `rg "vmat.json|vmatgraph|MaterialPropertyBlock" ai doc source`.

2. Material asset schema and loader. Status: complete.
   - Add runtime/editor material asset data structures.
   - Recognize `.vmat.json` as text material source data.
   - Support `source.kind = "builtin"`, `graph`, and single `shader` sources.
   - Verify parser errors and `xmake build -y vultra-app`.

3. Default material asset. Status: complete.
   - Store engine defaults under `builtin://materials/`, starting with
     `builtin://materials/default.vmat.json`.
   - Do not copy the default material into new projects; users fork builtin
     materials into `res://materials/` when they need editable project assets.
   - Show project `.vmat.json` assets in Content Browser and builtin defaults in
     Inspector material selectors.
   - Use the builtin default material for builtin primitive meshes that have no
     explicit slot override.
   - Verify builtin text loading, new project creation, old project fallback,
     and build.

4. Import pipeline emits material assets. Status: complete.
   - Explicit reimport generates `.vmat.json` for imported mesh materials under
     `res://materials/imported/<mesh>/<slot>_<material>.vmat.json`.
   - Generated material assets use builtin PBR source and map imported
     baseColor/metallic/roughness/alpha/texture data into properties.
   - Mesh GPU upload prefers a generated `.vmat.json` when present, then falls
     back to the embedded imported material path.
   - Do not migrate old imported output; delete and reimport when refresh is
     needed.
   - Verified single-material, multi-material, reimport, and old import
     behavior through Runtime MCP import smokes.

5. Mesh slot material references. Status: complete.
   - Add unified `material` URI while keeping legacy `materialGraph`.
   - Prefer `.vmat.json` in Inspector selection.
   - Verified scene save/load, legacy graph overrides, render smoke, and build.

6. Builtin material editing and fork. Status: complete.
   - Resolve material UI parameters through a source schema query layer.
   - Edit builtin material properties on `.vmat.json` project assets.
   - Add Content Browser fork/create flow.
   - Pack properties into the existing GPU material parameter pool.
   - Verified property persistence, visual change, texture readiness, and build.

7. MaterialPropertyBlock. Status: complete.
   - Add per-slot property overrides for runtime/entity-specific edits.
   - Support float, color/vec4, and texture URI first.
   - Preserve old `materialOverrides` scene text format when no property block
     is present; save JSON form only when block data exists.
   - Apply blocks to builtin-PBR and graph material render paths without
     mutating shared `.vmat.json`.
   - Verified two-instance isolation, scene/runtime persistence policy, and
     build.

8. Material Graph blackboard. Status: complete.
   - Add exposed graph parameters to `.vmatgraph.json` load/save.
   - Add Material Graph Window UI for editing the blackboard.
   - Feed graph blackboard entries into `.vmat.json` Inspector property UI when
     a material uses `source.kind = "graph"`.
   - Wire current CPU-side graph surface evaluation to consume `.vmat.json`
     property values as blackboard overrides.
   - Verified old graphs still open/compile and thumbnails do not thrash reloads.

9. Material Graph custom nodes. Status: complete.
   - Add `.vmatnode.json` as the reusable user-authored material graph node
     descriptor format.
   - Let nodes expose typed input/output pins and default params.
   - Discover project `.vmatnode.json` assets in Material Graph Window and
     register them into the node menu.
   - Back first-version node implementations with GLSL output expressions using
     `{{input:name}}` and `{{param:name}}` placeholders.
   - Verified descriptor parsing, custom node registration, graph save/load,
     schema propagation, snippet compilation, and a render smoke through a
     graph-backed `.vmat.json` material.

10. Single shader material source. Status: complete.
    - Keep `source.kind = "shader"` as a single mesh material shader path using
      `shaderLibrary` plus one shader `id`.
    - Derive editable parameters from shader reflection when available.
    - Keep graph-like composition inside Material Graph custom nodes.
    - Render through the DirectGBuffer mesh material ABI, with
      `VultraMaterialEval` consumed by the existing deferred lighting path.
    - Verified parser, shader diagnostics, Inspector display, ABI shader
      compilation, Runtime MCP render smoke, and build.

11. Lua/API parity and cleanup. Status: complete.
    - Expose thin mesh material/property APIs to Lua through C++ services.
    - Update `doc/lua_scripting.md` and `ai/knowledge/lua-scripting.md`.
    - Verified Lua material assignment/property calls, docs, Runtime MCP script
      smoke, and build.

## Progress Notes

- 2026-06-02: Added Runtime MCP component metadata/readback tools so automation
  can inspect component fields before calling `scene.update_component`.
- 2026-06-02: Added first Lua `entity.mesh` material APIs for `.vmat.json` slot
  assignment and per-slot MaterialPropertyBlock overrides. Build and MCP
  material override readback passed; direct Lua snippet execution remains a
  follow-up harness test.
- 2026-06-03: Material source direction is now builtin, graph, or single shader.
  Extensibility moves to reusable Material Graph custom nodes.
- 2026-06-03: `.vmat.json` parsing now goes through a shared strict parser for
  runtime/editor paths. Inspector shows parser diagnostics, runtime loaders log
  invalid material assets and fall back safely, and Content Browser still only
  exposes the fully supported Builtin PBR material template.
- 2026-06-03: Added `test-material-asset` to cover valid builtin/graph/single
  shader source parsing plus invalid source/version/property diagnostics.
- 2026-06-03: Fixed graph-backed material instance identity: GPU material cache
  keys now use the material instance key while `MaterialGraphSurfaceParams`
  keeps the source graph id. `.vmatgraph.json` now uses `typeId` and `nodeId`
  as the canonical node type and link endpoint fields.
- 2026-06-03: Added `.vmatnode.json` v1 descriptor parsing, Content Browser
  creation, source text recognition, and Material Graph Window discovery for
  reusable custom node schemas.
- 2026-06-03: Added GLSL expression implementation support for custom material
  graph nodes. Output expressions use `{{input:name}}` and `{{param:name}}`
  placeholders and are expanded by the Material Graph compiler.
- 2026-06-03: Canonical `.vmatgraph.json` authoring now uses `typeId` for
  graph node type identifiers and `nodeId` for link endpoints. Runtime readers
  can still open old `type`/`node` graph files, but writers, templates, tests,
  and docs should use the canonical names directly instead of adding alias
  patches.
- 2026-06-03: Single shader material sources now have a Content Browser
  `.vmat.json` creation template, Inspector source selection/display,
  `vshadersystem::MaterialDescription` schema conversion for params/textures,
  and a runtime render path that maps reflected conventional surface
  properties into the existing GPU PBR material parameter path.
- 2026-06-03: `test-material-asset` now compiles a real inline `.vshader`
  through `vshadersystem::build_single_shader()` and verifies schema conversion
  from the resulting `MaterialDescription`, including generated `Texture2D`
  index params mapped back to user-facing texture URI properties.
- 2026-06-03: Arbitrary mesh material fragment shaders now execute through the
  highend DirectGBuffer ABI path. Material params use the stable SSBO
  descriptor baseline instead of defaulting to BDA, WebGPU profile shader
  compilation is covered by `test-material-asset`, and shaders without
  `[properties]` can compile and render from engine constants without injected
  material access.
- 2026-06-03: Shader-backed `.vmat.json` diagnostics now warn once for missing
  shader variants, unknown properties, non-string texture properties, and
  unsupported reflected property types.
- 2026-06-03: Mesh Inspector MaterialPropertyBlock authoring can add supported
  float, color/vec4, and texture2D overrides directly from the referenced
  `.vmat.json` source schema or legacy graph blackboard schema, while retaining
  manual property entry as a fallback.
- 2026-06-03: MaterialPropertyBlock persistence policy is documented:
  Inspector-authored blocks are scene authoring data, while Lua/runtime writes
  affect current entity state and do not mutate shared `.vmat.json` assets.
- 2026-06-03: Direct Runtime MCP Lua script smoke verified `entity.mesh`
  material APIs during playback. The script assigned a shader-backed
  `.vmat.json`, wrote float/color/texture property block values, cleared a
  temporary property, cleared an empty slot, and read back the expected
  MeshComponent state without mutating the shared material asset.
- 2026-06-03: `test-material-asset` now covers canonical `.vmatgraph.json`
  blackboard load/save plus conversion to `.vmat.json` material source schema,
  including color, float UI range, and texture2D defaults.
- 2026-06-03: Runtime MCP custom-node smoke authored two project
  `.vmatnode.json` descriptors, confirmed `vultra.material_graph.list_nodes`
  discovers both, compiled a graph using both project nodes, applied a
  graph-backed `.vmat.json` material to a cube, captured
  `.vultra/mcp/render_rgb_1780511806093.png`, and saved
  `res://scenes/mcp_custom_node_render_smoke.vscn`.
- 2026-06-03: Runtime MCP builtin material edit smoke authored project material
  `res://materials/mcp_edited_builtin_pbr.vmat.json` with builtin PBR source,
  applied it to a cube, read back the unified `material` override field,
  captured `.vultra/mcp/render_rgb_1780512088131.png`, and saved
  `res://scenes/mcp_builtin_material_edit_smoke.vscn`.

## Verified Closures

1. Phase 4 import coverage. Status: verified.
   - Runtime MCP AssetSystem reimport smoke for DamagedHelmet verified
     single-material generated `.vmat.json` output:
     `res://materials/imported/DamagedHelmet_0_node_damagedHelmet_-6514_mesh_0_mesh_helmet_LP_13930damagedHelmet/0_Material_MR.vmat.json`.
     The generated material uses builtin PBR source and maps base color,
     roughness/metallic, alpha, double-sided, and texture URIs into properties.
   - Runtime MCP AssetSystem reimport smoke for CornellBox verified multi-mesh
     and multi-material generated `.vmat.json` output. The import produced
     separate builtin PBR material assets for floor, ceiling, back/right/left
     walls, boxes, and light; asset service readback confirmed distinct
     properties such as green right wall and red left wall base colors.
   - Delete-and-reimport smoke verified that deleting generated
     `0_leftWall.vmat.json` and reimporting CornellBox recreates the material
     asset with the expected builtin PBR properties.
   - Old imported output fallback smoke deleted the generated DamagedHelmet
     material asset, instantiated the imported mesh by UUID, stepped runtime,
     and captured `.vultra/mcp/render_rgb_1780509661057.png`; the mesh rendered
     using the embedded imported material data without requiring generated
     `.vmat.json` output.
   - Imported mesh slots prefer generated material asset URIs when the
     `.vmat.json` exists and fall back to embedded imported material data when
     it does not.

2. Phase 5 mesh slot references. Status: verified.
   - Runtime MCP verified scene serialization/readback for the new `material`
     URI field using `material_shader_smoke.vscn`: cube and sphere mesh slots
     load `res://materials/mcp_shader_material.vmat.json`, with and without
     property blocks.
   - Runtime MCP verified legacy `materialGraph` string compatibility with a
     minimal `legacy_material_graph_override.vscn`; the old
     `MeshComponent/materialOverrides = "0=res://...vmatgraph.json"` form loads
     into the `materialGraph` field and leaves `material` empty.
   - Runtime MCP screenshot `.vultra/mcp/render_rgb_1780507858309.png` covers
     the material asset override render smoke through DirectGBuffer/deferred
     lighting.

3. Phase 6 builtin material editing/fork. Status: verified.
   - Content Browser has a `Material/Builtin PBR` project `.vmat.json` creator
     and Inspector material editing resolves fields from the source schema.
   - Runtime MCP authored project builtin PBR material
     `res://materials/mcp_edited_builtin_pbr.vmat.json`, applied it to a cube,
     read back the unified `material` override, captured
     `.vultra/mcp/render_rgb_1780512088131.png`, and saved
     `res://scenes/mcp_builtin_material_edit_smoke.vscn`.

4. Phase 7 MaterialPropertyBlock. Status: verified.
   - Runtime MCP new-project/new-scene smoke verifies material overrides and
     property block persistence on graph-backed and shader-backed `.vmat.json`
     mesh slots.
   - Shared-material/two-entity divergence is verified in
     `build/.tmp/material-shader-mcp-project/resources/scenes/material_shader_smoke.vscn`:
     cube and sphere share `res://materials/mcp_shader_material.vmat.json`,
     while the sphere stores slot property overrides in scene data. The shared
     `.vmat.json` remains unchanged, and MCP screenshot
     `.vultra/mcp/render_rgb_1780507858309.png` shows divergent rendering.

5. Phase 8 Material Graph blackboard. Status: verified.
   - Graph blackboard schema flow from `.vmatgraph.json` to graph-backed
     `.vmat.json` Inspector properties is implemented.
   - Runtime graph surface evaluation consumes `.vmat.json` properties and
     per-slot property blocks as blackboard/param overrides.
   - `test-material-asset` covers canonical blackboard load/save and schema
     conversion for color, float UI range, and texture2D defaults.
   - Runtime MCP authored a new material graph plus graph-backed `.vmat.json`,
     set parameters/property block values, applied it to a cube, saved the
     scene, and captured editor/render PNG evidence.

6. Phase 9 custom nodes. Status: verified.
   - `.vmatnode.json` is the project-defined reusable node format.
   - `test-material-graph` covers descriptor parsing, project custom node
     registration, two-node snippet compilation, and canonical `typeId` /
     `nodeId` graph output.
   - Runtime MCP verified two project-defined nodes can be discovered, compiled
     into one graph, saved, and rendered through a graph-backed material.

7. Phase 10 single shader source. Status: verified.
   - Runtime MCP authored a real `.vshader`, shader library manifest, and
     shader-backed `.vmat.json`, registered them through vasset, set reflected
     parameters/property block values, applied the material to a sphere, saved
     the scene, and captured PNG evidence.
   - Runtime rendering now executes arbitrary mesh material fragment shaders
     through the DirectGBuffer ABI path, with shader parameters supplied through
     a stable SSBO material block and engine constants available directly from
     the ABI include.
   - Documentation has been updated to describe the ABI path instead of the
     historical reflected surface adapter.

8. Phase 11 Lua/API parity and cleanup. Status: verified.
   - Direct Lua script smoke verified material assignment and property block
     writes through playback.
   - `doc/lua_scripting.md` and `ai/knowledge/lua-scripting.md` document the
     runtime-write/non-mutating shared asset policy.
   - Runtime MCP material smokes now cover builtin/imported material assets,
     graph-backed `.vmat.json`, shader-backed `.vmat.json`, slot property
     blocks, and Lua material/property APIs.

## Remaining Work Queue

- No open Phase 1 material asset tasks.
- Shader-driven render graph pass replacement is explicitly deferred.
- Broader backend smoke coverage is explicitly deferred.
- Commit/push/release is the final step after the remaining authoring UX polish.
- Current authoring UX follow-up:
  - Mesh Inspector can fork the builtin default material into an editable
    project `.vmat.json`.
  - Material Graph add-node popup supports search across display names,
    `typeId`, categories, and subcategories.

## Defaults

- Material asset extension: `.vmat.json`.
- Material Graph extension remains `.vmatgraph.json`.
- Material Graph custom node descriptor extension is `.vmatnode.json`.
- `.vmat.json` can reference `builtin`, `graph`, or single `shader` sources.
- Graph-like composition belongs in Material Graph.
- No `.vmatinst` in version 1.
- No imported asset migration; delete imported output and reimport.
- Each phase should be independently verified before moving on.
