# In-Game UI V1

V1 is screen-space UI authored in pixels relative to a Canvas reference
resolution.

## Components

- `CanvasComponent`: enables a UI root, sort order, reference resolution in
  pixels, and scale mode.
- `RectTransformComponent`: UI transform source of truth. Anchors, pivot,
  anchored position, size delta, rotation, and scale are pixel-space layout
  data except `scale`, which is a multiplier.
- `UiPanelComponent`, `UiImageComponent`, `UiTextComponent`,
  `UiButtonComponent`, and `UiLayoutComponent`: common authoring components for
  MVP panels, images, text, buttons, and simple horizontal/vertical/grid layout.

## Pixel Semantics

- RectTransform position and size fields are authored in Canvas reference
  pixels.
- Layout padding, margin, spacing, cell size, font size, border radius, and hit
  rectangles are pixels.
- Runtime Canvas scaling converts reference pixels to render-target pixels.
- New Canvases default to `Scale With Screen`, so authoring against a reference
  resolution remains visible in smaller Game View or capture targets. Constant
  pixel mode is still available for exact render-target pixel authoring.
- Scene View 2D UI editing reports movement and sizing deltas in pixels.

## Transform Precedence

If an entity has `RectTransformComponent`, it is a UI entity. Inspector hides
the normal `TransformComponent`, and editor/MCP/Lua transform-style operations
update RectTransform pixel fields.

The engine may still keep `TransformComponent` for hierarchy compatibility, but
it is not the editable source of truth for UI layout.

## Editor And MCP

Shared editor commands and Runtime MCP component kinds include:

- `canvas`
- `rect_transform`
- `ui_panel`
- `ui_image`
- `ui_text`
- `ui_button`
- `ui_layout`

Entity templates include:

- `ui_canvas`
- `ui_panel`
- `ui_text`
- `ui_image`
- `ui_button`

When `scene.update_component` targets `transform` on a UI entity, the command
maps position, rotation, scale, size, anchors, and pivot onto RectTransform.

## Lua

Lua exposes:

- `entity.rectTransform`
- `entity.uiButton`
- `UI.isPointerOverUI()`
- `UI.hoveredEntity()`
- `UI.pressedEntity()`

UI Lua APIs use the same Canvas reference pixel units as editor/MCP authoring.

## Rendering

Runtime rendering submits UI draw items from `RenderWorldCooker` into the
render graph. `UiOverlayPass` composites panels, images, and buttons over the
final scene color using WebGPU-compatible bindings. `UiTextComponent` is
authorable and script-visible in V1, but text glyph rendering is deferred.

## Deferred

Slider, Checkbox, RadioButton, InputText, IME, world-space UI, XR UI,
nine-slice, text glyph rendering, full font asset import, and full retained-mode
UI rendering polish are deferred beyond V1.
