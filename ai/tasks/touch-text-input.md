# Task: Touch input + text-input stream (cross-cutting must-haves, Phase 1)

Spec: `ai/specs/touch-text-input.md`.

## In scope
- Touch + text events end-to-end (SDL → window → InputSystem → `Input.*`), Lua bindings, docs.

## Out of scope (follow-ons)
- IME UI, gestures, touch action-map source, pressure.

## Implementation plan (done)
1. `input_structs.hpp` `TouchPoint` (+ `<glm/vec2.hpp>`); `input_events.hpp` `TouchEvent`/
   `TextInputEvent`; `window_events.hpp` event types + optional fields.
2. `os/window.hpp` `startTextInput/stopTextInput`; `sdl_window.{hpp,cpp}` finger + text-input
   handling + impls.
3. `input_service.hpp` touch + text `VBIND_FN`; `input_system.{hpp,cpp}` state, handleEvent,
   clearStates, impls.
4. Codegen (extract+gen) -> `script_input_binding.gen.cpp` + stub.
5. Docs: `doc/lua_scripting.md` + CN "Touch"/"Text input".

## Verification
- `xmake build -y vultra-app`; `xmake run test-lua-api-conformance` PASS.
- Multi-touch + typed text readable from Lua; full device validation in the export plan.

## Status
Code + bindings + docs implemented; pending local build/conformance verification.
