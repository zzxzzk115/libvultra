# Codex-Debt Review — Round 2 (Duplication / Abstraction / Interface)

Date: 2026-06-09
Status: Done (written audit only — no code changed this round)
Scope: A second strict engineering audit, focused on the three areas Round 1
(`codex-debt-review-2026-06-03.md`) missed or under-explored: the **editor UI layer**, the
**asset loaders**, and the **render passes**. Hunting specifically for: high duplication,
missing abstraction / OOP, and poor interface design. This round records the critique and a
remediation roadmap for the later "big cleanup" to execute — no code is changed here.

## Summary

Round 1 concentrated on the engine core (`render_system.cpp` / `asset_system.cpp`) and the
AI/MCP tooling, and **never looked at the editor layer**, nor did it sweep per-pass /
per-loader for duplication. Reading the source file-by-file this round, the root cause is
identical to Round 1's diagnosis — **the absence of convergence mechanisms (no shared base
class / template / dispatch table to carry a repeated lifecycle)** — but its severity was
understated. The most glaring facts:

- The **two largest files in the whole codebase live in the editor layer**, bigger than the
  engine god-files: `inspector_window.cpp` **7276 lines** and `render_graph_window.cpp`
  **6246 lines** (vs `render_system.cpp` 5377, `asset_system.cpp` 2852). Round 1 never
  mentioned them.
- The same `RenderTargetSlot` struct + its retire/release logic is copy-pasted across **5**
  windows.
- Three asset CPU async loaders (`startMeshCpuLoadAsync` / `Texture` / `GaussianSplat`) are
  near-byte-identical.

Findings are tagged **[verified]** (read directly from source this round) or
**[needs recheck]** (relayed by exploration sub-agents; confirm against source before acting).

### Honesty correction (do not act on this as a bug)
- A sub-agent reported `asset_system.cpp:1078` (`startMeshCpuLoadAsync` calling
  `readTextureAssetBytes(uri)`) as a "critical bug — reads texture bytes to parse a mesh".
  **Verified: it is NOT a live bug for normal meshes.** For a non-builtin URI,
  `readTextureAssetBytes` is just `return m_VFS.readAll(uri)` (`asset_system.cpp:1298`) — a
  raw byte read, no decode, byte-identical to `loadMeshSync`'s read path. The real defect is a
  **naming / abstraction smell** (see P1-B5): a function named "read texture bytes" reused as a
  generic byte reader, carrying an `isBuiltinTextureUri` fallback branch that is only correct
  for textures — it works only because mesh URIs never hit the builtin-texture branch. Logged
  as poor interface design, not fixed as a bug.

---

## A. Editor UI layer — the biggest new find (entirely missed by Round 1)

### P0-A1 — `RenderTargetSlot` struct + release logic copy-pasted across 5 windows [verified]
The same struct
```cpp
struct RenderTargetSlot {
    std::optional<vultra::rhi::Texture> texture;
    vultra::rhi::Extent2D               extent {};
    vultra::IImGuiService::TextureID    textureId {};
    uint64_t                            releaseFrame {0};
};
```
is defined in **5** headers: `inspector_window.hpp:103`, `material_graph_window.hpp:41`,
`render_graph_window.hpp:35`, `scene_view_window.hpp`, `game_view_window.hpp`. Each .cpp then
hand-writes its own "collect retired targets -> deferred-frame release of textureId" reclaim
loop (e.g. `inspector_window.cpp:6965`, `material_graph_window.cpp:2142`,
`render_graph_window.cpp:6123`).
**Remediation:** extract `common/render_target_pool.{hpp,cpp}` — one `RenderTargetPool`
owning slot reuse, deferred release, and GC. The 5 windows compose it (as a member); delete
the 5 structs + 5 reclaim loops.

### P0-A2 — `inspector_window.cpp` 7276-line god-file (largest in the repo) [verified]
One file crams: entity Inspector, asset Inspector, texture/mesh import dialogs, model preview,
reflection UI. The worst offender `drawMetaValue()` (~`3812-4011`) is a 200-line
`if (strcmp(fieldName,"X")==0){...}` chain — every new enum field requires another hand-patched
block, with hard-coded bounds (`std::min(*v, 3u)`) scattered throughout. `drawVec3Control` /
`drawVec2Control` (`1074-1185`) are near-identical apart from axis count.
**Remediation (phased split):**
- Enum fields: an `EnumFieldRegistry` (`{fieldName -> {labels, min, max}}` table) + a single
  dispatcher, replacing the 200-line chain.
