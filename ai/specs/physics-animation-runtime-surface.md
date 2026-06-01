# Physics And Animation Runtime Surface

## Goal

Expose common gameplay-facing physics and skeletal animation features through
engine services first, then Lua/MCP/editor bindings. UI and automation should
drive the same service/component model.

## Implemented Baseline

Physics service:

- body state: `hasBody`, velocity get/set, `activate`, `setPosition`
- forces: `addForce`, `addImpulse`
- queries: `raycast`, `overlapSphere`, `overlapBox`
- gameplay contact snapshot: `contactPairs`

Animation service:

- single-clip animator playback: `play`, `pause`, `stop`
- animator edits: set animation, time, normalized time, speed, loop
- queries: playback state, skeleton joint count, animation duration

Lua bindings mirror these service calls through `Physics`, `Animation`,
`RigidBody`, and `Animator`.

## Remaining Native Jolt Work

The next physics slice should replace approximate gameplay queries with native
Jolt query/listener plumbing where needed:

- contact listener with begin/persist/end events
- ray cast, shape cast, and broadphase/narrowphase filters
- overlap queries using Jolt collision collectors
- collision layers and masks beyond the current object layer integer
- mesh, convex hull, compound, and heightfield shapes
- constraints/joints: fixed, distance, hinge, slider, cone/twist, six-DOF
- character controller component/service
- trigger volumes and event dispatch to scripts

## Remaining Ozz Animation Work

The next animation slices should add controller data instead of overloading the
single `AnimatorComponent`:

- multiple clips per animator and named clip lookup
- cross-fade/blending and additive layers
- bone masks and partial-body playback
- animation events/notifies
- root motion extraction/application
- state machine or blend tree asset/component
- retargeting metadata
- inverse kinematics hooks
- editor preview and debugging for controller state

## Constraints

- Lua bindings must stay thin and call services/components.
- Service methods must tolerate missing services/assets/entities and return
  explicit failure values.
- Renderer-facing skin palettes remain owned by the animation system.
