# Engine Workspace

Journals, handoff notes, verification logs, and cross-session planning artifacts.

A fresh agent should be able to find the current state from **this index alone**
without reading every note. Durable plans live in `ai/tasks/`; stable facts live in
`ai/knowledge/`. Workspace notes are the transient record around them.

## Conventions

- Start every note from `TEMPLATE.md`: it begins with `Date:` and `Status:`
  (`In-Progress | Blocked | Done`) so notes can be sorted and reaped by status/age.
- When a note reaches `Status: Done` and its work has landed, move it to `archive/`
  and add a line to the Archive index below.
- Keep notes short; link to specs/tasks instead of copying them.

## Active notes

Notes for work that is in progress, blocked, or otherwise still load-bearing. Keep this
list small — these are the files a fresh agent should actually open.

- [foundation-roadmap.md](foundation-roadmap.md) — full-engine audit + 6-phase
  foundation roadmap (Lua API spec/conformance, binding generator, script lifecycle,
  ImGui binding, editor extension API, plugin v2). Phase 1 in progress.
- [codex-debt-review-2026-06-03.md](codex-debt-review-2026-06-03.md) — engineering-debt
  audit and the current cleanup roadmap (P0/P1/P2).
- [codex-debt-review-round2-2026-06-09.md](codex-debt-review-round2-2026-06-09.md) —
  Round 2 audit: duplication/abstraction/interface debt in the editor UI layer, asset
  loaders, and render passes (P0/P1/P2); extends the roadmap above.
- [codex-debt-review-round3-2026-06-10.md](codex-debt-review-round3-2026-06-10.md) —
  Round 3 audit (P0-P3): post-Round-2 new code (prefabs, declarative_renderer, XR view
  synthesis) + deferred items; all P0/P1 executed same-session (execution log Round 8).
