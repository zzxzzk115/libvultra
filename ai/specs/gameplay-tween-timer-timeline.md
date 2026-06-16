# Spec: Gameplay utilities — tween/easing, timers, timeline

Status: implemented (P1.3 of `ai/workspace/game-capability-roadmap.md`).

## Intent

Game-feel/juice primitives usable from any script: tweening with easing, delayed/repeating
callbacks, and a minimal ordered sequencer.

## Public interface (Lua globals)

- `Ease`: `linear`, `quadIn/Out/InOut`, `cubicIn/Out/InOut`, `sineIn/Out/InOut`,
  `expoIn/Out`, `backOut`, `bounceOut` — `fun(t:number):number` mapping `[0,1]→[0,1]`.
- `Tween.to(obj, toFields, duration, opts?)` -> handle; `opts = { ease, onUpdate(t),
  onComplete }`. `Tween.cancel(handle)`.
- `Timer.after(seconds, fn)` / `Timer.every(seconds, fn)` (return `false` to stop) -> handle;
  `Timer.cancel(handle)`.
- `Timeline.new():wait(s):call(fn):start()` -> handle (chainable).

## Design / constraints

- **Pure-Lua runtime** (`script_tween_binding.cpp`, a `lua.script(...)` chunk like the
  coroutine runtime) — no per-binding IR/codegen. Tasks are global step closures stored in a
  hidden `__vultraTween`; `ScriptSystem::tickTweens(dt)` advances them once per frame (in
  `onUpdate`, after coroutines), and `__vultraTween.reset()` runs on teardown
  (`destroyAllInstances`).
- **Global, not per-entity** (unlike coroutines): callbacks must guard entity access; step
  closures are wrapped in `pcall` so one error doesn't stall the scheduler.
- Tween reads `dt` from the frame tick (not `Time.deltaTime` directly), interpolating each
  named numeric field of `obj` from its captured start value.
- Conformance: globals are PascalCase tables with camelCase members; documented in the
  hand-written section of `tools/lua-stubs/vultra.lua`.

## Out of scope (follow-ons)

- Tweening colors/quaternions/easing-by-name string; relative/yoyo/repeat-count tweens;
  per-entity ownership + auto-cancel on destroy; a full timeline/track editor.

## Acceptance

- A value tweens with an easing curve; a delayed callback fires once; an interval callback
  repeats; conformance PASS; doc in `doc/lua_scripting.md` (EN) + CN mirror.
