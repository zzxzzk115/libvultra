# Async Asset Loading Phase 1 Notes

## Changes

- Added non-blocking asset request APIs alongside the legacy sync APIs.
- Added `vtask` CPU load tasks owned by `AssetSystem`.
- Preserved sync API semantics by waiting if a sync request touches an asset
  already being loaded asynchronously.
- Updated RenderWorld cooking to skip assets until handles become ready.
- Scene instantiation no longer synchronously decodes mesh metadata only to apply
  default transforms.
- Mesh material texture resolution now uses async requests
  plus a pending material refresh queue, so high-resolution textures no longer
  load synchronously during mesh upload.
- Main-thread GPU upload queue draining is capped per frame to avoid a large
  batch of completed CPU tasks monopolizing one editor frame.
- Texture thumbnail CPU cooking now runs as a `vtask` job instead of doing
  `stb_image` decode/resize/write synchronously in the editor update.
- Added engine-level `IJobService` / `JobSystem` as the first wrapper around
  `vtask`, with job snapshots for UI.
- The editor bottom task bar now shows the current job label, message, and a
  flowing progress bar.
- Texture thumbnail cooking now submits through `IJobService` instead of owning
  a local `vtask::Scheduler`.
- Rendered thumbnails now wait for async mesh/splat bounds to become valid
  before starting warmup and writing the thumbnail file, avoiding empty sub-mesh
  thumbnails after glTF import.
- Runtime asset CPU load tasks now retry read/decode failures up to three
  attempts before marking the asset failed.
- `IJobService` now supports `JobOptions::maxAttempts`, and texture thumbnail
  cooking retries failed `stb_image` decode/resize/write work three times.
- `vasset` texture import now downscales authoring textures whose longest edge
  is greater than 4096px to a 2048px longest edge before KTX2/BasisU encoding.
- `vasset` mesh import now applies meshoptimizer vertex-cache, overdraw, and
  vertex-fetch index/vertex remapping before meshlet generation.
- Targeted editor imports now save the registry after each successfully imported
  source file and notify the main thread to reload the runtime registry and queue
  thumbnails immediately, so newly imported glTF sub-mesh thumbnails can start
  cooking before the whole import batch finishes.
- Rendered asset thumbnails now clear preview and scene cameras with alpha 0 and
  use a new thumbnail cache version so regenerated PNGs have transparent
  backgrounds instead of opaque black/dark fills.
- Preview/thumbnail code now waits for mesh GPU residency, material texture
  residency, and pending material refreshes before saving rendered thumbnails,
  which avoids white-model captures for glTFs with large texture sets.
- Final composition now preserves source alpha, and rendered thumbnail PNGs get
  a narrow post-save cleanup for pure black background pixels so transparent
  thumbnails survive the current render pipeline.
- Inspector model previews are narrower and keep resubmitting their preview
  camera while async assets settle, so a newly selected model no longer needs to
  be deselected/reselected to appear.
- Added scene-level async loading through `ISceneService::loadSceneAsync`.
  It prepares a staging world, requests scene mesh/material/texture/splat assets,
  reports loading progress, and only allows instantiation once all preview-critical
  assets are ready.
- Editor default-scene loading and VPK runtime startup now use the scene-level
  gate, keeping loading visible until the full scene is ready instead of showing
  white models or progressively appearing textures.
- Preserved the previous progressive behavior as explicit
  `ISceneService::loadSceneStreaming`, which currently maps to immediate
  instantiation and can later grow into large-world streaming.
- Runtime VPK scene preload no longer busy-waits in post-configure. It now starts
  `loadSceneAsync` during configure and polls readiness from the normal engine
  tick, so asset GPU uploads and material refreshes can progress frame by frame
  without deadlocking startup.
- Runtime VPK startup now clears the default `DemoAppHost` manual camera before
  the async scene gate begins, preventing the temporary runtime camera from
  rendering ImGui while the scene is loading. Scene cameras with an empty or
  legacy `"universal"` renderer key are mapped to the packaged runtime render
  graph key, preferring `res://render/default.vrg.json` when present.
- Fixed the runtime async scene gate for packages that release mesh CPU copies:
  `meshPreviewReady()` no longer requires `handle.cpu()` after the mesh is GPU
  ready. Without this, packaged runtime scenes could stay in "Loading scene"
  forever because `keepCpuCopy=false` clears the CPU mesh after upload.
- Stopping editor play/simulation mode now resynchronizes the current history
  state after restoring the play-mode snapshot, so the restore operation does
  not append a synthetic "Scene Edit" entry.
- Editor background asset import/delete now reloads only the asset registry and
  UUID resolver instead of calling full `AssetSystem::configure()`. This keeps
  the current scene's resident GPU assets alive and avoids a scene-looking
  reload when importing or deleting source models.
- The editor status bar now shows scene-load progress/messages when a
  `loadSceneAsync` gate is active, and scene loading also updates
  `statusMessage`.
- Rendered thumbnail generation now uses a private thumbnail `World` through
  `RenderCamera::worldOverride` and removes only its own `"Thumbnail Camera"`.
  It no longer clears the editor world or global manual cameras, so Scene View
  and Game View cameras are not stolen while thumbnails are rendered offscreen.

## Verification

- `xmake build -y vultra-app` passed after the runtime preload/manual-camera
  fix. One earlier attempt reached link and hit `LNK1104` because the output
  `vultra.exe` was still locked; rerunning after the lock cleared succeeded.
- After the `meshPreviewReady()` runtime gate fix, compilation reached the link
  step again, but `build/windows/x64/release/vultra-app/vultra.exe` was locked
  by a running `vultra` process (PID 35996), causing `LNK1104`.
- `xmake build -y vultra-app` passed after the editor history play/stop fix.
- `xmake build -y vultra-app` passed after the registry-preserving background
  import/delete fix and status-bar scene-load progress update.
- `xmake build -y vultra-app` passed after isolating rendered thumbnail cameras
  and worlds from the editor scene cameras.

## Handoff

Future phases should make GPU upload budgeting adaptive by estimated byte cost.
Existing import jobs still use their local scheduler and should be migrated to
`IJobService` next so import progress can share the same retry/status-bar
surface as thumbnail and runtime asset jobs.
