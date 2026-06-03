# AI Runtime RPC Knowledge

This file separates what is **implemented today** from **planned direction**. Only
rely on the "Implemented today" facts when automating; treat the rest as intent that
may not exist in the codebase yet.

## Implemented today

- Vultra's default AI tooling bridge is out-of-process Runtime MCP/RPC, not
  pybind11.
- `--rpc` is an alias for `--mcp`.
- `--render-mode=visible|offscreen|none` is recorded in Runtime MCP status.
- Headless means no visible window; it does not always mean no rendering.
- Visual embodied AI training should use `--render-mode=offscreen`: no visible
  window, GPU/render services active, visual observations captured from render
  textures.
- `--rpc --render-mode=offscreen --project <project>` runs the project in
  runtime mode, not the Launcher, when `--vpk` is not supplied.
- Non-visual training, CI, and large pure simulation batches should use
  `--render-mode=none`: no visible window and no GPU/render path.
- For `offscreen` and `none` project launches, an explicit `--project` takes
  priority over auto-discovered default VPKs unless `--vpk` is also supplied.
- Python tools should use batched simulation calls and avoid per-entity small
  RPC loops.
- The current data plane is JSON over MCP-over-HTTP.
- `vultra.render.stream` starts/stops an HTTP MJPEG stream exposed under the
  same Runtime MCP localhost server at `/stream/<id>`.
- `vultra.render.stream` treats `fps=0` as "stream every available engine frame";
  positive `fps` values cap the MJPEG rate.
- The MJPEG stream uses a latest-frame raw buffer plus a background encoder
  thread so HTTP clients consume the newest encoded frame without blocking the
  main runtime thread on JPEG encoding.
- The MJPEG stream uses Vulkan async readback slots for preview frames instead
  of per-frame `waitIdle()` readback. When preview size caps are set, it first
  blits/downscales on GPU and reads back only the smaller preview texture.
- `vultra.render.stream` accepts `maxWidth` and `maxHeight` caps for preview
  resolution while preserving aspect ratio; use these for high-FPS browser
  preview instead of sending full-resolution JPEG when fidelity is not required.
- The lightweight Python client lives at `tools/python/vultra_client`.
- Render capture is unavailable in `render-mode=none`.
- This bridge does not require Lua documentation updates unless a later task
  exposes the same behavior as player-facing gameplay scripting.
- PowerShell MCP helper functions must not use `$args` as the parameter name for
  tool arguments. `$args` is a PowerShell automatic variable and can cause
  `tools/call.params.arguments` to be sent as an array instead of an object; use
  `$toolArgs` or another explicit name.

## Planned direction (not yet implemented)

These describe where the bridge is headed. Do not assume any of these exist; verify
against source before automating against them.

- RPC is intended as the long-term control plane, with JSON, binary payloads,
  shared memory, and CUDA/GPU interop as progressively stronger data-plane options.
- The target training loop: Python writes batched actions, RPC steps K frames,
  Vultra fills batched observations, Python reads them into PyTorch.
- MJPEG is only the first video transport for visual/offscreen observation; higher
  throughput training is expected to move to binary/shared-memory or GPU-interop
  data planes.
- Intended ordering: shared memory first for large state observations; reserve
  CUDA/GPU interop for visual/tensor-heavy offscreen workloads after profiling.
