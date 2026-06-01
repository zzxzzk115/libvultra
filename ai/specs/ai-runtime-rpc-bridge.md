# AI Runtime RPC Bridge

## Intent

Vultra AI tooling uses the embedded Runtime MCP HTTP JSON-RPC endpoint as an
out-of-process bridge instead of embedding Python or exposing the engine through
pybind11 by default.

## V1 Shape

- `--rpc` is an alias for `--mcp`.
- `--render-mode` accepts `visible`, `offscreen`, or `none`.
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

## Constraints

- Do not link CPython, pybind11, NumPy, PyTorch, or libtorch into the default
  runtime for this bridge.
- Keep the RPC layer thin over engine services/components.
- Prefer large batched calls over per-entity/per-field round trips.
- `render-mode=none` must return explicit errors for render capture.
- Lua parity is not required for v1 because this is an external automation
  surface, not a player-facing gameplay scripting API.

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
