# Task: Gamepad input + project-level input action map (P0.1)

Spec: `ai/specs/input-gamepad-action-map.md`. Roadmap: P0.1 in
`ai/workspace/game-capability-roadmap.md`.

## Goal

First-class gamepad support + a Godot-style project-level input action map, exposed to Lua
under the `Input` namespace.

## In scope

- Gamepad device layer: SDL3 gamepad open/close, button/axis events, rumble.
- `GamepadButton` / `GamepadAxis` enums (Lua tables via VBIND_ENUM).
- Project-level action map `res://input.actions.json` (deadzone + events), loaded at startup.
- Action query (held/pressed/released/axis) + runtime rebinding API.
- Lua bindings (regenerated), LuaLS stub, conformance, EN+CN docs (with example config +
  script). No demo files in `resources/`.

## Out of scope (follow-ons)

- Multi-gamepad / per-player device assignment (the `which` id is plumbed but only pad 0 is
  tracked).
- Editor "Input Map" Project Settings tab to author/save the JSON (hand-edit for now).
- Persisting runtime rebinds back to the project config.
- Unifying the XR action model into the same map.

## Implementation plan (done)

1. Core data: `input_action.hpp` (`InputActionEvent`/`InputAction`/`InputActionMap` +
   `parseInputActionMapJson`) and `input_action.cpp` (nlohmann + magic_enum).
2. Enums + state: `GamepadButton`/`GamepadAxis`/`GamepadButtonState` in `input_structs.hpp`.
3. Events: `GamepadButtonEvent`/`GamepadAxisEvent`/`GamepadConnectionEvent` in
   `input_events.hpp`; new `WindowEventType` entries + optional fields in `window_events.hpp`.
4. Platform: `os::Window::setGamepadRumble` (default no-op); `SDLWindow` open/close/translate/
   rumble (`SDL_INIT_GAMEPAD` already set).
5. Service + system: `IInputService` virtuals (VBIND_FN) + `InputSystem` impl (gamepad state,
   action map, query, rumble forward, per-frame clear of edge flags).
6. Wiring: `demo_app_host::onPostConfigure` calls `attachWindow` + loads
   `res://input.actions.json`.
7. Codegen: `extract_bindings.py` + `gen_lua.py` regenerate `script_input_binding.gen.cpp`
   and the LuaLS stub.
8. Docs: `doc/lua_scripting.md` + `doc/zh_CN/lua_scripting_CN.md` Input sections.
9. Example config + script documented in `doc/lua_scripting.md` (EN) +
   `doc/zh_CN/lua_scripting_CN.md` (CN); nothing demo-specific checked into `resources/`.

## Verification plan

- `xmake build -y vultra-app` compiles (PowerShell — see build-invocation memory).
- `xmake run test-lua-api-conformance` passes.
- Run the demo scene with a controller: stick moves a value, trigger reads `[0,1]`, South
  button is "Jump", a rebind takes effect, rumble fires.

## Status

Code + bindings + docs + demo implemented. Final compile/run verification pending a local
build (user runs builds). Conformance: new `Input.*` symbols emitted into the stub by the
generator; re-run conformance after build and burn down any `stub.missing` if present.
