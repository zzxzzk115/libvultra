# AI Runtime RPC Bridge

## Intent

Vultra AI tooling uses the embedded Runtime MCP HTTP JSON-RPC endpoint as an
out-of-process bridge instead of embedding Python or exposing the engine through
pybind11 by default.

## V1 Shape

- `--rpc` is an alias for `--mcp`.
- `--render-mode` accepts `visible`, `offscreen`, or `none`.
- Render modes have distinct training roles:
  - `visible`: windowed rendering for human observation and debugging.
  - `offscreen`: no visible window, GPU/render services stay active, cameras
    render into textures, and `capture_rgb`/`capture_depth` provide visual
    observations for Python/PyTorch.
  - `none`: no visible window and no GPU/render path; only world, physics,
    script, state, action, and RPC services run.
- Python/PyTorch tools run outside `vultra.exe` and call localhost RPC.
- Simulation tools are batch-oriented:
  - `vultra.sim.reset`
  - `vultra.sim.step`
  - `vultra.sim.get_state_batch`
  - `vultra.sim.set_state_batch`
  - `vultra.sim.apply_actions_batch`
- Render capture tools are available when render mode is not `none`:
  - `vultra.render.capture_rgb`
  - `vultra.render.capture_depth`
- Browser/debug preview video is exposed as `vultra.render.stream`, with MJPEG
  served from `/stream/<id>`. `fps=0` means uncapped capture, positive `fps`
  caps the stream, and `maxWidth`/`maxHeight` cap preview resolution while
  preserving aspect ratio. Preview readback should avoid per-frame device idle:
  use in-flight readback slots and perform GPU downscale before CPU readback
  when preview caps are active.

## Constraints

- Do not link CPython, pybind11, NumPy, PyTorch, or libtorch into the default
  runtime for this bridge.
- Keep the RPC layer thin over engine services/components.
- Prefer large batched calls over per-entity/per-field round trips.
- `render-mode=offscreen` is the primary path for visual embodied AI training.
- `render-mode=none` must return explicit errors for render capture and should
  not be described as a visual observation mode.
- Lua parity is not required for v1 because this is an external automation
  surface, not a player-facing gameplay scripting API.

## Headless Implementation Flow

Headless does not mean "no rendering"; it means "no visible window". Implement
the next runtime work in two phases:

1. Extract a shared no-window runtime/MCP lifecycle so project runtime mode can
   start without EditorApp, ProjectLauncher, or visible window ownership.
2. Split the services by render mode:
   - `offscreen`: keep GPU/render/camera services, render to textures, and make
     RGB/depth capture usable without a visible window.
   - `none`: use a lighter simulation-only service set for physics/script/RPC,
     with render capture unavailable by design.

## Data Plane Roadmap

RPC remains the control plane. Data movement can evolve independently as
training workloads expose bottlenecks:

1. JSON-RPC control and JSON state payloads for prototyping and debugging.
2. Binary payloads for action/state arrays, using fixed float32/int32 schemas.
3. Shared memory buffers for large observation/action batches, with RPC sending
   only step/reset commands plus buffer handles, shapes, offsets, and readiness.
4. CUDA/GPU interop for visual observations and tensor-heavy training, keeping
   RGB/depth/segmentation buffers on GPU where possible instead of round-tripping
   through PNG files or CPU JSON arrays.
5. Hardware video encoding for human preview or teleoperation streams when
   browser-friendly video transport needs higher frame rates than CPU MJPEG can
   provide.

## Ideal Training Loop

The ideal high-throughput implementation keeps Python/PyTorch outside the
engine process while minimizing synchronization:

```text
Python/PyTorch writes batched actions tensor/buffer
RPC control call: step K frames
Vultra sim/render advances locally and fills observation buffers
Python/PyTorch reads batched observation tensor/buffer
Trainer computes rewards/losses and repeats
```

For state-only training, shared memory is likely enough. For visual embodied
training, add GPU/CUDA interop only after profiling proves CPU/binary transfer
or image readback is the bottleneck.
