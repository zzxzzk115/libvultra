# Spec: Navigation (Recast/Detour + NavAgent)

Status: implemented (P0.2 of `ai/workspace/game-capability-roadmap.md`).

## Intent

Bake a navmesh from level geometry and let agents path + avoid obstacles — unblocking
gameplay AI (enemies, click-to-move, patrols). Built on `recastnavigation` (Recast bake +
Detour query), wired into the engine as a subsystem/service and exposed to Lua as `Nav`.

## Public interface

### `INavigationService` (Lua `Nav`)

- `bake()` — build a solo-mesh navmesh from the world's static mesh geometry; `true` on success
- `isBaked()`
- `findPath(start, end)` -> ordered `Vec3` waypoints (empty if unreachable/not baked)
- `nearestPoint(point)` -> closest navmesh point
- `setAgentDestination(entity, target)`, `stopAgent(entity)`, `agentHasPath(entity)`
- `setDebugDrawEnabled(enabled)`, `debugDrawEnabled()`

### `NavAgentComponent` (Lua `Component.NavAgent`, fields via `NavAgent` usertype)

`radius`, `height`, `speed`, `stoppingDistance`, `targetPosition`, `hasTarget`,
`moving` (read-only). Reflected for scene serialization + editor inspector.

## Design / constraints

- `recastnavigation v1.6.0` added to `source/xmake.lua` (`add_requires` + private
  `add_packages`); it's in the project's xmake-repo.
- `NavigationSystem` (engine subsystem, `function/navigation`):
  - `bake()` gathers triangles from entities with `MeshComponent` (imported, non-builtin) +
    `TransformComponent` (vertices transformed by `worldMatrix`), runs the standard Recast
    solo-mesh pipeline (heightfield -> compact -> regions -> contours -> poly mesh -> detail),
    then `dtCreateNavMeshData` + `dtNavMesh`/`dtNavMeshQuery`. Detail-mesh edges are cached for
    debug draw.
  - `findPath` uses `findNearestPoly` + `findPath` + `findStraightPath`.
  - `onUpdate` steers each `NavAgentComponent`: recompute path to `targetPosition`, move toward
    the next waypoint by `speed`; feed `IPhysicsService::characterMove` when a
    `CharacterControllerComponent` is present, else move the transform; stop within
    `stoppingDistance`.
  - Debug draw via `dd::` (navmesh edges + active agent paths) when enabled.
- Services resolved lazily (init-order safe). Registered after Asset/Scene/Physics in
  `demo_app_host`.
- Lua `Nav` is a shim module (`script_nav_shim`, area `navigation`); `ScriptContext` gains
  `navService`. `findPath` returns a Lua array of `Vec3`.

## Out of scope (follow-ons)

- Builtin-primitive geometry in the bake (only imported meshes today).
- Tiled/streaming navmesh, off-mesh links, dynamic obstacles, crowd avoidance (DetourCrowd),
  area costs/flags, per-agent navmesh refit.
- Baking to a `.vasset` on disk (currently an in-memory runtime bake via `Nav.bake()`).
- Path caching (recomputed per frame per agent — fine for demo-scale).

## Acceptance

- An agent paths around an obstacle to a target in a demo; Lua can request a path; navmesh
  visualizes in the editor; conformance PASS; doc in `doc/lua_scripting.md` (EN+CN).
