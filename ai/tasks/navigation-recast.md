# Task: Navigation (Recast/Detour + NavAgent) (P0.2)

Spec: `ai/specs/navigation-recast.md`. Roadmap: P0.2.

## Goal

Navmesh bake + pathfinding + NavAgent steering, exposed to Lua as `Nav`.

## In scope

- `recastnavigation` package integration (xmake).
- `NavigationSystem` + `INavigationService`: bake (Recast), query (Detour), agent steering,
  debug draw.
- `NavAgentComponent` (Lua component + reflection).
- Lua `Nav` shim, regenerated bindings, stub, conformance, EN+CN docs (with example script).

## Out of scope (follow-ons)

- Builtin-primitive geometry in the bake (imported meshes only).
- Tiled navmesh, off-mesh links, dynamic obstacles, DetourCrowd avoidance, area costs.
- On-disk `.vasset` navmesh; per-frame path caching.

## Implementation plan (done)

1. xmake: `add_requires("recastnavigation v1.6.0")` + private `add_packages`.
2. `nav_agent_component.hpp` (VBIND_USERTYPE + reflection in `scene_reflection.cpp`;
   `ScriptNavAgentRef` in `script_types.hpp`; `eNavAgent` token in `script_entity_shim`).
3. `navigation_service.hpp` (`INavigationService`).
4. `navigation/navigation_system.{hpp,cpp}` (Recast bake, Detour query, steering, debug draw).
5. Registered in `demo_app_host` after Asset/Scene/Physics; `navService` added to
   `ScriptContext` + populated in `ScriptSystem`.
6. Lua `Nav`: `script_nav_shim.{hpp,cpp}` + `script_navigation_binding.hpp` + aggregator call;
   manifest entries; codegen.
7. Docs: `doc/lua_scripting.md` + CN "Navigation", including a complete example script
   (kept in docs, not in `resources/`).

## Verification plan

- `xmake repo -u` (if package fetch fails) then `xmake build -y vultra-app` (PowerShell).
- `xmake run test-lua-api-conformance` passes.
- Demo scene with imported floor + obstacle: `Nav.bake()`, agent reaches a target around the
  obstacle; `Nav.setDebugDrawEnabled(true)` shows the navmesh + path.

## Status

Code + bindings + docs + demo implemented. Pending: first build will fetch the
`recastnavigation` package (run `xmake repo -u` if the index is stale). Final compile/run
verification pending a local build. Builtin-geometry bake + on-disk navmesh are follow-ons.
