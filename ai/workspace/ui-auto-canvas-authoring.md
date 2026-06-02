# UI Auto Canvas Authoring

## Summary

- Continued the UI authoring plan after the 2D ImGuizmo spike.
- `scene.add_entity` now auto-creates a default `Canvas` when creating
  `ui_panel`, `ui_text`, `ui_image`, or `ui_button` without an explicit parent
  and no Canvas exists in the scene.
- If a Canvas already exists, new UI elements without an explicit parent attach
  to that Canvas.
- `scene.add_entity` returns `parent` for created child entities and returns
  `createdCanvas` / `createdCanvasUuid` when the command auto-created a Canvas.

## Verification

- `xmake build -y vultra-app` passed.
- Runtime MCP validation passed:
  - Created temporary scene `res://__mcp_ui_auto_canvas_temp.vscn`.
  - Called `vultra.scene.add_entity` with `entity_kind: "ui_panel"` and no
    parent.
  - The result included `createdCanvas`, `createdCanvasUuid`, and `parent`
    pointing at the new Canvas entity.
  - Screenshot captured to `build/.tmp/ui-auto-canvas-panel.png`.
  - Added a second `ui_text` with no parent; it reused the existing Canvas and
    returned the same `parent` without creating another Canvas.

## Handoff

- This completes the "default add UI content to Canvas, auto-create Canvas when
  missing" slice.
- Remaining larger slices from the original plan include FreeType-backed text
  rendering, more UI controls, local/global RectTransform semantics, and a more
  visual RectTransform Inspector.
