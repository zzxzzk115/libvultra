# Vultra Source Asset Package Task

## Goal

Add a `.vultrapackage` source asset package workflow similar to Unity packages.

## In Scope

- Export selected Content Browser source assets into a `.vultrapackage` file.
- Import `.vultrapackage` files back into the current project asset root.
- Skip imported files whose destination already exists with the same MD5 digest.
- Support Content Browser multi-selection and right-click export of the selected set.

## Out of Scope

- Runtime VPK packaging changes.
- Dependency graph expansion beyond the selected source files.
- Package preview or conflict-resolution UI.

## Verification

- `xmake build -y vultra-app` passed.
- `xmake build -y test-material-graph` passed.
- `build\windows\x64\release\test-material-graph\test-material-graph.exe`
  passed.

## Handoff

- Export uses a save dialog and writes only selected source assets into
  `.vultrapackage`; generated outputs and `.vimport` sidecars are excluded.
- Export includes local source dependencies declared by `.gltf`, `.obj`, and
  `.mtl` files, including glTF `.bin` buffers and referenced textures.
- Import skips destination files with matching MD5. Files that are written are
  queued through the normal external asset import path.
- `vultra-app` validates packages after export and before import, checking
  entry checksums, safe paths, and model dependency closure. Package import
  queues written source parent folders so importer behavior matches Explorer
  folder drops more closely.
- Import completion expands queued directories back to source files for editor
  refresh, clears material graph text overrides, reloads shader libraries and
  render pipelines when relevant, and prewarms thumbnails from the expanded
  file list.
- Material graph/model/mesh thumbnail request caches re-check the current
  source/imported file stamp before returning cached requests, so overwrites
  produce fresh thumbnail keys.
- Material graph thumbnail keys use source content hashes, and clearing text
  overrides bumps GPU resource content revision so render-side material graph
  caches cannot keep drawing overwritten graph contents.
- Inspector and material graph preview cameras replace their previous manual
  camera each frame; render thumbnails fail instead of waiting forever if a
  mesh/sub-asset never becomes ready.
- Model sub-asset expansion no longer runs a synchronous importer from the UI
  draw path; missing model imports are queued through the normal background
  import path, and sub-asset caches are invalidated by asset file generation.
- Content Browser drag and Reimport actions now queue source imports instead
  of calling `reimportAsset` directly from UI interaction paths.
- Override render worlds are released lazily instead of erased immediately, and
  thumbnail render jobs remove their manual camera/release override worlds
  before active job teardown. This avoids stale manual cameras and override
  render world lifetime races during asynchronous thumbnail/inspector preview
  rendering.
- Scene thumbnails are queued through `AssetThumbnailService` when imported
  `.vscn` files are prewarmed; import-triggered scene requests force a render
  even when an older thumbnail already exists. Content Browser also requests
  scene thumbnails on demand instead of only loading files saved by scene
  switching.
- Forced scene thumbnail requests carry a `forceRender` flag through the render
  queue so existing `.scene.png` files no longer short-circuit import-triggered
  renders. Model sub-asset collection filters invalid UUIDs and missing cooked
  mesh files before exposing entries to thumbnail rendering.
- Scene thumbnails instantiate the target scene into a dedicated override
  `World`, render one offline frame to a fixed 128x128 target, and use the
  scene primary camera's renderer key/clear settings. Scene output stays opaque,
  and Gaussian splat assets must be ready before capture.
- Override render worlds now build CPU-driven 3DGS GPU scene data as well as
  mesh draw data, so universal renderer 3DGS passes run for thumbnail worlds.
- Package export now treats `.vscn` files as dependency roots too. Scene asset
  UUID references are resolved through the current asset registry and their
  source files are included in the source-only package, so imported scenes keep
  mesh/3DGS asset references resolvable in a new project.
- OBJ materials imported without explicit two-sided metadata are treated as
  double-sided to avoid common OBJ backface preview/culling surprises.