- Vector controls: extract `ui::drawVectorControl<N>()` into the shared widgets header, reused
  by the inspector and any future panel.
- Big split: `EntityInspectorPanel` / `AssetInspectorPanel` / `ModelPreviewWidget` as separate
  TUs, with `InspectorWindow` reduced to an orchestrator.

### P1-A3 — `render_graph_window.cpp` 6246-line god-file [verified / needs recheck responsibilities]
Second-largest file. The exact responsibilities crammed in (node canvas / serialize / pass
catalog / thumbnail templates / property panel) need a per-section confirmation, but the size
already makes the point. **Remediation:** same as A2 — split by responsibility: canvas,
catalog, properties, thumbnails.

### P1-A4 — Graph-editor trio (material/animator/render) duplicate load/save/history skeleton [verified]
`MaterialGraphWindow::loadGraph` (`material_graph_window.cpp:1046`) and
`AnimatorGraphWindow::loadGraph` (`animator_graph_window.cpp:221`) are the same boilerplate: get
IAssetService -> `loadTextAssetSync` -> `loadGraphFromText` -> store URI -> dirty flag -> status
text -> `resetHistory()`. The `recordHistory()` / `resetHistory()` SnapshotHistory install
boilerplate (`m_HistoryReady` guard + `setRestore` + coalesced record) is duplicated verbatim
between the two windows, differing only by the serialization function.
**Remediation:** a template base class `SnapshotGraphEditor<GraphT>` carrying load/save/URI
tracking/status/history plumbing; subclasses override only type-specific logic (diagnostics
type, node-port init, selection reset). Fold render_graph in too.

### P1-A5 — Asset-selector popups duplicated (texture vs mesh) + state has no shared base [verified]
`drawTextureSelectorPopup` (`texture_selector.cpp:498`) and `drawMeshSelectorPopup`
(`mesh_selector.cpp:216`) are near-identical in structure: filter bar + icon-size slider +
column grid + double-click select + Clear button (~100 lines of copy-paste).
`TextureSelectorState` (`texture_selector.hpp:28`) and `MeshSelectorState`
(`mesh_selector.hpp:14`) share core fields (filter/iconSize/previewCache/generation tracking)
yet are unrelated structs.
**Remediation:** a `drawAssetSelectorPopup<T>()` template + an `AssetSelectorState` base; inject
per-asset-type callbacks for list/thumbnail/metadata.

---

## B. Asset system — loader duplication + dispatch should be table-driven

### P0-B1 — Three CPU async loaders are near-byte-identical [verified]
`startMeshCpuLoadAsync` (`asset_system.cpp:1063`), `startTextureCpuLoadAsync` (`1119`),
`startGaussianSplatCpuLoadAsync` (`1176`) are the same shape: resolve URI -> build
`CpuLoadTask`/`TaskSet` -> `for attempt` retry loop (read bytes -> parse -> store cpu -> set
eCPUReady/eUploadQueued -> `enqueueUploadOnce`) -> on failure set eFailed -> push to
`m_CpuLoadTasks`. Only the parse function and `UploadCmd::Kind` differ (~160 lines duplicated).
**Remediation:** extract
`template<typename TCpu> void startAssetCpuLoadTask(rec, uuid, ReadFn, ParseFn, UploadCmd::Kind)`,
encapsulating the retry + task creation + state machine; the three wrappers pass read/parse/kind.

### P0-B2 — sync/async load entries duplicated x3 each + 10 URI-forwarding stubs [verified / needs recheck line numbers]
`loadMeshSync/Async`, `loadTextureSync/Async`, `loadGaussianSplatSync/Async` (around
`asset_system.cpp:2216`, `2408`, `2524`, `2613`) share the same cache-lookup + state-check +
upload-enqueue skeleton. Plus ~10 `loadXxxSync(uri)/Async(uri)` one-line forwarders (resolve URI
-> call the UUID version), e.g. `loadTextureSync(uri)` (`2476`).
**Remediation:**
`template<typename TCpu,typename TGpu> AssetHandle<...> loadAssetSync/Async(uuid, AssetCache&, UploadCmd::Kind, StartFn)`;
collapse the URI overloads into one non-member template. The `AssetCache<TCpu,TGpu>` template
already exists, yet loaders bypass it by hand-writing per-type cache access — use it to converge.

