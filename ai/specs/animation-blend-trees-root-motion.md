# Spec: Animation blend trees + root motion

Status: implemented (P1.1 steps 2 & 3 of `ai/workspace/game-capability-roadmap.md`, after
keyframe events — see `animation-events.md`). Completes P1.1.

## Intent

- **Blend trees (1D):** a graph state blends N clips by a float parameter (idle/walk/run by
  speed) for smooth directional/locomotion movement.
- **Root motion:** the playing clip's root-joint horizontal translation drives the entity
  transform instead of sliding the mesh in place.

## Public interface

- Authoring (`.vanimgraph.json`): a state may carry `blendTree: { parameter, entries:
  [{animation, threshold}] }` (kept sorted by threshold). Non-empty `entries` overrides the
  state's single `animation`.
- C++: `animator_graph::BlendEntry`/`BlendTree` + `State::blendTree`, `State::hasBlendTree()`.
- Component: `AnimatorComponent::applyRootMotion` (graph mode; reflected/serialized).
- Lua: drive the blend parameter with the existing `Animation.setFloat(entity, name, value)`.

## Design / constraints

- **Blend trees** (`AnimationSystem`): a state's effective duration is the phase-matched
  weighted average of active entries' durations, so all clips stay synchronized; every active
  entry is sampled at the same normalized ratio and combined with `ozz BlendingJob` (N layers),
  weights from a 1D piecewise-linear lookup that sums to 1. Single dominant entry skips the
  blend. Events/normalized-time flow unchanged (they use the effective duration).
- **Root motion** (graph mode, opt-in): after building the model palette, the root joint's
  horizontal (xz) model delta moves `TransformComponent.position` (rotated by the entity's
  rotation); the accumulated root xz is then subtracted from every palette matrix so the mesh
  stays centered. Skipped during cross-fades and on the loop-wrap frame; root tracking resets
  on state change. Joint 0 is assumed to be the skeleton root.

## Out of scope (follow-ons)

- 2D blend trees (directional), blend-space editor UI; per-clip playback-speed sync modes.
- Root motion for single-clip animators; vertical/rotational root motion; feeding a
  `CharacterControllerComponent` (currently drives the transform directly); foot-locking/IK.

## Acceptance

- A 1D locomotion blend tree blends idle/walk/run by a `speed` parameter; (stretch) root
  motion drives a clip; conformance PASS (no new bindings beyond Scene persistence); docs in
  `doc/lua_scripting.md` (EN) + CN mirror.
