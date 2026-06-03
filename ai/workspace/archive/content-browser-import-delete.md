# Content Browser Import/Delete Notes

Date: 2026-05-29

## Issue

- Deleting a visible source asset from Content Browser removed only the source
  file and left the source-side `.vimport` descriptor behind.
- Dragging an external PNG into Content Browser copied the file and then ran
  `reimportAsset` synchronously from the UI path, which made the operation look
  unlike the editor background import flow and risked mutating the live asset
  registry while editor UI/rendering code was still using it.

## Change

- File deletion now removes the source asset and its sidecar generated with the
  vasset rule `replace_extension(".vimport")`.
- Content Browser import/drop now copies the external file/folder into the
  selected content directory and queues an editor asset import refresh for that
  copied path only.
- `EditorApp` can run targeted path imports through the existing background
  import task by calling `vasset::VAssetImporter::importOrReimportAsset` for
  files and `importOrReimportAssetFolder` for dropped folders. Project startup
  still passes no explicit paths and performs the full scan.
- Once the targeted import completes, the editor reloads the runtime asset
  registry on the main thread.
- Imported source paths are queued for targeted thumbnail prewarm. Texture
  thumbnails are cooked through a CPU-only background step so imported images
  can show thumbnails in Content Browser without running the full thumbnail
  render pipeline during normal editing.
- Import progress is now structured for the editor UI: the background import
  task records progress, current item, processed item count, and total item
  count. The editor displays that state in an `Import Assets` modal progress
  bar while targeted imports are running.
- `vasset::VAssetImporter::importOrReimportAsset` now emits progress callbacks
  for single-file imports, matching folder import behavior.
- Model import progress now expands nested work: vasset pre-scans Assimp
  material texture references, reports unique texture imports, mesh output
  writes, and the final scene manifest as separate progress items.
- Model source thumbnails are queued after targeted import. Render thumbnails
  are processed in the editor by snapshotting the current world before thumbnail
  rendering and restoring it afterward.
- Delete queues the removed path for a cleanup-only targeted refresh; the import
  task skips missing paths, cleans stale registry rows, then saves
  `asset_registry.tsv`.
- SDL3 file drop handling no longer manually frees `event.drop.data`; SDL owns
  the temporary event memory and releases it after event maintenance. The app
  copies the path string before emitting its own event.

## Verification

- `xmake build -y vultra-app` passed.
- `xmake build -y vultra-app` passed after the SDL drop lifetime fix.
- `xmake build -y vultra-app` passed after targeted texture thumbnail cooking.
- `xmake build -y vultra-app` passed after import progress modal and single-file
  vasset progress callbacks.
- `xmake build -y vultra-app` passed after model subtask progress and model
  thumbnail processing.

## Follow-Up

- Manual editor smoke test: drag a PNG from Explorer into Content Browser,
  verify the UI remains alive, then confirm the texture becomes selectable after
  the queued import finishes.
- Manual delete smoke test: delete a source asset with an existing `.vimport`
  sidecar and confirm both files are removed.
