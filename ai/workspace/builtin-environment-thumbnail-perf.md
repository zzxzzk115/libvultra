# Builtin Environment And Thumbnail Perf Notes

Date: 2026-05-30

## Changes

- Added the citrus orchard sky as an embedded builtin VTexture payload.
- Added a fixed builtin UUID/URI for the sky texture and taught `AssetSystem`
  texture loading to resolve/read it.
- Made `EnvironmentComponent` default to the builtin sky texture.
- Updated the launcher default scene template to include an Environment entity
  and set the main Camera clear mode to Skybox.
- Reduced render thumbnail warmup from 4 frames to 1 frame. Model/scene/material
  graph thumbnails are GPU render jobs processed one at a time, so worker-thread
  count is not the main bottleneck for model thumbnail latency.

## Verification

- `xmake build -y vultra-app` passed.

## Follow-Up

- The profiler screenshot shows multiple `RenderCamera::execute` entries in one
  frame, consistent with Scene/Game/preview cameras all rendering.
- `EditorApp::commandsAndHistory` was around 10 ms in the screenshot and needs a
  focused pass on history observation and command processing costs.
