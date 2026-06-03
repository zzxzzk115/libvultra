# UI Render Preview Layer/Culling Foundation

## Summary

Added render layer and camera culling-mask infrastructure as the first step
toward replacing Scene View UI2D's editor-only overlay preview with a real
rendered UI preview.

## Changes

- Added `LayerComponent` with `mask`.
- Added render layer constants:
  - `kRenderLayerDefaultMask`
  - `kRenderLayerUiMask`
  - `kRenderLayerAllMask`
- Added `CameraComponent::cullingMask`, serialized as `cullingMask`.
- Cooked `CameraComponent::cullingMask` into `RenderCamera`.
- Cooked layer masks into mesh instances, UI draw items, and gaussian splat
  instances.
- Filtered UI overlay, direct gbuffer, and compatibility base-color mesh draws
  by camera culling mask.
- Inspector can add/edit Layer and Camera culling mask with Default/UI/All/Clear
  controls.
- UI creation commands assign UI layer explicitly; UI without a LayerComponent
  still defaults to UI layer during UI render cooking.
- Scene View's existing UI2D editor camera builder now uses a UI-only culling
  mask, ready for a future true-rendered UI preview target.
- Asset service exposes UUID-to-URI resolution so Scene View/texture selector
  can resolve both builtin texture UUIDs and project registry UUIDs. This avoids
  builtin UI images falling back to the gray editor rectangle.
- Scene View UI2D now submits a real UI-only manual render camera and maps the
  runtime `UiOverlayPass` into the Scene View canvas offset/scale. The editor
  overlay no longer draws UI content; it only draws grid, rulers, canvas border,
  hover/selection outlines, and handles.
- Scene View UI2D keeps and displays its render target instead of replacing the
  viewport with an `InvisibleButton` plus a filled ImGui overlay. Grid, rulers,
  and handles are now drawn over the rendered target, so UI authoring is WYSIWYG
  for runtime `UiOverlayPass` content.
- Scene View UI2D now uses a dedicated internal renderer, `editor-ui2d`,
  backed by `builtin://render/ui_editor.vrg.json`. The graph is intentionally
  only `CameraClear -> UiOverlay -> FinalComposition`, so UI authoring no
  longer touches the project/deferred renderer, skybox, environment map, or
  GBuffer outputs.
- `CameraClear` is a builtin declarative render-graph pass that creates a
  target-sized color texture and clears it with the active camera clear color.
  Scene View UI2D owns that authoring clear color and passes it through the
  dedicated UI2D renderer.
- UI image texture presence no longer depends on `textureIndex != 0`; GPU
  bindless index 0 is valid. Render cooking now separates "texture is ready"
  from the numeric GPU texture index, and the fragment shader keys off the
  explicit texture flag.
- Lua exposes `camera.camera.cullingMask` and `Layer.Default`, `Layer.UI`,
  `Layer.All`.

## Verification

- `xmake build -y vultra-app` passed.
- Runtime MCP smoke passed:
  - Started `vultra-app --editor --mcp --project example.vproject --no-xr`.
  - Called `vultra.runtime.status`, `vultra.editor.scene_view` with
    `mode=ui2d`, and `vultra.editor.command` to create a Canvas + UI Image.
  - Captured `.vultra/mcp/editor_ui2d_wysiwyg_after_fix.png`; Scene View shows
    the rendered UI Image under editor grid/selection handles, confirming the
    content is no longer drawn by ImGui-only preview code.
  - Captured `.vultra/mcp/editor_ui2d_editor_renderer_smoke.png`; Scene View
    UI2D shows a rendered white UI Image over the authoring background while the
    Game View preview still shows the runtime camera scene.
  - `vultra.runtime.frame_resources` showed `Scene View 2D` using renderer
    `editor-ui2d` with `CameraClear` and backbuffer resources only; the
    project camera still had `DeferredLightingOutput` and `Environment Map`,
    confirming those resources are no longer part of the UI2D Scene View.
  - `vultra.runtime.rendergraph_source` with `rendererKey=editor-ui2d` returned
    `builtin://render/ui_editor.vrg.json` and the expected
    `CameraClear -> UiOverlay -> FinalComposition` pass chain.

## Follow-Up

- Scene View UI2D currently maps the first active Canvas into the editor preview
  transform. Multiple simultaneous canvases need a per-canvas transform path if
  they use different reference resolutions or authoring offsets.
- GPU-driven meshlet and gaussian splat camera-layer filtering still need to be
  fully pushed into per-camera GPU scene view/drawset construction.