- [codex-debt-cleanup-execution.md](codex-debt-cleanup-execution.md) — execution log for
  the roadmap above (Rounds 1-8; Round 8 = Round 3 audit's P0+P1, all landed).
- [asset-dependency-residency-handoff.md](asset-dependency-residency-handoff.md) —
  asset dependency graph and residency-pin architecture.
- [async-asset-loading-phase1.md](async-asset-loading-phase1.md) — non-blocking asset
  requests and async texture cooking.
- [editor-agent-mcp.md](editor-agent-mcp.md) — editor-facing MCP configuration layer.
- [graph-layout-rules.md](graph-layout-rules.md) — automatic graph layout / data-flow
  topology rules.

## Archive

Completed, landed notes kept for history. Grouped by area; all are `Status: Done`.

### Unified material assets

- [unified-material-asset-phase1.md](archive/unified-material-asset-phase1.md) — unified `.vmat.json` material asset model (2026-06-02)
- [unified-material-asset-lua-api.md](archive/unified-material-asset-lua-api.md) — Lua material slot and property-block APIs (2026-06-02)
- [unified-material-assets-cleanup-verification.md](archive/unified-material-assets-cleanup-verification.md) — cleanup of abandoned shader-pass material model (2026-06-03)
- [unified-material-assets-parser-diagnostics.md](archive/unified-material-assets-parser-diagnostics.md) — parser/validation for `.vmat.json` assets (2026-06-03)
- [unified-material-graph-custom-node-descriptors.md](archive/unified-material-graph-custom-node-descriptors.md) — custom material graph nodes via `.vmatnode.json` (2026-06-03)
- [unified-material-graph-instance-verification.md](archive/unified-material-graph-instance-verification.md) — graph-backed material instance identity fixes (2026-06-03)
- [unified-material-mcp-smoke.md](archive/unified-material-mcp-smoke.md) — MCP workflow validation for unified materials (2026-06-03)
- [unified-material-property-block-authoring.md](archive/unified-material-property-block-authoring.md) — Inspector editing of material property blocks (2026-06-03)
- [unified-material-single-shader-source.md](archive/unified-material-single-shader-source.md) — single-shader material source with reflection (2026-06-03)
- [material-authoring-ux-followup.md](archive/material-authoring-ux-followup.md) — forking builtins, node search, material graph inspector (2026-06-03)
- [material-graph-tint-pulse.md](archive/material-graph-tint-pulse.md) — default material graph with time-driven color effect
- [mesh-material-ssbo-baseline.md](archive/mesh-material-ssbo-baseline.md) — shader-backed mesh materials with SSBO baseline (2026-06-03)

### Runtime MCP and AI runtime

- [ai-runtime-rpc-bridge-v1.md](archive/ai-runtime-rpc-bridge-v1.md) — AI runtime RPC bridge v1 verified
- [ai-runtime-headless-none-phase1.md](archive/ai-runtime-headless-none-phase1.md) — no-window runtime mode with disabled GPU resources (2026-06-02)
- [ai-runtime-headless-offscreen-handoff.md](archive/ai-runtime-headless-offscreen-handoff.md) — headless vs offscreen render-mode design (2026-06-01)
- [ai-runtime-offscreen-project-phase1.md](archive/ai-runtime-offscreen-project-phase1.md) — offscreen rendering for project runtime sessions (2026-06-02)
- [ai-runtime-mjpeg-stream-phase1.md](archive/ai-runtime-mjpeg-stream-phase1.md) — HTTP MJPEG stream endpoint for visual capture (2026-06-02)
- [ai-runtime-mjpeg-stream-async-readback.md](archive/ai-runtime-mjpeg-stream-async-readback.md) — async GPU readback for MJPEG streaming (2026-06-02)
- [ai-runtime-mjpeg-stream-buffered-encoder.md](archive/ai-runtime-mjpeg-stream-buffered-encoder.md) — background-thread encoder for MJPEG streams (2026-06-02)
- [ai-runtime-mjpeg-stream-uncapped.md](archive/ai-runtime-mjpeg-stream-uncapped.md) — uncapped-FPS MJPEG capture mode (2026-06-02)
- [runtime-mcp-v1-verification.md](archive/runtime-mcp-v1-verification.md) — Runtime MCP server embedded in vultra-app verified
- [runtime-mcp-component-metadata.md](archive/runtime-mcp-component-metadata.md) — MCP tools for component metadata/inspection (2026-06-02)
- [runtime-mcp-readme-usage-update.md](archive/runtime-mcp-readme-usage-update.md) — updated Runtime MCP docs with examples (2026-06-02)
- [runtime-frame-graph-viewer-popup.md](archive/runtime-frame-graph-viewer-popup.md) — independent frame-graph viewer with texture previews (2026-05-29)
- [docs-cross-shell-command-examples.md](archive/docs-cross-shell-command-examples.md) — cross-shell-compatible Runtime MCP docs

### Rendering and render graph

- [builtin-rendergraphs.md](archive/builtin-rendergraphs.md) — embedded builtin render graphs and XR graph updates (2026-05-29)
- [builtin-environment-thumbnail-perf.md](archive/builtin-environment-thumbnail-perf.md) — builtin sky texture and thumbnail performance (2026-05-30)
- [render-performance-alpha-history-fixes.md](archive/render-performance-alpha-history-fixes.md) — render perf, alpha handling, history optimization (2026-05-30)
- [editor-render-freeze-and-framegraph-notes.md](archive/editor-render-freeze-and-framegraph-notes.md) — static frame rendering and frame-graph notes (2026-05-31)
- [rendergraph-editor-pass-catalog-sync.md](archive/rendergraph-editor-pass-catalog-sync.md) — editor pass-catalog and stale-link repair (2026-05-29)
- [rendergraph-node-placement.md](archive/rendergraph-node-placement.md) — screen-space node placement for render-graph adds (2026-05-31)
- [rendergraph-thumbnail-template-fixes.md](archive/rendergraph-thumbnail-template-fixes.md) — model thumbnail and render-graph template fixes (2026-05-30)
- [rt-lighting-raster-parity.md](archive/rt-lighting-raster-parity.md) — ray-tracing lighting alignment with raster deferred

### XR

- [xr-default-view-synthesis.md](archive/xr-default-view-synthesis.md) — XR view synthesis with adaptive mesh and pull-push (2026-05-27)
- [xr-rendergraph-stereo-abstraction.md](archive/xr-rendergraph-stereo-abstraction.md) — stereo render graph with viewMask/multiview (2026-05-27)
- [xr-rendergraph-view-synthesis-redesign.md](archive/xr-rendergraph-view-synthesis-redesign.md) — atomic XR synthesis passes and VRAM optimization (2026-05-28)
- [xr-view-synthesis-atomic-geometry.md](archive/xr-view-synthesis-atomic-geometry.md) — geometry warp and pull-push inpainting passes (2026-05-29)
- [xr-vr-runtime.md](archive/xr-vr-runtime.md) — XR scene camera and OpenXR session infrastructure

### Editor and UI UX

- [editor-asset-import-usability.md](archive/editor-asset-import-usability.md) — asset import, drag-drop, thumbnail improvements (2026-05-27)
- [editor-history.md](archive/editor-history.md) — undo/redo with scene-snapshot history
- [editor-profiler-sync-lifetime.md](archive/editor-profiler-sync-lifetime.md) — GPU lifetime safety and VRAM budget tracking (2026-05-28)
- [editor-settings-persistence.md](archive/editor-settings-persistence.md) — editor theme/settings persistence
- [editor-startup-splash-import-progress.md](archive/editor-startup-splash-import-progress.md) — async asset import with splash progress (2026-06-03)
- [editor-themes.md](archive/editor-themes.md) — session-local editor theme switching
- [export-run-output-directory-fix.md](archive/export-run-output-directory-fix.md) — fixed export directory UI default (2026-05-31)
- [fullscreen-close-resize.md](archive/fullscreen-close-resize.md) — prevented resize after fullscreen close
- [content-browser-asset-creation.md](archive/content-browser-asset-creation.md) — asset-creation menu for scenes/scripts/materials
- [content-browser-import-delete.md](archive/content-browser-import-delete.md) — import/delete UI and sidecar cleanup (2026-05-29)
- [scene-hierarchy-create-menu.md](archive/scene-hierarchy-create-menu.md) — right-click creation for entities/lights/cameras
- [resource-selector-previews.md](archive/resource-selector-previews.md) — texture/mesh selector preview rows
- [ui-auto-canvas-authoring.md](archive/ui-auto-canvas-authoring.md) — auto-create canvas for UI elements
- [ui-imguizmo-2d-spike.md](archive/ui-imguizmo-2d-spike.md) — 2D UI authoring with handles/canvas overlay
- [ui-render-preview-layer-culling.md](archive/ui-render-preview-layer-culling.md) — render-layer culling and real-rendered UI preview
- [readme-refresh.md](archive/readme-refresh.md) — README refresh for current architecture
- [vscode-workspace-settings.md](archive/vscode-workspace-settings.md) — workspace docs and clang-format CI pass (2026-05-27)

### Assets, import, animation, physics, scripting

- [asset-registry-reconcile.md](archive/asset-registry-reconcile.md) — stale asset-registry cleanup on startup
- [vasset-basisu-threading.md](archive/vasset-basisu-threading.md) — BasisU texture compression threading defaults
- [vasset-example-vpk-cpp-import.md](archive/vasset-example-vpk-cpp-import.md) — C++ asset import/pack API replacing CLI
- [vmesh-local-bounds.md](archive/vmesh-local-bounds.md) — mesh AABB bounds storage and SunTemple FBX fixes (2026-05-31)
- [skeletal-animation-import-phase1.md](archive/skeletal-animation-import-phase1.md) — skeletal asset import with ozz binary wrappers
- [animation-system-phase1.md](archive/animation-system-phase1.md) — skeletal animation playback with GPU skinning (2026-05-31)
- [jolt-physics-integration.md](archive/jolt-physics-integration.md) — physics system: rigid bodies, shapes, simulation
- [script-binding-parity-docs.md](archive/script-binding-parity-docs.md) — Lua binding parity policy and docs
- [shader-import-release-crash.md](archive/shader-import-release-crash.md) — Release-build crash from MSVC toolset mismatch
- [stale-test-scene-fallbacks.md](archive/stale-test-scene-fallbacks.md) — removed stale test-scene defaults/fallbacks
