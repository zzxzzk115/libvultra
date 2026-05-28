# Editor Profiler Synchronization / Lifetime Audit

Date: 2026-05-28

## Trigger

Opening the Profiler tab could still hit Vulkan `ErrorDeviceLost`, especially after XR/editor view work. The crash pattern pointed at GPU in-flight lifetime hazards rather than the Profiler UI itself.

## Findings

- `ImGuiSystem::removeTexture` freed backend ImGui texture descriptors immediately. In Vulkan this maps to `ImGui_ImplVulkan_RemoveTexture`, so a descriptor set could be returned while the previous submitted ImGui draw still referenced it.
- Several editor windows destroyed owned render-target textures immediately on close/collapse/project reset paths:
  - Game View render target and XR mirror descriptors.
  - Scene View render/picking/game-overlay targets.
  - Render Graph preview overlay and runtime texture thumbnails.
  - Inspector model preview target.
  - Material Graph preview target.
  - Asset preview cache textures/icons.
- Normal resize paths mostly already used retired target queues; the risky paths were the low-frequency immediate release paths reached by docking/tab changes.
- RenderSystem issued frame GPU timestamp queries even when `RuntimeProfiler` capture was disabled. Opening the Profiler window should not by itself add timestamp query pressure.

## Changes

- `ImGuiSystem::removeTexture` now retires backend texture ids and releases them after four post-render frames, with forced cleanup at ImGui shutdown.
- RenderSystem now writes frame/scope GPU timestamp queries only while runtime profiler capture is enabled and Tracky is not active. Opening the Profiler window without Capture no longer records GPU timestamps.
- Immediate editor-owned render-target destruction paths now call `RenderDevice::waitIdle()` before dropping textures/descriptors. Resize still uses delayed retirement, so drag-resize remains non-blocking except for explicit hide/close/reset.
- Asset preview cache clear/trim waits before releasing preview descriptors and owned preview textures.

## Verification

- `xmake build -y vultra-app` passed.
- `git diff --check` passed.

## Follow-Up

- If Capture-enabled GPU scope timing still causes device loss, audit Vulkan timestamp placement inside active dynamic rendering/compute regions next. The current fix isolates that risk behind the explicit Capture toggle.

## VRAM Budget Follow-Up

- Added RHI-facing `RenderDeviceMemoryBudget`.
- Vulkan enables `VK_EXT_memory_budget` when available and VMA is created with `eExtMemoryBudget`, so the editor can query real device-local heap budget/usage/available bytes instead of relying only on Vultra's internal allocation ledger.
- WebGPU currently reports budget unavailable.
- The bottom editor task bar now shows `VRAM used / budget` when a real budget is available.
- Profiler Memory tab shows budget, available bytes, and a usage bar before the resource table.
- Build verification: `xmake build -y vultra-app` passed.

## FrameGraph Transient Cache Correction

- Clarified the confusing `FrameGraph Texture` entries: these are runtime frame graph transient render targets, not Render Graph editor window or Frame Debugger UI caches.
- Compared against `dev`'s original `source/src/function/framegraph/transient_resources.cpp`.
- Important correction: the original design immediately reused released resources from the matching desc pool. My intermediate `kMinReusableResourceAgeFrames` change was wrong for this pool because it forced duplicate large render targets across frames.
- Restored the original immediate reuse behavior:
  - `acquireTexture` / `acquireBuffer` pop from the matching pool immediately when available.
  - `releaseTexture` / `releaseBuffer` push back with life 0.
  - `heartbeat` only ages unused cached resources and deletes them after 10 frames.
- Kept only resource labeling/statistics around the original reuse behavior.
- Build verification: `xmake build -y vultra-app` passed.

## XR Swapchain Allocation Correction

- Found that `resources/scenes/sponza.vscn` still had `XRViewComponent/enabled = true` on the primary Camera, which caused `XRRuntimeSystem` to auto-request XR for the default scene.
- Also found that `XRHeadset` created the OpenXR swapchain and PiMax-sized render-target views in its constructor, before `xrBeginSession` and before `XrFrameState::shouldRender`.
- Changed `XRHeadset` to create only the OpenXR session/reference-space and eye metadata during construction. Eye swapchain images/render-target views are now created lazily only after `xrBeginFrame` succeeds and `shouldRender == true`.
- `endSession` and destructor now destroy swapchain views/images and only call `xrEndSession` when a session is actually running.
- Removed the serialized XRView component from `resources/scenes/sponza.vscn`.
- Build verification: `xmake build -y vultra-app` passed.

## Runtime Render Graph Preview Size Correction

- Runtime graph preview camera sizing had regressed toward preview/UI dimensions when Game View had not yet marked a current-frame render target, or when the Game View was not visible.
- Added persistent `gameViewLastRenderTargetWidth/Height` state that is updated only from a real Game View render target.
- Render Graph runtime preview cameras now use that last real Game View target size, so overlay thumbnails and XR mirror preview dimensions do not shrink the runtime graph target.
- Build verification: `xmake build -y vultra-app` passed.

