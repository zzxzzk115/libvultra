# Editor Asset Import Usability

Date: 2026-05-27

## Changes

- Project loading now spends one frame in a splash-visible phase before releasing
  editor state and starting the asynchronous asset import task. This keeps the
  splash visible before import progress starts updating.
- Content Browser model expansion now matches model sub-assets by registry
  `sourcePath` first, with `importedPath` as a fallback. Model prefab manifests
  reference mesh source URIs, so imported-path-only lookup hid sub-mesh assets.
- Rendered model thumbnails now wait several frames before capture and use a new
  cache version. Large meshes such as Armadillo could be captured before their
  first visible render and then reused as a black cached thumbnail.
- Direct GBuffer and compatibility base-color passes now derive `VTX_HAS_UV0`
  from each mesh vertex layout. No-UV meshes such as Armadillo and Cornell Box
  are no longer skipped; UV-dependent texture sampling is disabled and material
  base-color factors still render.
- Vertex layout inspection is now centralized in `gpu_vertex_layout.hpp`.
  GPU-driven meshlet draw records and mesh tables carry attribute masks and
  byte offsets, so BDA vertex pulling no longer assumes one compile-time vertex
  struct for all meshes.
- Visibility buffer, thin GBuffer, and depth pre-pass now use draw-record vertex
  attribute masks/offsets. Missing UVs no longer sample material textures at
  `(0, 0)` during alpha or GBuffer resolve.
- Content Browser right-click menus now include:
  - `Import File`
  - `Import Folder`

Selected external files or folders are copied into the current Content Browser
directory, then accepted files are reimported through `IAssetService`.
- SDL and GLFW windows now emit file-drop events. The editor stores dropped
  external paths and the Content Browser imports them when the drop lands over
  that window.

## Verification

- `xmake build -y vultra-app` passed.
- Deleted `.vultra/thumbs` so model thumbnails will regenerate on the next
  editor run.

## Follow-Up

- Direct Explorer/Finder drag-and-drop should be manually smoke-tested on each
  desktop backend, because OS drag behavior differs slightly between SDL and
  GLFW.
