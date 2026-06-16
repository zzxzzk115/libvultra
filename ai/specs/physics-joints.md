# Spec: Physics joints / constraints

Status: implemented (P0.3 of `ai/workspace/game-capability-roadmap.md`).

## Intent

Expose Jolt's two-body constraints (already in the engine's Jolt dependency) so games can
build doors/levers, ropes/chains, vehicles-adjacent rigs, and ragdolls, plus motor-driven
joints. Runtime, entity-keyed API — no scene-level entity-reference serialization required.

## Public interface (`IPhysicsService`, Lua `Physics`)

Six constraint kinds; `bodyB == entt::null` (Lua `nil`) anchors `bodyA` to the world.
`point`/`axis` are world-space at creation; angles degrees, distances meters. Each returns an
opaque `uint32_t` id (`0` on failure).

- `addFixedConstraint(bodyA, bodyB)`
- `addPointConstraint(bodyA, bodyB, point)`
- `addDistanceConstraint(bodyA, bodyB, minDistance, maxDistance)` — anchors at each body COM
- `addHingeConstraint(bodyA, bodyB, point, axis, minAngleDegrees, maxAngleDegrees)`
- `addSliderConstraint(bodyA, bodyB, point, axis, minDistance, maxDistance)`
- `addConeConstraint(bodyA, bodyB, point, twistAxis, halfAngleDegrees)`
- `removeConstraint(id)`, `isConstraintValid(id)`
- `setConstraintMotor(id, enabled, targetVelocity, maxForce)` — hinge motor angular (deg/s),
  slider motor linear (m/s)

Lua: same names under `Physics.*`, with `bodyB` an optional `ScriptEntity` (omit/`nil` =
world). `findPath`-style table building not needed.

## Design / constraints

- Constraints live in `PhysicsSystem::Impl::constraints` (`unordered_map<uint32_t,
  ConstraintRecord>` holding a `JPH::Ref<JPH::Constraint>` + the two entities + captured
  `BodyID`s + type). `createConstraint` is a private template that locks the body/bodies
  (`BodyLockWrite` / `BodyLockMultiWrite`), runs a per-type settings lambda, `AddConstraint`s,
  and registers the record. Bodies are created on demand (`ensureBody`).
- **Jolt ordering rule:** constraints must be removed before the bodies they reference.
  `destroyBody`, `removeStaleBodies`, and `clearBodies` all call `removeConstraintsForEntity`
  / `clearConstraints` first, so a recreated/destroyed body never leaves a dangling
  constraint.
- Motors: hinge -> `HingeConstraint::SetMotorState/SetTargetAngularVelocity` + torque limit;
  slider -> `SliderConstraint::SetMotorState/SetTargetVelocity` + force limit. Other kinds
  return false.
- Headers added to `physics_system.cpp`: `Jolt/Physics/Constraints/*` + `BodyLockMulti.h`.

## Out of scope (follow-ons)

- A declarative `JointComponent` + editor gizmos (the card's "nice-to-have"). Current API is
  runtime/Lua-driven; entity-reference scene serialization (UUID remap) is the blocker.
- A dedicated `VehicleConstraint` wrapper (wheels/suspension/steering).

## Acceptance

- A hinge door + a distance/rope constraint work in a demo; a cone-constraint ragdoll falls
  believably; Lua creates/breaks/drives a joint; conformance PASS; doc in `doc/lua_scripting.md`
  (EN+CN).
