# Task: Animation keyframe events (P1.1 — events first)

Spec: `ai/specs/animation-events.md`. Roadmap: P1.1 (step 1 of events → blend trees → root motion).

## Goal

Author keyframe events on animator-graph states and dispatch them to Lua
`OnAnimationEvent(self, name)`.

## In scope

- `Event` struct + `State::events`; JSON (de)serialization.
- Frame-to-frame event-crossing detection (loop + transition aware) in the animation system.
- Direct dispatch to the entity script via `IScriptService::dispatchAnimationEvent`.
- EN+CN docs; spec/task.

## Out of scope (follow-ons)

- Blend trees, root motion (rest of P1.1).
- Single-clip events; incoming-state events during cross-fade.

## Implementation plan (done)

1. `animator_graph.hpp`: `Event` struct + `State::events`.
2. `animator_graph.cpp`: parse/write `events` in graphFromJson/graphToJson (`time` fallback).
3. `animation_system.{hpp,cpp}`: `ControllerRuntime::eventScanState/eventScanNorm`,
   `IScriptService* m_Scripts`, event-crossing detection in `updateGraphAnimator`.
4. `script_service.hpp`: non-bound `dispatchAnimationEvent`.
5. `script_instance.hpp` + `script_system.{hpp,cpp}`: `onAnimationEvent` cached at load +
   `dispatchAnimationEvent` impl (mirrors `dispatchContactEvent`).
6. Docs: lifecycle "Animation Event Callback" + Animation "Keyframe events" (EN+CN).

## Verification plan

- `xmake build -y vultra` compiles (PowerShell).
- `xmake run test-lua-api-conformance` passes (no new bindings, so no new symbols).
- Author an `events` entry on a looping graph state; an entity script's `OnAnimationEvent`
  prints on each crossing.

## Status

Code + docs implemented. No Lua bindings changed (callback is C++→Lua), so conformance is
unaffected. Final compile/run verification pending a local build.
