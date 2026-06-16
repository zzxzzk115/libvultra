# Task: Physics joints / constraints (P0.3)

Spec: `ai/specs/physics-joints.md`. Roadmap: P0.3.

## Goal

Expose Jolt two-body constraints (hinge/fixed/distance/slider/point/cone) + motors via the
physics service and Lua `Physics` namespace.

## In scope

- `IPhysicsService` constraint methods + `PhysicsSystem` implementation (create/lock/register,
  motor drive, lifetime tied to bodies).
- Lua shim (`Physics.add*Constraint`, `removeConstraint`, `isConstraintValid`,
  `setConstraintMotor`), regenerated bindings, stub, conformance, EN+CN docs.

## Out of scope (follow-ons)

- Declarative `JointComponent` + editor inspector/gizmos.
- `VehicleConstraint` wrapper.
- Persisting joints in scenes (needs entity-reference UUID remap).

## Implementation plan (done)

1. `physics_service.hpp`: 6 `add*Constraint` + `removeConstraint`/`isConstraintValid`/
   `setConstraintMotor`.
2. `physics_system.{hpp,cpp}`: `ConstraintRecord`, `constraints` map, `createConstraint`
   template, per-type settings, motor casts; constraint cleanup wired into `destroyBody`/
   `removeStaleBodies`/`clearBodies`; Jolt constraint + `BodyLockMulti` includes.
3. `script_physics_shim.{hpp,cpp}`: `Physics.*` shim fns (bodyB optional => world anchor).
4. Codegen (`extract_bindings` + `gen_lua`) -> `script_physics_binding.gen.cpp` + stub.
5. Docs: `doc/lua_scripting.md` + `doc/zh_CN/lua_scripting_CN.md` "Constraints / joints".

## Verification plan

- `xmake build -y vultra-app` compiles (PowerShell).
- `xmake run test-lua-api-conformance` passes.
- Demo: hinge door swings (motor), rope keeps distance, cone-ragdoll falls; remove breaks it.

## Status

Code + bindings + docs implemented. Final compile/run verification pending a local build.
