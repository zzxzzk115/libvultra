# Codex-Debt Review — Round 3 (post-audit new code + deferred items)

Date: 2026-06-10
Status: Done (audit + same-session execution of all P0 and all P1 items; see
`codex-debt-cleanup-execution.md` Round 8)

## Scope

Rounds 1–2 audited the engine core, MCP tooling, editor UI, asset loaders, and render
passes. Since Round 2 landed, **new, never-audited code** was merged: the Unity-style
prefab system, the self-registering render-graph passes + `declarative_renderer.cpp`,
the multi-RT XR view-synthesis graphs, and the render-graph window polish. Round 3
audited that new code plus re-verified the Round 2 deferred items. Method: 3 Explore
sub-agents (editor / render / asset+scene+runtime) + line-level source verification of
every load-bearing claim before acting.

## Corrections to sub-agent findings (verified against source)

1. **`executeCommand` had 33 branches, not 84** (the agent counted wrong). Still a
   ~700-line if-chain — fixed this round (P0-2).
2. **The SnapshotHistory boilerplate existed in THREE windows**, not two: the agent
   claimed `render_graph_window` lacked it, but `GraphEditorState` carried the same
   `historyReady/historyPending/applyingHistory` + reset/record skeleton
   (`render_graph_window.cpp:3542-3563` pre-refactor). Fixed for all three (P1-1).
3. **"Reuse `packMaterialParamsPBRMR` directly for the FromAsset fallback" was a
   regression trap.** The FromAsset path intentionally omits the emissive
   black→white import fixup (documented at `asset_system.cpp:1117`). The dedup is
   real but had to be parameterized, not substituted (P1-3).
4. **`RenderWorldCooker::cook` and the `RenderSystem` member layout are healthy.**
   Two agents independently confirmed: cook() is 343 well-profiled lines; the 40+
   members are logically grouped. Round 1/2's C4 "god function/class" framing is
   softened: the actual split target is the ~2600 lines of material/shader-cook
   anonymous helpers in `render_system.cpp`, per `doc/architecture/render-system.md`.

## Findings and disposition

### P0 (executed this round — all landed, build + runtime verified)

- **P0-1 prefab field-diff lookup triplication** [`scene_system.cpp`] — `prefabOverriddenFields`
  / `revertPrefabField` / `applyPrefabField` repeated the instance-root walk + prefab-doc
  load + source-index build verbatim; const/mutable `collectPrefabSourceByUuid` pair.
  → `findPrefabInstanceRef` + `SceneSystem::resolvePrefabSource` + one NodeT template.
- **P0-2 `executeCommand` if-chain** [`editor_commands.cpp:2094-2951` pre-refactor] —
  the same dispatch disease the MCP layer already cured with a table (`runtime_mcp_tools.cpp:73`).
  → 30 `cmdXxx` member handlers (bodies unchanged) + one `{name → member-pointer}` table.
- **P0-3 `declarative_renderer.cpp` god-module** [4434 lines] — 30 inlined
  `XxxBuiltin : IBuiltinRenderGraphPass` adapters + factory + backbuffer helpers + Lua
  machinery + renderer in one TU; +100 lines per new pass; catalog un-greppable. The
  in-file comment claimed the adapters needed "this file's render helpers" — verified
  coupling was actually 5 helpers, 2 shared.
  → `builtin_passes_{scene,post_process,gpu_scene}.cpp` (verbatim moves) +
  `builtin_render_graph_pass_factory.cpp` (the one catalog) +
  `render_graph_backbuffer.{hpp,cpp}` (shared backbuffer-view cluster;
  `normalizeId` → public `normalizeRenderGraphId`).

### P1 (executed this round — all landed)

