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
  `UiButtonComponent`, `UiToggleComponent`, `UiSliderComponent`,
  `UiProgressBarComponent`, and `UiLayoutComponent`: common authoring
  components for MVP panels, images, text, buttons, toggles, sliders, progress
  bars, and simple horizontal/vertical/grid layout.

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
- `ui_toggle`
- `ui_slider`
- `ui_progress_bar`
- `ui_layout`

Entity templates include:

- `ui_canvas`
- `ui_panel`
- `ui_text`
- `ui_image`
- `ui_button`
- `ui_toggle`
- `ui_slider`
- `ui_progress_bar`

When `scene.update_component` targets `transform` on a UI entity, the command
maps position, rotation, scale, size, anchors, and pivot onto RectTransform.

`UiButtonComponent` follows Unity-style target graphic semantics. Its
`targetGraphic` field points at the UI entity whose visual component should be
tinted by `normalColor`, `hoveredColor`, and `pressedColor`. If `targetGraphic`
is empty, the button falls back to graphics on its own entity. The `ui_button`
template adds `UiButtonComponent` and `UiImageComponent` to one entity and sets
`targetGraphic` to itself. A button with a target graphic does not draw an
additional full-rect overlay.

## Lua

Lua exposes:

- `entity.rectTransform`
- `entity.uiButton`
- `entity.uiToggle`
- `entity.uiSlider`
- `entity.uiProgressBar`
- `UI.isPointerOverUI()`
- `UI.hoveredEntity()`
- `UI.pressedEntity()`

UI Lua APIs use the same Canvas reference pixel units as editor/MCP authoring.

## Rendering

Runtime rendering submits UI draw items from `RenderWorldCooker` into the
render graph. `UiOverlayPass` composites panels, images, buttons, toggles,
sliders, and progress bars over the final scene color using WebGPU-compatible
bindings. `UiTextComponent` is authorable and script-visible in V1, but text
glyph rendering is deferred.

## Deferred

RadioButton, InputText, IME, world-space UI, XR UI, nine-slice, text glyph
rendering, full font asset import, and full retained-mode UI rendering polish
are deferred beyond V1.
