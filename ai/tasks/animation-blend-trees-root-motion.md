# Task: Animation blend trees + root motion (P1.1 steps 2 & 3)

Spec: `ai/specs/animation-blend-trees-root-motion.md`. Roadmap: P1.1 (after events).

## Goal

1D blend trees + opt-in graph-mode root motion, completing P1.1.

## In scope

- `BlendEntry`/`BlendTree` + `State::blendTree` (+ JSON round-trip, sorted by threshold).
- Blend-tree sampling (phase-matched, N-layer `BlendingJob`) + blend-aware state duration.
- `AnimatorComponent::applyRootMotion` (+ reflection) and root-motion application.
- EN+CN docs; spec/task.

## Out of scope (follow-ons)

- 2D blend trees + editor UI; single-clip root motion; vertical/rotational root motion;
  character-controller-driven root motion; IK/foot-locking.

## Implementation plan (done)

1. `animator_graph.{hpp,cpp}`: `BlendEntry`/`BlendTree`, `State::blendTree`/`hasBlendTree`,
   JSON parse/write (sorted entries).
2. `animation_system.cpp`: `blendWeights1D`, `stateDuration` (blend-aware) used by `advance`,
   `sampleClip` + blend-tree branch in `sampleInto`; root-motion block after palette build,
   gated by `applyRootMotion` + not-transitioning + not-looped; `ControllerRuntime` root state.
3. `animator_component.hpp` + `scene_reflection.cpp`: `applyRootMotion`.
4. Docs: lua_scripting.md "Blend trees" + "Root motion" (EN+CN).

## Verification plan

- `xmake build -y vultra` compiles.
- `xmake run test-lua-api-conformance` passes (no new animation bindings).
- A graph state with a `speed` blend tree blends idle/walk/run; an `applyRootMotion` clip moves
  the entity while the mesh stays centered.

## Status

Code + docs implemented. Final compile/run verification pending a local build. Root motion is
opt-in (default off) so existing animations are unaffected.
