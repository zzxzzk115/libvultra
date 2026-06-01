# AI Runtime RPC Bridge V1

## Scope

Implement the first Vultra AI runtime bridge through Runtime MCP:

- add `--rpc` and `--render-mode`;
- keep MCP active in runtime mode;
- add batch simulation MCP tools;
- add render capture MCP wrappers;
- add a small standard-library Python client under `tools/python/vultra_client`;
- record verification and handoff notes.

## Acceptance Criteria

- `tools/list` includes `vultra.sim.*` and `vultra.render.capture_*`.
- `vultra.sim.get_state_batch` returns transform/status/rigid-body data.
- `vultra.sim.set_state_batch` writes transform, status, and velocity data.
- `vultra.sim.apply_actions_batch` supports `force`, `impulse`,
  `target_velocity`, and `teleport`.
- `vultra.sim.step` advances by a bounded batch of normal engine frames and
  returns one state payload.
- `vultra.render.capture_rgb` and `vultra.render.capture_depth` reject
  `render-mode=none`.
- No pybind11/CPython dependency is introduced.

## Lua Parity

Not applicable. This task adds external runtime automation tools and does not
change player-facing Lua gameplay scripting.
