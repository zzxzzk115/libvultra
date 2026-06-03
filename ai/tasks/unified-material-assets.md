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

2. Material asset schema and loader. Status: mostly complete.
   - Add runtime/editor material asset data structures.
   - Recognize `.vmat.json` as text material source data.
   - Fully support `source.kind = "builtin"` first; parse `graph` and single
     `shader` sources safely for later phases.
   - Verify parser errors and `xmake build -y vultra-app`.

3. Default material asset. Status: mostly complete.
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

4. Import pipeline emits material assets. Status: partial.
   - Explicit reimport generates `.vmat.json` for imported mesh materials under
     `res://materials/imported/<mesh>/<slot>_<material>.vmat.json`.
   - Generated material assets use builtin PBR source and map imported
     baseColor/metallic/roughness/alpha/texture data into properties.
   - Mesh GPU upload prefers a generated `.vmat.json` when present, then falls
     back to the embedded imported material path.
   - Do not migrate old imported output; delete and reimport when refresh is
     needed.
   - Verify single-material, multi-material, reimport, and old import behavior.

5. Mesh slot material references. Status: partial.
   - Add unified `material` URI while keeping legacy `materialGraph`.
   - Prefer `.vmat.json` in Inspector selection.
   - Verify scene save/load, legacy graph overrides, render smoke, and build.

6. Builtin material editing and fork. Status: partial.
   - Resolve material UI parameters through a source schema query layer.
   - Edit builtin material properties on `.vmat.json` project assets.
   - Add Content Browser fork/create flow.
   - Pack properties into the existing GPU material parameter pool.
   - Verify property persistence, visual change, texture readiness, and build.

7. MaterialPropertyBlock. Status: partial.
   - Add per-slot property overrides for runtime/entity-specific edits.
   - Support float, color/vec4, and texture URI first.
   - Preserve old `materialOverrides` scene text format when no property block
     is present; save JSON form only when block data exists.
   - Apply blocks to builtin-PBR and graph material render paths without
     mutating shared `.vmat.json`.
   - Verify two-instance isolation, scene/runtime persistence policy, and build.

8. Material Graph blackboard. Status: partial.
   - Add exposed graph parameters to `.vmatgraph.json` load/save.
   - Add Material Graph Window UI for editing the blackboard.
   - Feed graph blackboard entries into `.vmat.json` Inspector property UI when
     a material uses `source.kind = "graph"`.
   - Wire current CPU-side graph surface evaluation to consume `.vmat.json`
     property values as blackboard overrides.
   - Verify old graphs still open/compile and thumbnails do not thrash reloads.

9. Material Graph custom nodes. Status: mostly complete.
   - Add `.vmatnode.json` as the reusable user-authored material graph node
     descriptor format.
   - Let nodes expose typed input/output pins and default params.
   - Discover project `.vmatnode.json` assets in Material Graph Window and
     register them into the node menu.
   - Back first-version node implementations with GLSL output expressions using
     `{{input:name}}` and `{{param:name}}` placeholders.
   - Verify descriptor parsing, custom node registration, graph save/load,
     schema propagation, snippet compilation, and later a render smoke through
     the project graph shader path.

10. Single shader material source. Status: remaining.
    - Keep `source.kind = "shader"` as a conventional single shader material
      path using `shaderLibrary` plus one shader `id`.
    - Derive editable parameters from shader reflection when available.
    - Keep graph-like composition inside Material Graph custom nodes.
    - Verify parser, shader diagnostics, Inspector display, and build.

11. Lua/API parity and cleanup. Status: partial.
    - Expose thin mesh material/property APIs to Lua through C++ services.
    - Update `doc/lua_scripting.md` and `ai/knowledge/lua-scripting.md`.
    - Verify valid and invalid Lua calls, docs, new project smoke, and build.

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

## Remaining Work Queue

1. Close Phase 4 import coverage.
   - Re-run single-material, multi-material, delete-and-reimport, and old
     imported asset smoke tests after the generated `.vmat.json` path settles.
   - Confirm imported mesh slots prefer generated material asset URIs and still
     fall back to embedded imported material data for old output.

2. Close Phase 5 mesh slot references.
   - Verify scene serialization for the new `material` URI field and legacy
     `materialGraph` scenes.
   - Verify Inspector slot selection persists `.vmat.json` after scene reload.
   - Use Runtime MCP plus screenshot/frame dump for default material, material
     asset override, and legacy graph override render smoke.

3. Close Phase 6 builtin material editing/fork.
   - Finish the source-schema based material property UI path.
   - Make builtin material fork/create flows produce project `.vmat.json` assets
     without editing builtin definitions.
   - Verify baseColor/roughness/texture edits persist and affect rendering.

4. Close Phase 7 MaterialPropertyBlock.
   - Finish per-slot float, color/vec4, and texture URI override UI/API
     behavior.
   - Verify two entities sharing one `.vmat.json` can diverge through property
     blocks without mutating the shared asset.
   - Decide and document which block values are authoring data versus runtime
     only data.

5. Close Phase 8 Material Graph blackboard.
   - Finish graph blackboard schema flow from `.vmatgraph.json` to graph-backed
     `.vmat.json` Inspector properties.
   - Verify blackboard overrides feed the current graph surface evaluation.
   - Re-test old graph load/compile compatibility.

6. Close Phase 9 custom nodes.
   - Keep `.vmatnode.json` as the project-defined reusable node format.
   - Verify a live editor graph can add two or more project-defined nodes from
     the node menu, save, reload, compile, and render through a graph-backed
     material.
   - Keep `typeId`/`nodeId` canonical; newly written files must not introduce
     old `type`/`node` graph-node fields.

7. Implement Phase 10 single shader source.
   - Wire `source.kind = "shader"` as one conventional shader material source,
     not multiple pass groups.
   - Derive editable material properties from shader reflection where available.
   - Add parser, diagnostics, Inspector, and render smoke tests.

8. Close Phase 11 Lua/API parity and cleanup.
   - Run direct Lua script smoke tests for material assignment and property
     blocks.
   - Finish `doc/lua_scripting.md` and `ai/knowledge/lua-scripting.md` sync.
   - Run final new-project smoke with builtin default, project `.vmat.json`,
     graph-backed `.vmat.json`, and single-shader material once implemented.

## Defaults

- Material asset extension: `.vmat.json`.
- Material Graph extension remains `.vmatgraph.json`.
- Material Graph custom node descriptor extension is `.vmatnode.json`.
- `.vmat.json` can reference `builtin`, `graph`, or single `shader` sources.
- Graph-like composition belongs in Material Graph.
- No `.vmatinst` in version 1.
- No imported asset migration; delete imported output and reimport.
- Each phase should be independently verified before moving on.
