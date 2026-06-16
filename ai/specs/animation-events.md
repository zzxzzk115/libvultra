# Spec: Animation keyframe events

Status: implemented (P1.1 step 1 — "events first" — of `ai/workspace/game-capability-roadmap.md`).
Blend trees and root motion are the remaining P1.1 steps (separate specs/tasks).

## Intent

Fire named events at authored times within an animator-graph clip and dispatch them to the
entity's Lua `OnAnimationEvent(self, name)` — the foundation for footstep SFX, hit/damage
frames, particle spawns, etc.

## Public interface

- **Authoring** (`.vanimgraph.json`): each state gains an optional `events` array of
  `{ name, normalizedTime }` (`normalizedTime` in `[0,1]`). Round-trips through
  `animator_graph.cpp` (graphFromJson/graphToJson); `time` is accepted as a fallback key.
- **C++**: `animator_graph::Event { std::string name; float normalizedTime; }` +
  `std::vector<Event> State::events`.
- **Lua callback**: `function OnAnimationEvent(self, name) ... end` on the entity script.

## Design / constraints

- **Detection** lives in `AnimationSystem::updateGraphAnimator` (graph mode only — single-clip
  `mode 0` has no states). Each frame, after advancing the current state, events whose
  `normalizedTime` falls in `(prevNorm, curNorm]` fire; loop wrap (`curNorm < prevNorm`) fires
  the `(prev,1] ∪ [0,cur]` range. A per-controller cursor (`eventScanState`/`eventScanNorm`)
  tracks the last scan; on a state switch the cursor resets to the current point so entering a
  state never replays past events.
- **Dispatch** is direct, via a new non-bound `IScriptService::dispatchAnimationEvent(entity,
  name)` (implemented by `ScriptSystem`, mirroring `dispatchContactEvent`). `AnimationSystem`
  registers after `ScriptSystem`, so the callback runs the same frame (just after `OnUpdate`).
  No Lua-facing API was added, so conformance is unaffected.
- `ScriptInstance` gains `onAnimationEvent`, cached at script load like the other callbacks.

## Out of scope (P1.1 follow-ons)

- Blend trees (1D/2D) feeding ozz `BlendingJob` with N layers.
- Root motion extraction → transform / character controller.
- Events on single-clip animators; events during cross-fade on the *incoming* state (they
  fire once it becomes current); sub-frame ordering of multiple events.

## Acceptance

- A Lua `OnAnimationEvent` fires on a footstep event authored in a graph state; conformance
  PASS (no new bindings); doc in `doc/lua_scripting.md` (EN) + `doc/zh_CN/lua_scripting_CN.md`
  (CN).
