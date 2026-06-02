# AI Runtime Headless None Phase 1

Date: 2026-06-02

## Implementation Notes

- Added a `RuntimeHeadlessApp` path for non-editor `--render-mode none`.
- The new path starts no window, no render backend, no ImGui, and no GPU
  resource service.
- It keeps the runtime MCP server alive through the existing `EditorApp`
  Runtime MCP owner and exposes the existing `vultra.sim.*` tools.
- It configures project or VPK assets, loads the requested/default scene through
  `SceneSystem`, and ticks world, physics, script, and animation services.
- `AssetSystem` can now initialize without render/GPU services for CPU/text
  asset access. GPU upload work is skipped in that mode.
- `vultra.editor.quit` now also shuts down no-window runtime sessions by setting
  `editorShutdownRequested` when no `WindowService` exists.
- CLI help now documents `--render-mode none` as a no-window simulation path and
  `--render-mode offscreen` as hidden-window rendering for visual capture.

## Verification

- `git diff --check`: passed.
- `xmake build -y vultra-app`: passed.
- Started:
  `build/windows/x64/release/vultra-app/vultra.exe --rpc --render-mode none --project example.vproject --no-xr --mcp-port 8863`.
- MCP `initialize`: passed.
- MCP `tools/list`: passed and included `vultra.sim.step` plus
  `vultra.render.capture_rgb`.
- `vultra.runtime.status`: passed with `mode=runtime`,
  `renderMode=none`, `backend=unavailable`, and empty `rendererKeys`.
- `vultra.render.capture_rgb`: returned the expected
  `render.capture_rgb is unavailable when renderMode is none` error.
- `vultra.editor.quit`: passed and the process exited with code 0.
- Started a second no-window process on port 8864 and smoke-tested
  `vultra.sim.step(frames=1, limit=4)`: passed with `ok=true`.

## Handoff

- `render-mode=none` now has the first real no-window/no-GPU lifecycle.
- `render-mode=offscreen` still uses the existing hidden-window render path.
  The next slice should make visual capture robust for hidden/offscreen runtime
  sessions and eventually move from hidden swapchain capture toward camera render
  texture observations.
- GPU asset requests in `render-mode=none` are intentionally not useful yet;
  simulation-only content should rely on text/scene/script/animation/physics
  data. A later task can add explicit CPU-only mesh/material metadata queries if
  training workloads need them.
