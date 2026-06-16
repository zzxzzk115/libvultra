# Spec: Gamepad input + project-level input action map

Status: implemented (P0.1 of `ai/workspace/game-capability-roadmap.md`).

## Intent

Give games first-class gamepad support and a Godot-style, **project-level** input action
map so gameplay code binds to named actions ("Jump", "MoveX") instead of raw keys, and so
players can rebind. Unifies keyboard, mouse, and gamepad under one `Input` Lua namespace
(the XR action model in `function/openxr/xr_input.hpp` stays separate but conceptually
aligned: named actions resolved from device inputs).

## Public interfaces

### Device layer (raw gamepad), engine core `vultra/core/input`

- `enum class GamepadButton` (SDL_Gamepad layout, Xbox names): `eSouth eEast eWest eNorth
  eBack eGuide eStart eLeftStick eRightStick eLeftShoulder eRightShoulder eDpadUp eDpadDown
  eDpadLeft eDpadRight`.
- `enum class GamepadAxis`: `eLeftX eLeftY eRightX eRightY eLeftTrigger eRightTrigger`.
  Sticks report `[-1,1]` (SDL sign: up/left negative), triggers `[0,1]`.
- `IInputService` additions (all `VBIND_FN`, namespace `Input`):
  - `isGamepadConnected()`, `isGamepadButtonHeld/Pressed/Released(GamepadButton)`,
    `gamepadAxis(GamepadAxis)`, `rumble(low, high, durationMs)`.

### Action layer (named actions)

- Config file `res://input.actions.json` (Godot-style), loaded at startup:
  ```json
  {
    "actions": {
      "Jump":  { "deadzone": 0.5, "events": [ {"key":"Space"}, {"gamepadButton":"South"} ] },
      "MoveX": { "events": [ {"gamepadAxis":"LeftX"}, {"key":"D","scale":1}, {"key":"A","scale":-1} ] }
    }
  }
  ```
  Event kinds: `key` | `mouseButton` | `gamepadButton` | `gamepadAxis`, each with an
  optional `scale` (sign/magnitude of the analog contribution). Names use the stripped enum
  forms (matching the Lua enum tables): `Space`, `South`, `LeftX`, `D`.
- `IInputService` additions (all `VBIND_FN`, namespace `Input`):
  - Query: `hasAction(name)`, `isActionHeld(name)` (currently down), `isActionPressed(name)`
    / `isActionReleased(name)` (this-frame edges, matching `isKeyHeld` vs `isKeyPressed`),
    `actionAxis(name)` (signed `[-1,1]`, deadzone applied).
  - Rebinding (in-memory): `clearActionEvents(name)`, `bindActionKey(name, KeyCode)`,
    `bindActionMouseButton(name, MouseCode)`, `bindActionGamepadButton(name, GamepadButton)`,
    `bindActionGamepadAxis(name, GamepadAxis, scale)`.
- Non-bound engine/app hooks on `IInputService`: `attachWindow(os::Window*)` (rumble path),
  `loadActionsFromJson(std::string_view)`.

## Constraints / design

- **Engine core owns the data + query + JSON parse** (`input_action.hpp/.cpp`, nlohmann +
  magic_enum). The app layer (`demo_app_host::onPostConfigure`) reads `res://input.actions.json`
  via `IAssetService::loadTextAssetSync` and feeds `loadActionsFromJson`, keeping core
  decoupled from the app-level `.vproject` parser.
- **Rumble** crosses the platform boundary via a new no-op-default virtual
  `os::Window::setGamepadRumble`; `SDLWindow` implements it on the first connected gamepad.
  `InputSystem` forwards through the window pointer set by `attachWindow`.
- **Single-player**: the SDL backend tracks the first connected gamepad only. Multi-pad is a
  follow-on (the `which` device id is already plumbed through the events).
- Naming follows `doc/lua_api_design.md`: camelCase functions, no `get` prefix
  (`actionAxis`/`gamepadAxis`, not `getAxis`).

## Acceptance

- A Lua script reads stick/trigger/button and a rebindable named action; rumble fires.
- `xmake run test-lua-api-conformance` passes (new symbols in the LuaLS stub).
- Documented in `doc/lua_scripting.md` (EN) + `doc/zh_CN/lua_scripting_CN.md` (CN).
- Example config + script live in `doc/lua_scripting.md` (EN) / `doc/zh_CN/lua_scripting_CN.md`
  (CN) — not checked into `resources/` (a project supplies its own `res://input.actions.json`).