## Thumbnail Sampling Correction

- Asset preview thumbnails and image/icon previews now bind ImGui textures with an explicit linear clamp sampler.
- Runtime Render Graph texture thumbnails now use linear clamp sampling instead of nearest clamp sampling.
- Build verification: `xmake build -y vultra-app` passed.

## Runtime Graph Capture Cost Correction

- Runtime Graph preview previously enabled frame graph texture capture every frame and requested all texture previews.
- That path does not copy textures back to CPU, but it injects fullscreen debug-preview passes for each capturable frame graph texture, which can materially lower FPS.
- Added Capture / Pause / Auto-enable controls. Continuous capture is now opt-in; a single Capture refreshes thumbnails, then the last captured debug textures remain visible without adding capture passes every frame.
- Follow-up correction: opening Runtime Graph also forced selection of the synthetic `Render Graph Preview` camera. That synthetic camera renders the edited graph as a full extra camera every frame, so FPS remained low even with texture capture off. Opening Runtime Graph now prefers existing Game/Camera graphs and does not auto-create the preview camera.
- Follow-up correction: the normal Render Graph editor canvas also rendered a bottom-left Game Preview overlay by default whenever Game View was not visible. That overlay registered the same synthetic preview camera every frame. The overlay is now disabled by default and controlled by a `Game Preview` checkbox.
- Build verification: `xmake build -y vultra-app` passed.

## Render Graph Editor Profiling Labels

- Added `RuntimeProfiler::addExternalCpuScope` so editor UI work performed before `RenderSystem::renderFrame` can still appear in the built-in Profiler CPU tree.
- Added Render Graph editor labels:
  - `Editor::RenderGraph/Window`
  - `Editor::RenderGraph/GraphEditor`
  - `Editor::RenderGraph/AssetSelector`
  - `Editor::RenderGraph/RegisterPassCatalog`
  - `Editor::RenderGraph/Canvas`
  - `Editor::RenderGraph/CanvasImNodes`
  - `Editor::RenderGraph/PipelineCanvas`
  - `Editor::RenderGraph/PipelineImNodes`
  - `Editor::RenderGraph/AddPopup`
  - `Editor::RenderGraph/GamePreviewOverlay`
  - Runtime graph labels for parse, texture lookup, graph-node rendering, popup, and texture preview.
- Build verification: `xmake build -y vultra-app` passed.

## Render Graph Asset Selector Cache

- Profiler showed `Editor::RenderGraph/AssetSelector` taking about 25 ms per frame while the Render Graph editor was open.
- Root cause: the selector recursively scanned project assets for `.vrg.json` every frame.
- Cached render graph asset URIs in `RenderGraphWindow` and refresh only when project path, asset root, or project generation changes.
- Added `Editor::RenderGraph/AssetSelectorRefresh` so the expensive filesystem refresh is visible only when it actually occurs.
- Build verification: `xmake build -y vultra-app` passed.

## Main Loop Profiling Labels

- Extended external CPU scopes with a global profiler sink so core/editor paths outside `RenderSystem::renderFrame` can be labeled without plumbing `IRenderService` everywhere.
- External scopes now track nesting depth; CPU frame time only adds root external scopes to avoid double-counting nested editor labels.
- Added high-level labels for:
  - `MainLoop::stepFrame`, `pollEvents`, `engineTick`, before/after tick hooks.
  - `FramePipeline` phases and per-subsystem `Update` / `Render`.
  - `ImGuiSystem::begin`, `render`, `postRender`, retired texture collection.
  - `EditorShell::onImGui`, editor draw, launcher draw, shell camera setup.
  - `EditorApp` top bar, task bar, dockspace, window manager, history/commands, popups.
  - `EditorWindowManager` plus per-window draw/tick labels.
- Build verification: `xmake build -y vultra-app` passed.

## Profiler Scope Table Sorting

- CPU and GPU scope tables now use ImGui sortable table headers.
- Supported sort columns: `Scope`, `Total ms`, `Self ms`, and `Calls`.
- Sorting preserves tree structure by sorting sibling scopes under each parent instead of flattening the whole tree.
- Default sort is `Total ms` descending.
- Build verification: `xmake build -y vultra-app` passed.

## Render Graph Canvas Profiling Split

- Profiler showed the remaining Render Graph editor cost concentrated under `Editor::RenderGraph/CanvasImNodes`.
- Added finer labels for node drawing, node width calculation, shader label resolution, shader param collection, param widgets, pin drawing, link drawing, minimap/end, interactions, and context popups.
- Added lower-level labels for shader param desc reads, enum reads, shader ref resolution, and project pass Lua shader scans.
- Cached `registerEditorProjectRenderGraphPasses` behind project path, asset root, and project generation so the project pass catalog is not re-scanned every frame.
- Build verification: `xmake build -y vultra-app` passed.

