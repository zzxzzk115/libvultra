# Task: Gameplay utilities — tween/timer/timeline (P1.3)

Spec: `ai/specs/gameplay-tween-timer-timeline.md`. Roadmap: P1.3.

## Goal

`Tween` / `Timer` / `Timeline` / `Ease` Lua globals, frame-driven, for game-feel.

## In scope

- Pure-Lua runtime + a per-frame tick hook in ScriptSystem.
- LuaLS stub entries (hand-written section); EN+CN docs; spec/task.

## Out of scope (follow-ons)

- Color/quat tweens, ease-by-name, yoyo/repeat-count, per-entity auto-cancel, timeline editor.

## Implementation plan (done)

1. `script_tween_binding.{hpp,cpp}`: `registerScriptTweenRuntime` injects the global
   `Ease`/`Tween`/`Timer`/`Timeline` tables + hidden `__vultraTween` { tick, reset }.
2. `script_binding.cpp`: include + call after `registerScriptCoroutineRuntime`.
3. `script_system.{hpp,cpp}`: `tickTweens(dt)` called in `onUpdate` after `tickCoroutines`;
   `__vultraTween.reset()` in `destroyAllInstances`.
4. `tools/lua-stubs/vultra.lua`: stub entries for the new globals (hand-written section).
5. Docs: lua_scripting.md "Tween, Timer, Timeline" (EN+CN).

## Verification plan

- `xmake build -y vultra` compiles.
- `xmake run test-lua-api-conformance` passes (new globals present in the stub, PascalCase
  tables + camelCase members).
- A tween moves a value with easing; `Timer.after` fires once; `Timer.every` repeats.

## Status

Code + stub + docs implemented. Final compile/run verification pending a local build.
