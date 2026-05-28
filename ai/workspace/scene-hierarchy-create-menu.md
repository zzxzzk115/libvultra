# Scene Hierarchy Create Menu

## Changes

- Added Scene Hierarchy right-click creation entries for:
  - empty entities
  - builtin geometry: quad, cube, sphere, capsule
  - lights: directional, point, spot, area
  - camera
  - XR camera
  - environment
- The existing toolbar Create button opens the same creation popup menu.
- The toolbar Create entry is now a right-aligned icon button.
- Entity context menus can create the same object types as children.
- New objects are selected immediately and mark the scene dirty.
- Fixed builtin capsule mesh ring order so Capsule creates a capsule instead of
  a malformed sphere-like cap.
- Builtin MeshComponent instances hide the external mesh UUID field and expose
  a `Builtin Geometry` selector.
- Area Light creation stays as a single Light entity. The renderer now adds an
  implicit quad surface while cooking `RenderWorld`; its color and size are
  derived from the area light every frame. The surface renders through the
  normal GBuffer path and uses the builtin unlit double-sided path
  (`CullMode::eNone` in Direct GBuffer).
- The implicit area light surface is marked `castsShadow = false` at the cooked
  `RenderInstance` level so it is skipped by `ShadowMapPass`.
- Direct and thin GBuffer material attachments now use `RGBA16F` instead of
  `RGBA8_UNorm` so the alpha channel can store material model ids such as
  Unlit/Phong/MaterialGraph without clamping to 1.0.

## Verification

- `xmake build -y vultra-app` passed.