### P1-B3 — `update()` upload queue: 3-way switch is an isomorphic state machine -> handler dispatch [verified / needs recheck line numbers]
`update()` (~`asset_system.cpp:841-1007`) has isomorphic eMesh/eTexture/eGaussianSplat cases over
`UploadCmd::Kind`: findCache -> check state -> call type-specific `uploadXxx` -> store gpuIndex ->
set eReady -> release the cpu copy per policy. Only the upload function and the `hasSkin` release
policy differ.
**Remediation:** define an `IAssetUploadHandler` (or `{Kind -> uploadFn}` table) and fold the
three cases into one dispatch.

### P1-B4 — Material-param packing duplicated in 3 places (pack / fromAsset / emit JSON) [verified / needs recheck line numbers]
`createAndAppendGpuMaterial` (~`1535`) and `createAndAppendGpuMaterialFromAsset` (~`1629`) each
pack the same `MaterialParamsPBRMR` fields per material model (PBRMR/PBRSG/Unlit/Phong/Toon);
`emitImportedMaterialAssets` (~`1767`) then reads the same fields in the same order to write JSON.
Any field change must be mirrored in 3 places.
**Remediation:** a "material params codec" — paired `packMaterialParamsXxx(core)` +
`serializeXxx(core, json)` reused by pack/fromAsset/emit; the JSON override becomes a separate
function applying deltas to a pre-packed block.

### P1-B5 — `readTextureAssetBytes` is misnamed + ridden by the mesh path [verified] (false alarm downgraded, see above)
Logged as poor interface design: the name claims texture-specific, but only the
`isBuiltinTextureUri` branch is texture-specific; the rest is a generic read.
**Remediation:** rename to a generic `readAssetBytes(uri)`; push builtin-texture resolution down
into an explicit branch or a separate `resolveBuiltinTextureBytes`. The mesh/splat paths switch
to the generic name, removing the "correct only because URIs never collide" fragile coupling.

### P2-B6 — `asset_system.cpp` 2852-line god-file; split by service [needs recheck]
Crams loader / uploader / material conversion / texture resolution / UUID<->URI mapping / builtin
special cases / cache management. Round 1 already started the split by extracting
`material_params.hpp` / `mesh_vertex_packing` / `asset_memory_estimate`.
**Remediation (continue):** `AssetLoaderService` / `AssetUploadService` / `MaterialConverter` /
`AssetUriResolver` / `BuiltinAssetLoader`, with `AssetSystem` reduced to an orchestrator. Aligns
with the existing split plan in `doc/architecture/asset-system.md`.

---

## C. Render passes — missing mesh-pass base / binding helpers

### P1-C1 — Mesh render passes duplicate the "standard buffer binding + descriptor set" skeleton [verified / needs recheck line numbers]
`depth_pre_pass` / `visibility_buffer_pass` / `thin_gbuffer_pass` / `direct_gbuffer_pass` each
hard-code the same set: read cameraBlock(set0,b0) + conditionally read
drawBuffer/indirect/drawSet/meshlets, plus
`rc.resourceSet[0] = {{0,UBO},{1,SSBO},{4,..meshlets},{8,materialTable},{9,materialParams},{10,..},{11,..},{46,skin}}`.
Slots 0/1/4/8/9/10/11/46 are re-copied in every mesh pass.
**Remediation:** extract `bindStandardMeshRenderingBuffers(builder, pd, ...)` +
`bindMeshRenderingDescriptors(rc, pipeline, gpuSceneDatabase, cameraUbo, ...)`; or a
`MeshRenderingPass` base class carrying that lifecycle.

### P1-C2 — Five-way material-model switch + `loadMaterialParams<T>` duplicated across passes [verified / needs recheck line numbers]
`direct_gbuffer_pass.cpp`'s `makeDrawParams` and `compatibility_basecolor_pass.cpp`'s
`resolveMaterialTextureIndex/BaseColorFactor` each re-implement the PBRMR/PBRSG/Unlit/Phong/Toon
switch + field mapping.
**Remediation:** into a shared `material/` header: `getMaterialParams<T>(pool, idx)` and
per-model baseColor/tex accessors, reused by passes.

