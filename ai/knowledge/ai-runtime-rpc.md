# AI Runtime RPC Knowledge

Stable facts:

- Vultra's default AI tooling bridge is out-of-process Runtime MCP/RPC, not
  pybind11.
- `--rpc` is an alias for `--mcp`.
- `--render-mode=visible|offscreen|none` is recorded in Runtime MCP status.
- Python tools should use batched simulation calls and avoid per-entity small
  RPC loops.
- RPC is the long-term control plane; JSON, binary payloads, shared memory, and
  CUDA/GPU interop are progressively stronger data-plane options.
- The ideal training loop is: Python writes batched actions, RPC steps K frames,
  Vultra fills batched observations, Python reads them into PyTorch.
- Use shared memory first for large state observations; reserve CUDA/GPU interop
  for visual/tensor-heavy workloads after profiling.
- The lightweight Python client lives at `tools/python/vultra_client`.
- Render capture is unavailable in `render-mode=none`.
- This bridge does not require Lua documentation updates unless a later task
  exposes the same behavior as player-facing gameplay scripting.
