# Scene View Gizmos, Debug Draw, Delete & Drag-Drop

## Goal

Fill in the missing scene-editing essentials in the editor scene view: entity
billboard icons, an immediate-mode debug-draw system (selection bounds, physics
colliders, light & camera gizmos), Delete-key entity removal, and cursor-following
asset drag-drop.

## Scope

In scope:

- **Billboard icons** — MDI glyph icons for cameras/lights drawn in the scene view,
  clickable to select, with a toolbar popup toggle and an icon-size slider.
- **Debug draw** — wire the previously-dead `DebugDrawInterface` into the renderer as
  a builtin `DebugDraw` pass driven by the global `dd::` immediate queue. Expose
  `IRenderService::debugDraw{Line,Aabb,Box,Sphere,Frustum}` (also usable by gameplay).
  Editor submits selection AABB, collider wireframes (box/sphere/capsule), light
  gizmos (point sphere / spot cone / directional disc+rays) and camera frustums.
- **Delete key** — `ImGuiKey_Delete` in scene view and scene hierarchy routes to the
  existing `scene.remove_entity` command (recursive, undoable, selection-aware).
- **Drag-drop** — content-browser asset drop with a cursor-following ghost AABB,
  raycast placement onto scene meshes (ground-plane fallback), and the asset's
  default scale applied via `instantiateAssetInScene`.

Out of scope:

- Multi-entity gizmo manipulation; transform gizmos already exist (ImGuizmo).
- Depth-correct debug draw in non-editor cameras (gated to `debugDrawEnabled`).
- Screen-space debug text (`dd::screenText`) — only line primitives are rendered.

## Key design notes

- Debug geometry flows through the **global `dd::` context** + `commonContext.debugDraw`
  (`dd::initialize`/`shutdown` in `RenderSystem::onInit`/`onShutdown`). The builtin
  `DebugDrawPass` calls `dd::flush()` which renders and auto-clears the queue — this
  replaced an earlier CPU-buffer approach whose per-frame clear raced the graph build.
- The pass reads `DirectGBuffer.depth` as a read-only depth attachment so wireframes are
  occluded by geometry (`depthTest` on, `depthWrite` off, `eLessOrEqual`).
- The debug-draw VP applies the same `projection[1][1] *= -1` Vulkan clip-space flip as
  `GPUCameraBlock` (see `upload_resources.cpp`), otherwise wireframes drift in Y as the
  camera moves.
- The line pipeline must set `setVertexStride(sizeof(dd::DrawVertex))`; a stride of 0
  collapses every line to a point.
- New builtin passes follow the data-driven render graph: register in
  `declarative_renderer.cpp` and add the node to `builtin/render/universal.vrg.json`
  (and `resources/render/default.vrg.json`).

## Critical files

- `source/vultra/.../debug_draw/debug_draw_interface.{hpp,cpp}` — line pipeline, depth test.
- `source/vultra/.../rendering/srp/builtin/passes/debug_draw_pass.{hpp,cpp}` — builtin pass.
- `source/vultra/.../rendering/srp/declarative_renderer.cpp` — pass registration + Y-flip.
- `source/vultra/.../services/render_service.hpp`, `rendering/render_system.{hpp,cpp}` — API.
- `source/vultra/.../core/base/common_context.{hpp,cpp}` — global `debugDraw` interface.
- `source/vultra_app/.../ui/windows/scene_view_window.{hpp,cpp}` — icons, submission, drag-drop, Delete, toolbar.
- `source/vultra_app/.../ui/windows/scene_hierarchy_window.cpp` — Delete.
- `source/vultra_app/include/editor_app/ui/viewport_math.hpp` — world↔screen / screen-ray helpers.

## Verification

- `xmake build vultra` and `xmake build vultra-app` build clean.
- Run the editor on `res://scenes/sponza.vscn`:
  - Camera/light icons appear and are clickable; icon size slider + per-gizmo toggles work.
  - Selecting a mesh shows an orange AABB (toggle, default off); colliders show green wires;
    point/spot/directional lights and camera frustums show gizmos, all occluded by geometry.
  - Wireframes stay locked to world positions while orbiting/panning (no Y drift).
  - Delete removes the selected entity (recursive) in both scene view and hierarchy; Ctrl+Z restores.
  - Dragging a mesh from the content browser shows a ghost that follows the cursor, snaps onto
    geometry, and drops at the cursor with the asset's default scale (no camera clipping).

## Status

Implemented and verified on Vulkan/Windows. Debug draw confirmed rendering (pipeline builds
with depth, no validation errors). Remaining ideas: depth-correct draw for game cameras,
debug screen text, and configurable gizmo colors.
