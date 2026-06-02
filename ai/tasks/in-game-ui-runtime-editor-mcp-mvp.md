# In-Game UI Runtime Editor MCP MVP

## Goal

Implement screen-space UI authoring and runtime interaction enough for MCP and
editor workflows to create, inspect, save, reload, and manipulate UI entities.

## Implemented Surface

- ECS components: Canvas, RectTransform, UI Panel, UI Image, UI Text, UI Button,
  and UI Layout.
- Scene reflection and `.vscn` parse/write support for UI components and
  `glm::vec2`.
- Inspector UI category, component labels, texture picker support for UI Image,
  and Transform hiding when RectTransform exists.
- Shared editor command/MCP component kinds and UI entity templates.
- Transform intent forwarding to RectTransform for editor commands and Lua.
- Runtime `UiSystem` service for hierarchy rect resolution, simple layout,
  pointer raycast, hover/press/click state, and camera/game input suppression
  while UI captures pointer.
- Scene View `3D / 2D UI` toggle with a pixel-space RectTransform overlay and
  move/rotate/scale edits for selected UI entities.
- Render graph `UiOverlayPass` for panels, images, and buttons. Text rendering
  is intentionally skipped in this pass for the MVP.
- Lua bindings for `entity.rectTransform`, `entity.uiButton`, and global `UI`
  pointer queries.
- Builtin and project default render graphs route final scene color through
  `UiOverlay` before `FinalComposition`.

## Verification

- Build target: `xmake build -y vultra-app`.
- Runtime MCP smoke should start with:
  `xmake run vultra-app --editor --mcp --project example.vproject --no-xr`
- MCP test flow:
  create `ui_canvas`, add `ui_panel`, `ui_text`, `ui_image`, and `ui_button`,
  set RectTransform pixel fields, save/reload scene, then verify
  `scene.update_component` with `component_kind=transform` updates
  RectTransform pixel data on UI entities.
- Screenshot verification: create a Canvas with `scaleMode=Scale With Screen`
  and confirm panels/images/buttons are visible in Game View at sub-reference
  render target sizes.

## Known Follow-Up

Text glyph rendering, nine-slice sprites, richer controls, IME/input text, and
world-space/XR UI remain follow-up work.
