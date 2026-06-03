# AI Runtime Headless/Offscreen Handoff

Date: 2026-06-01

Current repo state:

- Branch: `dev-next`
- Commit: `3fd73b7`
- Completed baseline: `feat: add AI runtime RPC bridge`

Design decision:

- Headless means no visible window, not necessarily no rendering.
- Visual embodied AI training should use `--render-mode offscreen`.
- Pure physics/state/script training, CI, and high-volume non-visual simulation
  should use `--render-mode none`.

Recommended launch shapes:

- Visual observations:
  `vultra-app --rpc --render-mode offscreen --project example.vproject --no-xr`
- Non-visual simulation:
  `vultra-app --rpc --render-mode none --project example.vproject --no-xr`

Next ideal implementation flow:

1. Extract a shared no-window runtime/MCP lifecycle that does not depend on the
   editor or launcher.
2. Make `offscreen` the visual headless path: no visible window, GPU/render
   active, cameras render to textures, `capture_rgb/depth` return observations.
3. Make `none` the pure simulation path: no window, no GPU/render, world and
   physics/script/RPC only, render capture returns explicit errors.
4. Profile state/action and visual observation transfer before adding binary
   payloads, shared memory, CUDA, or GPU interop.
