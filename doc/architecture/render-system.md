# RenderSystem architecture and split plan

`source/vultra/src/function/rendering/render_system.cpp` is ~5000 lines and mixes
many responsibilities in one translation unit. This document describes those
responsibilities and a concrete, build-verified-incremental plan to split it. It is
intended to be executed in small steps, each followed by `xmake build -y vultra-app`.

## Current responsibilities (file order)

1. **Anonymous-namespace material helpers** (~lines 76–2700)
   - Material-graph parameter structs (`MaterialGraphSurfaceParams`, `MaterialParamsPBRMR`).
   - JSON/graph utilities, recursive graph evaluation (`constantNodeValue`,
     `surfaceInputValue`, `materialGraphTextureIndex`), graph cache (`cachedMaterialGraph`).
   - Builtin/graph/shader material cooking into GPU materials
     (`ensureMaterialGraphGpuMaterial`, `ensureMaterialAssetGpuMaterial`,
     `ensureBuiltinMaterialAssetGpuMaterial`, shader-param serialization).
   - Frame-graph debug/capture helpers.
   - Render-world cooking (gaussian-splat sorting/selection, `RenderWorldCooker::cook`).
2. **Lifecycle**: `onInit`, `onShutdown`.
3. **Renderer management**: `registerRenderer`, `rendererKeys`, `resolveRenderer`,
   `rendererRequiresRayTracingScene`.
4. **Resize / scene state**: `onResize`, `resetSceneState`.
5. **Pipeline reload**: `reloadRenderPipeline[Now]`, `updateRenderGraph[Now]`,
   `reloadProjectShaderLibrary`.
6. **Frame-graph debug/capture**: `clearFrameGraphDebugState`,
   `getFrameGraphTexturePreviewPipeline`, `addFrameGraphTextureCapturePasses` (~345 lines).
7. **Per-frame render loop**: `renderFrame` (~1267 lines) plus `onPreRender`/`onRender`/
   `onPostRender`/`onPresent`.

## Proposed split (by responsibility)

Move free helpers into `render_system_internal.hpp` (shared declarations) plus focused
`.cpp` units; keep `RenderSystem` methods grouped into a few TUs that include that header.

| New unit | Contents | Notes |
|---|---|---|
| `material/material_params.hpp` | `MaterialParamsPBRMR` (+ alpha-mode helpers) | **Done**: extracted to `vultra/function/material/material_params.hpp` and shared by `render_system.cpp`, `asset_system.cpp`, `direct_gbuffer_pass.cpp` (3 byte-identical copies). NOTE: `compatibility_basecolor_pass.cpp` keeps its own *different, smaller* `MaterialParamsPBRMR` layout (a distinct GPU ABI) and must NOT use this header. NOTE: `emissiveFactor` (rgb = emissive colour x strength) is appended at the **end** of the struct (offset 80) so the hand-written byte offsets in `gpu_scene.glsl`'s `get_pbrmr_params` stay valid — never insert fields mid-struct. |
| `rendering/material_graph_cook.cpp` | graph evaluation + `cachedMaterialGraph` + graph→GPU material | Owns the graph cache; expose a small accessor instead of a TU-local static. |
| `rendering/shader_material_cook.cpp` | shader-source material cooking + param serialization | Owns the shader-material cache and `warnShaderMaterialOnce` dedup set. |
| `rendering/render_world_cook.cpp` | `RenderWorldCooker::cook`, splat sort/select | Self-contained; depends on the two cook units above. |
| `rendering/render_system_framegraph_debug.cpp` | capture-pass building, preview pipeline, debug state | Large but cohesive; moves en masse. |
| `render_system.cpp` (slimmed) | lifecycle, renderer mgmt, resize, pipeline reload, `renderFrame`, events | The render loop stays here. |

## Barriers to handle

- **TU-local caches** at (current) lines ~449 (graph cache), ~590 (shader-material
  cache), ~945 (warn-once set). When moving the functions, move the cache with them and
  expose a function-scoped `static` inside the new TU, or a small `MaterialCookCache`
  object owned by `RenderSystem` and passed in. Do **not** leave a static in one TU that
  another TU expects to read.
- **Shared structs** (`MaterialParamsPBRMR`) — extract to the shared header before
  moving any cooking code, so all four current copies converge.

## Execution order (each step builds)

1. Extract `material/material_params.hpp`; replace the 4 duplicate definitions. Build.
2. Move shader-material cooking to its own TU. Build.
3. Move graph cooking to its own TU. Build.
4. Move render-world cooking. Build.
5. Move frame-graph debug/capture. Build.
6. Confirm `render_system.cpp` is the render loop + lifecycle only. Build + MCP render
   smoke (`vultra.render.capture_rgb`).