## Render Graph Shader Metadata Cache

- Profiler confirmed the canvas cost was dominated by repeated shader metadata work:
  - `Editor::RenderGraph/ShaderParamDescs`
  - `Editor::RenderGraph/ShaderParamEnum`
  - `Editor::RenderGraph/ResolvePassShader`
  - `Editor::RenderGraph/ProjectPassShaderScan`
- Added editor-side caches for project pass shader refs, resolved shader refs, reflected/source shader param descriptors, and enum options.
- Cache keys include project root, asset root, project generation, pass type, explicit library/fragment, shader profile, and param name where applicable.
- Build verification: `xmake build -y vultra-app` passed.

## Runtime Graph Selection and DOT Safety

- Runtime Graph popup no longer exposes Capture / Pause / Auto-enable controls. It enables frame graph texture capture automatically while the popup is open and keeps the existing low-resolution thumbnail cap for overview mode.
- Opening Runtime Graph no longer injects a synthetic `Render Graph Preview` manual camera. The graph selector now prefers `Game`, then the normal `Camera`, then the largest real graph; a stale selected preview graph is ignored when a real game/camera graph exists.
- Runtime Graph rendering no longer passes raw DOT directly to `imgui_graphnode` / graphviz layout. It uses the already parsed safe node/edge representation instead, avoiding graphviz crashes from HTML-like labels such as `P0` / `R0_1`.
- Build verification: `xmake build -y vultra-app` passed.

## Render Graph Preview Resolution

- Render Graph editor preview is mandatory again when Game View is not visible, so Runtime Graph always has a camera graph to inspect while editing render graphs.
- Removed the `Game Preview` toggle from the Render Graph toolbar.
- The preview overlay display size remains a small UI thumbnail, but the backing render target now uses `gameViewLastRenderTargetWidth/Height` rather than thumbnail dimensions.
- When Game View is visible, Render Graph releases its preview render target to avoid duplicate rendering and VRAM usage.
- Build verification: `xmake build -y vultra-app` passed.

## Runtime Graph DOT Layout Restoration

- Restored the Runtime Graph path that passes `graph.rawDot` into `ImGuiGraphNode::NodeGraphLoadDot`.
- The previous raw-DOT bypass was reverted because the observed crash is tied to Runtime Graph popup/tab switching lifecycle, not to the DOT contents themselves.
- Build verification: `xmake build -y vultra-app` passed.

## Runtime Graph Attached Window Lifecycle

- Runtime Graph and Runtime Texture Preview are attached to the Render Graph editor window, not independent editor top-level windows.
- When the Render Graph tab/window becomes invisible, closes, or is destroyed, the window now explicitly suspends its attached runtime graph state:
  - closes Runtime Graph and texture preview windows,
  - disables frame graph texture capture,
  - clears texture preview overrides,
  - releases thumbnail ImGui textures,
  - resets pending auto-fit state,
  - invalidates the parsed runtime graph snapshot so DOT/layout reloads cleanly on next open.
- This prevents hidden attached windows from keeping capture/preview resources alive after switching to Game View.
- Build verification: `xmake build -y vultra-app` passed.

## Runtime Graph Open Side Effect Removal

- Opening Runtime Graph no longer calls `IRenderService::reloadRenderPipeline`.
- The reload was only intended to refresh the graph preview, but it can queue a render pipeline reload while the editor/render systems are mid-frame.
- Runtime Graph is now a pure inspector action: it opens the popup, resets selected graph state, and reads the next available frame graph snapshot.
- Build verification: `xmake build -y vultra-app` passed.

## Runtime Graph Independent Window

- Runtime Graph is now drawn as an independent non-modal ImGui window.
- The window continues to draw even when the Render Graph tab content is not visible, so it no longer behaves like an attached child window that silently disappears while keeping resources alive.
- It is intentionally not modal: double-clicking runtime graph nodes can still open independent large texture preview windows.
- While Runtime Graph or its texture preview is open, Render Graph tab invisibility no longer triggers runtime graph suspension. Closing the Runtime Graph window still runs the suspend path and releases capture/preview resources.
- Build verification: `xmake build -y vultra-app` passed.

## Runtime Graph Owned Preview Camera

- Runtime Graph modal now owns the preview camera while it is open.
- The Render Graph canvas preview overlay is hidden while Runtime Graph is open, but the modal keeps the backing render target alive and registers `Render Graph Preview` itself.
- The preview render target still uses `gameViewLastRenderTargetWidth/Height`, so Runtime Graph sees a graph matching the Game View target resolution rather than thumbnail dimensions.
- Runtime Graph capture settings are applied while the modal is open; the modal itself no longer blocks capture setup through the generic popup check.
- Build verification: `xmake build -y vultra-app` passed.