- **P1-1 SnapshotHistory boilerplate ×3** → `SnapshotHistoryHost<Derived>` CRTP in
  `graph_history.hpp`; editors provide `historySnapshot()`/`applyHistorySnapshot()`
  (+ render-graph's `onBeforeHistoryRecord` position-baking hook). The full
  `SnapshotGraphEditor<GraphT>` mega-base envisioned by Round 2 was NOT built —
  load/save/URI handling genuinely differs per window.
- **P1-2 RHI facade buffer-factory duplication** [`render_device_facade.cpp:711-996`
  pre-refactor] — 9 methods × (WebGPU dispatch + `#if VULTRA_ENABLE_VULKAN` + BDA/RT
  usage augmentation + vma plumbing). → one `makeBackendBuffer` helper; ~290 → ~130 lines.
  Note: per-backend usage masks intentionally differ (vk adds eTransferSrc on
  readback/storage) — kept as two parameters, not "unified".
- **P1-3 PBRMR packer** → `emissiveBlackToWhiteFixup` parameter (default true);
  FromAsset seeds through the packer with it disabled.
- **P1-4 Lua pipeline-asset loading** → `declarative_renderer_asset_loader.cpp` +
  internal `declarative_lua_utils.hpp`. **Boundary note:** `renderScriptState` and
  `parseScriptedPassTable` stayed in the runtime TU — they touch its anon-namespace
  scripted-pass state (`s_RenderScriptThreadId` thread-ownership assert,
  `registerScriptedPassLuaBindings`, `parseScriptedPassParams`). Moving them would
  have silently duplicated cross-TU mutable state.

`declarative_renderer.cpp`: **4434 → ~2290 lines** after P0-3 + P1-4.

### Assessed clean (no action)

- **Prefab system architecture**: editor-side `prefab_ops.cpp` is minimal and reuses
  `captureWorldAsScene`/`saveSceneSync`; engine-side prefab logic lives in
  SceneSystem; vscn reader/writer stay prefab-agnostic. Correct layering.
- **XR view-synthesis passes** (`geometry_warp_pass`, `pullpush_inpaint_pass`):
  focused, no cross-eye duplication, graceful mono fallback.
- **Render-graph window polish** (RT picker/declutter/filtering): reuses the shared
  `render_target_pool.hpp`, correct deferred-release lifecycle.
- **Self-registering pass pattern** (`IBuiltinRenderGraphPass::registerInto`): clean;
  the only gap was catalog discoverability, fixed by the factory TU.

## P2 — next sessions (each its own pass + editor visual verification)

1. **A2 inspector panel split**: `inspector_window.cpp` 6550 lines; method-level split
   exists (drawEntityInspector/drawAssetInspector/drawPrefabSection); next is class-level
   EntityInspectorPanel / AssetInspectorPanel / ModelPreviewWidget TUs.
2. **A3 render_graph_window split**: 5852 lines; runtime graph viewer vs graph editor
   in one class; split canvas/catalog/properties (or RuntimeGraphPanel vs GraphEditorPanel).
3. **C4 continuation**: extract the ~2600-line material/shader cook helper block from
   `render_system.cpp` per `doc/architecture/render-system.md` (cook() itself stays).
4. **B6 continuation**: `asset_system.cpp` (~2150) → material-upload / gpu-upload /
   registry-import services per `doc/architecture/asset-system.md`.
5. **Tests**: `asset_memory_estimate` has none → `tests/asset_memory_estimate/`;
   `tests/material_asset` lacks a builtin-PBR JSON fixture.
6. **`buildNodeFromWorldR` vs `buildInstanceNodeFromWorldR`** (~40% shared)
   [NEEDS RECHECK — not line-verified this round]: read the delta-emission boundary
   before deciding; not a mechanical merge.
7. **MCP FrameGraphTexture dump flakiness** (found during this round's verification):
   `vultra.runtime.dump_frame_textures` intermittently writes blank/garbage PNGs while
   the live viewport renders correctly (user-confirmed). Captures taken shortly after
   launch are unreliable even after the scene loads; the same call succeeded ~2 min in
   on one binary and kept returning white frames on another. Suspect capture-timing /
   readback in the frame-graph debug texture path (`render_system` capture +
   `command_buffer_profiler_bridge`). Needs a dedicated investigation — it is the main
   obstacle to automated pixel-compare verification.

## P3 — opportunistic

1. `trim_copy` duplicated (`vscn_reader.cpp` / `scene_system.cpp`) + `containsIgnoreCase`
   duplicated (`texture_selector` / `mesh_selector`) → shared util when next touching.
2. Parallelize `RenderWorldCooker::cook` via vtask (GitHub #8 closeout; a feature, not debt).
3. `startXxxCpuLoadAsync` thin wrappers table-driven — only if a new asset type lands.
4. A5 selector-popup template — judged "not a clean template" twice; stays not-done.

## Explicitly not doing (consistent across two reviews)

C1 mesh-pass binding helper (structurally different descriptor sets per pass);
C2 material-model accessors (low value, not clean); A5 selector template;
re-litigating RenderSystem/cook() as god designs (see correction #4).
