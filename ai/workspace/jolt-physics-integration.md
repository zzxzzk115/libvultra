# Jolt Physics Integration Handoff

## Summary

- Added `function/physics` with a Jolt-backed `PhysicsSystem` and `IPhysicsService`.
- Added editor-facing physics components:
  - `RigidBodyComponent`
  - `BoxShapeComponent`
  - `SphereShapeComponent`
  - `CapsuleShapeComponent`
- Registered physics components for EnTT scene reflection, `.vscn` serialization, Inspector editing, Add Component, component ordering/removal, hierarchy icons, and creation presets.
- Inspector Add Component is grouped by category. Adding box/sphere/capsule shapes estimates default shape parameters from the entity mesh bounds when a builtin mesh or loaded CPU mesh is available.
- Wired Jolt jobs through the engine `IJobService` shared `vtask::Scheduler`; no `JPH::JobSystemThreadPool` is used.
- Physics simulation now follows editor playback state: Play runs, Pause holds, Step advances one fixed tick, Stop disables simulation and lets the editor restore its play-mode snapshot.
- Physics body lifetime is now tied to playback/enabled state: Stop and Disable release all Jolt bodies, Pause keeps bodies but still removes stale bodies when ECS components/entities are deleted.
- Physics fixed stepping now uses the engine `ITimingService` fixed-step count when available, so low render FPS catches up with multiple 1/60 steps instead of the physics system maintaining a separate render-driven accumulator.
- Added `test-physics-jolt`, which builds a floor, dynamic sphere, and capsule through the engine ECS and verifies fixed-step simulation updates transforms.

## Verification

- `xmake build -y test-physics-jolt` passed.
- `xmake run test-physics-jolt` passed, including stopped, paused, single-step, and running simulation checks.
- `xmake build -y vultra-app` passed after editor playback wiring.
- `xmake build -y vultra-app` passed after categorized Add Component and mesh-bound shape fitting.
- `xmake build -y test-physics-jolt`, `xmake run test-physics-jolt`, and `xmake build -y vultra-app` passed after fixed-step catch-up and body lifetime cleanup.

## Notes

- Current state is a first runnable integration, not a complete physics feature set.
- v1 supports one primitive collider per entity. If multiple shape components exist, runtime priority is box, sphere, then capsule.
- Dynamic bodies write simulation results back into local `TransformComponent` position/rotation.
- Static and kinematic bodies push ECS transforms into Jolt before stepping.
- Jolt global registration is reference-counted by `PhysicsSystem`; shutdown releases it only when this system acquired it.
- A remaining deeper validation gap is external leak tooling. The current checks assert engine-visible body counts and shutdown paths, but do not run ASan/LSan or a platform memory profiler.
- Mesh/convex/compound colliders, joints, contacts/events, character controller, and Lua bindings are left for later tasks.

## Follow-up Work

- Add collision/contact event dispatch and query APIs.
- Add mesh, convex, compound, trigger-only, and offset/rotated collider support.
- Add explicit physics material assets or combine policy controls for friction/restitution.
- Add debug draw for collider shapes and broadphase bodies.
- Add scene/runtime reset semantics beyond editor snapshot restore, including body recreation when scenes hot-reload.
- Add Lua bindings for raycast, overlap, velocity, forces/impulses, and body state.
- Add richer tests for serialization round-trip, editor play/stop restoration, kinematic bodies, sensors, and restitution behavior.
- Add a leak/profiler test pass for repeated Play/Stop, scene reload, component removal, and engine shutdown cycles.
