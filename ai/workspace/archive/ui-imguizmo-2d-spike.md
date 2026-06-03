# UI ImGuizmo 2D Spike

## Summary

- Added and validated an editor-only ImGuizmo spike for selected
  `RectTransformComponent` entities in Scene View `2D` mode, then removed the
  2D ImGuizmo path after visual testing showed it is not a good fit for UI
  authoring.
- Scene View `2D` now uses an editor-only UI authoring canvas instead of the
  scene render target. This prevents skybox, Sponza, and other non-UI scene
  content from showing behind UI elements while authoring.
- Replaced the 2D ImGuizmo path with self-drawn RectTransform handles:
  `Select` is the mixed/default operation, dragging the rect moves it, the
  rotation handle edits `rotationDegrees`, and the scale handle edits `scale`.
- Follow-up correction aligned the editor 2D overlay/gizmo with the runtime UI
  pass: Canvas preview uses the render target top-left origin and the same
  RectTransform size/scale formula as UI cooking.
- Second follow-up accounts for the Scene View render target's vertical texture
  display flip: Canvas border, RectTransform overlay, and ImGuizmo pivot now map
  through the same flipped display-space conversion.
- Added `vultra.editor.scene_view` MCP tool for reliable automated switching
  between Scene View `2d`/`3d` modes and Select/Move/Rotate/Scale/Rect/Transform
  tools during editor tests.
- Corrected an early false positive: the first screenshot showed the legacy
  self-drawn overlay axes, not ImGuizmo. The legacy axes now only draw on the
  old overlay edit path; the 2D spike draws ImGuizmo from the selected
  RectTransform overlay branch so screenshots are unambiguous.
- Scene View `2D` mode now uses a fixed orthographic editor camera instead of
  the normal perspective fly camera. 3D fly controls, scene picking, and the
  view manipulator stay on the `3D` path.
- Scene View `2D` camera now forces solid color clear mode so the skybox does
  not render behind the Canvas authoring surface.
- Scene View auto-syncs on entity selection changes: selecting a Canvas or
  RectTransform entity switches to `2D` + Transform; selecting a non-UI entity
  switches back to `3D`. Explicit MCP/UI Scene View mode requests still win for
  that frame.
- Scene View toolbar was moved out of the viewport overlay into a fixed
  top-of-window toolbar, matching Game View's layout pattern. The viewport no
  longer has to special-case toolbar hit testing.
- Scene View tool shortcuts now follow the Unity-style sequence:
  `Q` Select, `W` Move, `E` Rotate, `R` Scale, `T` Rect, `Y` Transform.
  Selecting UI now defaults to `2D` + Transform.
- Self-drawn UI handles now latch the drag operation on mouse-down. Resize,
  rotate, and move no longer re-run hit testing every frame, so dragging out of
  a handle cannot accidentally switch into another operation.
- The selected RectTransform overlay now uses eight resize handles plus a
  rotation handle, closer to the Unity-style Rect tool reference.
- Inspector uses a custom Rect Transform editor instead of raw reflected
  fields, with an anchor preset preview and grouped Position/Size/Anchors/Pivot/
  Rotation/Scale controls.

## Verification

- `xmake build -y vultra-app` passed after the initial spike.
- `xmake build -y vultra-app` passed after the overlay/gizmo alignment fix.
- `xmake build -y vultra-app` passed after the vertical display-space flip fix.
- `xmake build -y vultra-app` passed after adding MCP Scene View tool control
  and moving ImGuizmo into the selected RectTransform overlay branch.
- Runtime MCP validation passed with a temporary unsaved scene:
  `MCP Canvas` + `MCP Panel`, Scene View switched by
  `vultra.editor.scene_view { mode: "2d", tool: "move" }`, screenshot captured
  to `build/.tmp/ui-gizmo-capture-2d-real-imguizmo-v8.png`. The real ImGuizmo
  X/Y move handles are visible and aligned to the selected RectTransform pivot.
- Runtime MCP screenshots for `tool: "rotate"` and `tool: "scale"` were also
  captured to `build/.tmp/ui-gizmo-capture-2d-real-imguizmo-rotate.png` and
  `build/.tmp/ui-gizmo-capture-2d-real-imguizmo-scale.png`; both aligned with
  the yellow RectTransform overlay.
- `xmake build -y vultra-app` passed after switching Scene View `2D` to an
  orthographic editor camera.
- Runtime MCP validation for the orthographic camera passed with screenshot
  `build/.tmp/ui-gizmo-capture-2d-orthographic-camera-v2.png`; the selected
  Panel, yellow overlay, and ImGuizmo handles remain aligned.
- Runtime MCP validation for selection-driven view mode passed:
  - Selecting `MCP Canvas` captured
    `build/.tmp/ui-auto-mode-select-canvas.png`: Scene View entered `2D` +
    Move and used a solid background with no skybox.
  - Selecting child `MCP Panel` captured
    `build/.tmp/ui-auto-mode-select-panel.png`: Scene View stayed `2D` +
    Move with ImGuizmo aligned.
  - Selecting `MCP Cube` captured `build/.tmp/ui-auto-mode-select-cube.png`:
    Scene View switched back to `3D`.
- `xmake build -y vultra-app` passed after replacing the 2D ImGuizmo path with
  self-drawn UI handles and making Scene View `2D` skip the scene render target.
- Runtime MCP validation for self-drawn 2D UI handles passed with a temporary
  unsaved scene:
  - Selecting `MCP Panel` captured `build/.tmp/ui-self-handles-2d-panel.png`:
    Scene View entered `2D` + Select and displayed only the editor UI canvas,
    Canvas border, Panel fill, yellow RectTransform outline, pivot, axes,
    rotation handle, and scale handle.
  - Selecting `MCP Cube` captured `build/.tmp/ui-self-handles-3d-cube-v2.png`:
    Scene View switched back to `3D` and displayed the normal scene render.
- `xmake build -y vultra-app` passed after moving the toolbar out of the
  viewport overlay and adding `Rect`/`Transform` tools.
- Runtime MCP validation for the fixed toolbar passed with a temporary unsaved
  scene:
  - Selecting `MCP Panel` captured
    `build/.tmp/ui-scene-toolbar-2d-transform-v2.png`: Scene View is in `2D`,
    Transform tool is active, and the toolbar sits above the viewport.
  - Selecting `MCP Cube` captured `build/.tmp/ui-scene-toolbar-3d-cube-v3.png`:
    Scene View automatically switches back to `3D`, with the toolbar still
    outside the viewport.
- `xmake build -y vultra-app` passed after adding latched UI handle dragging and
  the custom Rect Transform Inspector.
- Runtime MCP validation for latched handles passed with a temporary unsaved
  scene:
  - `build/.tmp/ui-handles-latch-before.png` shows the updated eight-handle
    RectTransform overlay.
  - `build/.tmp/ui-handles-latch-after-scale.png` was captured after an injected
    mouse drag from a resize handle toward the rect body. The drag stayed on the
    latched handle path rather than switching to move.

## Handoff

- ImGuizmo 2D is no longer recommended for UI RectTransform authoring in this
  editor. The current baseline is custom UI handles.
- The current scale handle is still a spike and writes
  `RectTransformComponent::scale`, not `sizeDeltaPx`; future UI design work
  should add proper resize handles and anchor/pivot visual editing.
- No engine-wide culling mask was added yet. The 2D editor mode avoids non-UI
  content by using an editor-only UI authoring surface instead of rendering the
  scene camera.
