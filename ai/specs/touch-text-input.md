# Spec: Touch input + text-input stream

Status: implemented (cross-cutting must-haves, Phase 1 — see
`C:/Users/Administrator/.claude/plans/valiant-plotting-oasis.md`).

## Intent

Give games multi-touch input (mobile/tablet/touch-laptop/web — the Android target already
exists) and an OS text-input stream (for editable text fields / chat).

## Public interface (`IInputService`, Lua `Input`)

- Touch (positions in window pixels): `touchCount()`, `touchId(i)`, `touchPosition(i)`,
  `touchDelta(i)`, `isTouchPressed(i)` (began this frame), `isTouchReleased(i)` (ended).
- Text: `startTextInput()`, `stopTextInput()`, `textInput()` (UTF-8 typed this frame).

## Design / constraints

- Mirrors the gamepad plumbing: `TouchPoint`/`TextInputEvent` (input_structs/input_events),
  `WindowEventType::eTouch{Down,Up,Motion}/eTextInput` + optional `WindowEvent` fields; SDL3
  `SDL_EVENT_FINGER_*` (normalized → window px) and `SDL_EVENT_TEXT_INPUT` translated in
  `sdl_window.cpp`. `os::Window::startTextInput/stopTextInput` (no-op default) → SDLWindow calls
  `SDL_StartTextInput/StopTextInput`; `InputSystem` reaches the window via the existing
  `attachWindow` pointer.
- `InputSystem` stores `std::vector<TouchPoint> m_Touches` (down/up edges + delta) + a per-frame
  `m_TextInput` string; `clearStates()` drops ended touches and resets the buffer each frame.
- SDL's default touch→mouse synthesis stays on, so existing pointer UI works on touch; this API
  adds multi-touch/gestures + the text stream the input field needs.

## Out of scope (follow-ons)

- IME composition/candidate UI; gesture recognizers (pinch/swipe); touch as an action-map
  source; per-touch pressure.

## Acceptance

- Lua reads multi-touch points + typed text; conformance PASS; docs in `doc/lua_scripting.md`
  (EN+CN). Real-device touch validation lands with the Web/Android export plan.
