# AI Runtime Offscreen Project Phase 1

Date: 2026-06-02

## Implementation Notes

- Non-editor `--rpc --render-mode offscreen --project <project>` now enters
  runtime mode instead of hidden Launcher mode.
- Explicit project runtime launches take priority over auto-discovered default
  VPKs when `--vpk` is not supplied. Passing `--vpk` still keeps the packaged
  runtime path.
- Project runtime reuses `ProjectLauncher::configureAssets` for project
  asset-root configuration, then loads the project default scene.
- Runtime post-configure now treats project and VPK runtime the same way:
  load render graphs, enable world cameras, load the runtime scene, and keep
  Runtime MCP pumping.
- If the editing render graph is not present in the asset registry render-graph
  scan, it is added explicitly so project runtime has the expected renderer key.
- The same project-over-default-VPK priority was applied to
  `render-mode=none`.

## Verification

- `git diff --check`: passed.
- `xmake build -y vultra-app`: passed.
- Started:
  `build/windows/x64/release/vultra-app/vultra.exe --rpc --render-mode offscreen --project example.vproject --no-xr --mcp-port 8866`.
- MCP `initialize`: passed.
- `vultra.runtime.status`: passed with `mode=runtime`,
  `renderMode=offscreen`, `backend=vulkan`, `projectName=example`,
  `defaultScene=res://scenes/sponza.vscn`, and renderer keys including
  `default`.
- `vultra.sim.get_state_batch(limit=4)`: passed and returned scene entities
  plus live physics body data.
- `vultra.render.capture_rgb`: passed and wrote
  `.vultra/mcp/offscreen_project_rgb_smoke.png`.
- `vultra.render.capture_depth`: passed and dumped two `DirectDepthPre`
  textures into `.vultra/mcp/offscreen_project_depth_smoke`.
- `vultra.editor.quit`: passed and the process exited with code 0.

## Handoff

- `render-mode=offscreen` is now usable for visual project-runtime smoke tests
  through the hidden-window render path.
- The current RGB capture is still a backbuffer PNG capture. The next visual AI
  slice should introduce camera observation capture over render textures so RGB,
  depth, and later segmentation can be requested as structured observations
  instead of editor/backbuffer-oriented files.
- Depth capture currently uses frame graph texture dumps and may return duplicate
  entries when the same resource is present across captured frames. A later pass
  should add observation-oriented de-duplication or frame indexing.
