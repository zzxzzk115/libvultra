# Spec: UI widgets (input field, dropdown, scroll view)

## Goal

Extend the retained UI component system with the three widgets required for real menus and
forms, beyond the existing Button/Toggle/Slider/Image/Text/Panel/ProgressBar/Layout:

- **UiInputField** — single-line editable text field (focus, caret, keyboard editing, submit).
- **UiDropdown** — header + expandable option list, single selection.
- **UiScrollView** — clipped viewport that scrolls oversized content.

All three are authorable in the editor (inspector + serialization), scriptable from Lua, and
localizable (component labels in all four catalogs).

## Behaviour

### UiInputField
- Fields: `enabled`, `interactable`, `text`, `placeholder`, `maxLength`, `fontSizePx`, `font`,
  `normalColor`, `focusedColor`, `textColor`, `placeholderColor`, `caretColor`; runtime-only
  `caret`, `focused`, `submitted`.
- The `UiSystem` owns focus: click inside focuses (and calls `Input.startTextInput()`),
  click elsewhere / Escape blurs (`Input.stopTextInput()`). While focused it appends
  `Input.textInput()` (respecting `maxLength`), handles Backspace/Delete, Left/Right/Home/End
  caret movement, and Enter/KP-Enter → `submitted` + `Submit` event. Any text change pushes a
  `ValueChanged` event.
- Render: background tinted by focus state; placeholder or text with horizontal scroll so the
  caret stays visible; glyphs past the rect are culled (CPU-side clip clamp, no shader change);
  caret quad at the caret advance.
- Lua: `text`, `placeholder`, `interactable` (read/write), `focused`/`submitted` (read-only),
  `onValueChanged`/`onSubmit` signals.

### UiDropdown
- Fields: `enabled`, `interactable`, `options` (string list), `selectedIndex`, `fontSizePx`,
  `font`, colors; runtime-only `expanded`.
- Header click toggles `expanded`; while expanded the option rows are hit-tested in-component
  (no child entities). Selecting sets `selectedIndex`, collapses, pushes `ValueChanged`.
- Render: header shows `options[selectedIndex]`; the expanded list is an overlay (high sort
  order) with hovered/selected highlight.
- Lua: `selectedIndex`, `options`, `interactable`, `expanded` (read-only), `onValueChanged`.

### UiScrollView
- Fields: `enabled`, `contentSizePx`, `scrollPx`, `horizontal`, `vertical`, `scrollSpeedPx`,
  `backgroundColor`.
- Wheel (and touch drag) over the view adjusts `scrollPx`, clamped to `contentSize - viewport`;
  `rebuild` offsets descendant resolved rects by `-scrollPx`.
- Render: descendants are clip-clamped to the scroll-view bounds (CPU-side).
- Lua: `scrollPx`, `contentSizePx`, `horizontal`, `vertical`.

## Shared infra
- `UiEventType` += `ValueChanged`, `Submit` (with `signalMatchesEvent` + `ScriptUiEvent`).
- `UiSystem` focus tracking via `m_FocusedEntity`.
- Clipping is CPU-side (cull/clamp glyphs and child rects at emission) — no UI-shader change.

## Out of scope
- Multi-line text areas, rich text, per-option dropdown styling, inertial/elastic scrolling.
