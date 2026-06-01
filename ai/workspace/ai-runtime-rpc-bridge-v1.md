# AI Runtime RPC Bridge V1 Workspace

## Implementation Notes

- Added `--rpc` as an alias for Runtime MCP and `--render-mode` launch state.
- Runtime mode now pumps the same Runtime MCP server used by editor/launcher.
- Added batch simulation tools for reset, step, state read/write, and actions.
- Added render capture wrappers that fail explicitly when render mode is none.
- Added a zero-dependency Python client package under `tools/python`.
- Recorded the preferred long-term architecture: RPC as control plane, then
  JSON -> binary payload -> shared memory -> CUDA/GPU interop as data-plane
  upgrades when profiling requires them.

## Verification

- `git diff --check`: passed.
- `xmake build -y vultra-app`: passed.
- Started hidden editor with `--editor --rpc --project example.vproject
  --render-mode none --no-xr --mcp-port 8858`.
- Runtime MCP `initialize`: passed.
- Runtime MCP `tools/list`: passed and included `vultra.sim.*` plus
  `vultra.render.capture_*`.
- `vultra.runtime.status`: passed and returned `renderMode=none`.
- `vultra.sim.get_state_batch`: passed and returned live entity state.
- `vultra.render.capture_rgb`: returned the expected error in
  `render-mode=none`.
- Started a second hidden editor on port 8859 and smoke-tested
  `vultra.sim.step(frames=1)`: passed.
- `vultra.editor.quit`: passed for both smoke-test processes.
- Created `.venv` with Python 3.13.2 from
  `%LOCALAPPDATA%/Programs/Python/Python313/python.exe`.
- `.venv/Scripts/python.exe` works, but `ensurepip` failed during creation and
  pip is not installed in the venv. This is fine for `vultra_client` because it
  has no third-party runtime dependencies.
- Python client import smoke passed with `PYTHONPATH=tools/python`.
- Started a third hidden editor on port 8860 and tested the Python client
  against live RPC:
  - `sim.status()` returned `renderMode=none`.
  - `sim.get_state_batch(limit=1)` returned `ok=True`.
  - `sim.step(frames=1, limit=1)` returned `ok=True`.
  - `sim.capture_rgb()` raised the expected `VultraRpcError` for
    `render-mode=none`.
- Ran a simple live physics simulation on the default 8848 port:
  - Created an empty scene through MCP.
  - Added a static box floor and a dynamic sphere through scene/component tools.
  - `sim.step(frames=120)` moved the sphere from `y=4.0` to about `y=3.973`
    with `physicsBodies=2`.
  - `sim.set_state_batch` teleported the sphere to `y=3.0`.
  - `sim.apply_actions_batch` with `target_velocity` plus `impulse`, followed
    by `sim.step(frames=90)`, moved the sphere down to about `y=2.892`.
  - `vultra.editor.quit` stopped the process and 8848 was no longer listening.