### P1-C3 — GBuffer multi-attachment create/write and meshlet push-constants duplicated [verified / needs recheck line numbers]
`direct_gbuffer_pass` and `thin_gbuffer_pass` each hard-code color-slot 0..4 + clear-value
create-write pairs; meshlet push-constant structs like `ThinGBufferPushConstants` are redefined
per pass.
**Remediation:** a `GBufferAttachmentBuilder` helper + move `MeshletPushConstants` to a shared
header.

### P2-C4 — `render_system.cpp` 5377 lines + `RenderSystem` god-class + giant `RenderWorldCooker::cook` [needs recheck]
`RenderWorldCooker::cook` (~`2200+`) entangles material packing / material-graph evaluation /
shader variants / mesh-instance transform+override / GPU scene build / splat preprocessing / UI
flattening in one function. `RenderSystem` (header ~218 lines, 40+ private members) mixes renderer
registration / world cooking / frame resources / framegraph snapshot debug / splat params /
builtin settings / debug draw / particle manager / XR view.
**Remediation:** `MaterialCooker` / `MeshSceneCooker` / `ParticleCooker` to split cook;
`RenderSystemDebugger` / `RenderSettingsManager` / `RenderWorldManager` to split RenderSystem,
the latter becoming a facade. Aligns with `doc/architecture/render-system.md` (Round 1 P2-11
continuation).

### P2-C5 — Features still use raw `new`/`delete` (pass lifetime not unified) [needs recheck]
E.g. `direct_gbuffer_feature.cpp` uses `new DirectGBufferPass()` + `delete` in the dtor (Round 1
P1-10 swept a few sites; this is a leftover). **Remediation:** unify on `unique_ptr` + a
`FeaturePass` interface; a `.clang-tidy` owning-memory check to prevent regression.

---

## Remediation roadmap (tiered, for the "big cleanup" to execute in phases)

| Tier | Item | Core value | Representative files |
|---|---|---|---|
| **P0** | A1 RenderTargetPool converging 5 copies | delete 5 structs + 5 reclaim loops | render_target_pool (new) + 5 windows |
| **P0** | A2 split inspector god-file + extract enum/vector widgets | split the repo's largest file | inspector_window.cpp |
| **P0** | B1 CPU async-load template converging 3 copies | delete ~160 lines | asset_system.cpp |
| **P0** | B2 sync/async loader template + URI-forward template | converge onto AssetCache template | asset_system.cpp |
| **P1** | A3/A4/A5 split render_graph + SnapshotGraphEditor base + selector template | converge graph editors / selectors | *_graph_window / *_selector |
| **P1** | B3/B4/B5 upload-handler dispatch + material codec + readAssetBytes rename | table-driven asset dispatch | asset_system.cpp |
| **P1** | C1/C2/C3 mesh-pass binding helpers + material-model accessors + GBuffer builder | pass de-duplication | rendering/srp/builtin/passes |
| **P2** | B6/C4/C5 split god-files by service + split cook/RenderSystem + finish raw-new | long-term maintainability | render_system / asset_system / features |

**Cross-cutting root cause (same diagnosis as Round 1, now proven in 3 more subsystems):**
everywhere "the same lifecycle is hand-written repeatedly" lacks a shared base class / template /
dispatch table to carry it. The fix pattern is constant — find the repeated lifecycle, extract one
convergence point, and reduce call sites to injecting only the differences.

## Result

Written audit only; no code changed. The roadmap above extends the Round 1 P-tier roadmap in
`codex-debt-review-2026-06-03.md`. Execution progress (when the big cleanup runs) should continue
in `codex-debt-cleanup-execution.md`.

## Next steps

- Execute the P0 items first (A1, A2, B1, B2) — highest maintainability payoff, lowest risk.
- For each landed item, verify with `xmake build -y vultra-app` (and `xmake build -y vultra`
  warning-free); editor changes checked via MCP `reload`/screenshot for unchanged inspector /
  graph-editor / selector behavior; asset-loader changes run existing tests
  (`xmake build -y test-mesh-vertex-packing`) plus minimal new RHI/asset tests (P2-13 continuation).

## Blockers

- Items tagged **[needs recheck]** (A3 internal responsibilities; B2/B3/B4 line numbers; C1/C2/C3
  line numbers; C4/C5 distribution) must be re-read against source before touching code, so each
  abstraction lands on the right boundary.
