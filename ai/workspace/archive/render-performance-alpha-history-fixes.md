# Render Performance, Thumbnail Alpha, And History Fixes

Date: 2026-05-30

## Changes

- Added per-camera `RenderCamera::suppressSkybox` and enabled it for asset
  thumbnail cameras. Scene thumbnails now force color clear mode with alpha 0
  instead of inheriting camera skybox clear mode.
- Made declarative and direct built-in deferred paths respect
  `suppressSkybox`, so thumbnail render worlds can keep environment lighting
  data without drawing the skybox background.
- Changed SSAO and SSR disabled runtime paths to skip their expensive
  fullscreen passes. SSR composite now passes through when reflection input is
  absent or aliases the source.
- Improved disabled render-graph passthrough for single-output multi-input
  passes by falling back to the first input, which handles SSR-style disabled
  nodes.
- Preserved alpha in FXAA, gaussian splat composite, foveated splat composite,
  XR geometry warp, and XR pull-push passes. Skybox remains opaque for normal
  Scene/Game View rendering; thumbnails skip the skybox through
  `RenderCamera::suppressSkybox`.
- Changed default universal/project render graphs to keep the SSAO node wired
  but with `params.enabled=false`, so deferred lighting uses its AO fallback
  without a full-resolution SSAO pass. SSR is top-level disabled by default and
  passthroughs through the source color path.
- Synced the open `build/test-project` render graph and imported render-graph
  cache to the same SSAO/SSR defaults, since existing projects do not
  automatically inherit template changes.
- Reduced opt-in SSAO/SSR defaults: SSAO uses 16 max radius pixels, 2 steps,
  and 4 directions; SSR uses 8 steps and 2 binary-refinement iterations.
- Reduced editor history observation cost by avoiding full scene serialization
  during active item/mouse interaction and by skipping idle captures when the
  dirty state already matches the current history entry.
- Reduced world cook material graph overhead by reusing material graph GPU
  materials while the GPU resource content revision is unchanged and avoiding
  parameter uploads when the packed params are identical.
- Updated the project launcher default render graph template to match the
  universal deferred chain, including SSAO, shadow, gaussian composite, SSR,
  SSR composite, disabled Pixelate/Invert project passes, tone mapping, FXAA,
  selection outline, and final composition.
- Follow-up: replaced the previous horizon-search SSAO shader with a
  Supernova-style hemisphere kernel/TBN sampler while keeping Vultra's pass
  contract and lighter default sample count.
- Follow-up: DirectGBuffer now skips the `entityId` MRT unless the camera is
  doing entity-id debug output or selection outline. Scene View only requests
  selection outline when an entity is selected, dropping one full-resolution
  color attachment in the normal no-selection editor path.
- Follow-up: Model Inspector preview cameras set `suppressSkybox = true`, so
  previews do not draw a skybox while normal Scene/Game View skyboxes remain
  camera-graph controlled.
- Follow-up: render time now comes from `ITimingService` and is uploaded to the
  frame block with delta time. Material graph surface-param caching detects
  `vultra.input.time` and bypasses the static content-revision early-out for
  time-dependent graphs.
- Follow-up: material graph surface-param evaluation now caches the parsed
  graph/output/time-dependency metadata by content revision. Time-dependent
  graphs still update every frame, but no longer reload and parse JSON during
  `RenderWorldCooker::cook`.
- Follow-up: `RenderWorldCooker::cook` now emits nested CPU profiler scopes for
  meshes, material overrides, gaussian splats, lights, environment, and
  reflection probes.
- Follow-up: DirectGBuffer now prepares draw records in one traversal and draws
  from those records, avoiding the previous count/prepare/draw triple walk and
  repeated layout checks. Double-sided state is also evaluated from the remapped
  material index.
- Follow-up: material graph GPU materials now cache the last evaluated
  time-dependent params per graph/material/time, so repeated mesh overrides in
  the same frame do not re-evaluate the same graph. Existing material param
  updates upload only the changed material-param range instead of the whole
  parameter buffer.
- Follow-up: DirectGBuffer and ThinGBuffer now store normal and material data in
  `RGBA8_UNorm` render targets instead of `RGBA16F`. Normals are encoded from
  `[-1, 1]` to `[0, 1]`, material model IDs are packed into 8-bit alpha, and
  Deferred Lighting/SSAO/SSR decode the packed GBuffer data on read.
- Follow-up: DirectGBuffer no longer treats Phong, SpecGloss, Unlit, and other
  non-PBR-MR materials as implicitly double-sided. Unknown two-sided state now
  defaults to back-face culling; only PBR-MR's explicit `doubleSided` flag keeps
  culling disabled.
- Follow-up: DirectGBuffer can now consume an optional `DepthPre.depth` input.
  When present it attaches depth read-only, disables depth writes, and uses an
  `EARLY_FRAGMENT_TESTS` shader variant so hardware early-Z can reject hidden
  GBuffer fragments. Missing pre-depth inputs fall back to the original
  self-writing depth path.
- Follow-up: the default mono render graphs now run `DepthPre` before
  `DirectGBuffer`; declarative `DepthPre` now maps to a CPU direct-geometry
  `DirectDepthPrePass` that reuses DirectGBuffer draw preparation and alpha
  test rules, so the pass is visible and retained for the non-meshlet
  DirectGBuffer path.
- Follow-up: registered the explicit `DirectDepthPre` declarative pass type and
  kept `DepthPre` as a legacy compatibility alias. The editor add-node popup
  hides the old alias and shows `DirectDepthPre`.
- Follow-up: upgraded current builtin, resource, imported cache, project
  launcher template, and `build/test-project` render graphs to use
  `DirectDepthPre -> DirectGBuffer.depth`.
- Follow-up: GBuffer normals now use octahedral encoding in `RG8_UNorm` instead
  of `RGBA8_UNorm`, reducing full-resolution normal attachment bandwidth by
  another two bytes per pixel. Deferred Lighting, SSAO, and SSR decode the new
  normal representation.
- Follow-up: upgraded builtin/resource/imported/test-project/project-launcher
  render graphs from `DirectDepthPre -> DirectGBuffer` to
  `VisibilityBuffer -> ThinGBuffer`. `VisibilityBuffer` now publishes depth for
  downstream passes and supports CPU-driven indirect command buffers; declarative
  renderer builds import the persistent GPU scene buffers that the highend
  visibility/thin path needs.
- Follow-up: `ThinGBuffer` can emit the optional `entityId` attachment when
  selection outline or entity-id debug output needs it. CPU draw records and GPU
  instances now carry `entityPickingId`, and the highend shader structs were
  updated to match.
- Follow-up: SSAO and SSR defaults were restored to disabled after the graph
  migration, with the lighter opt-in sample settings kept in the graph params.
- Follow-up: declarative `ThinGBuffer` now exposes a passthrough `depth` output.
  Current graphs route downstream depth users through `ThinGBuffer.depth`, so
  the logical topology reads `VisibilityBuffer -> ThinGBuffer -> lighting/post`.
- Follow-up: completed the visibility-buffer topology by adding
  `CoarseInstanceCull -> MeshletCull -> BuildIndirect -> DrawsetBuild` ahead of
  `VisibilityBuffer` in builtin/resource/imported/test-project/project-launcher
  graphs. Declarative GPU scene import now only imports persistent scene buffers;
  transient visible lists, draw records, indirect commands, and draw sets are
  produced by their passes each frame.
- Follow-up: `VisibilityBufferPass` now treats the presence of a draw-set buffer
  as the drawset/queue-window path, even when `gpuSceneView->isGpuDriven()` is
  false, so declarative visibility graphs do not accidentally draw a compact CPU
  indirect list from a transient drawset buffer.
- Follow-up: tightened declarative validation contracts so meshlet producer
  passes only declare graph-connected transient inputs. Persistent scene buffers
  remain runtime imports and no longer trigger missing `CoarseInstanceCull`
  `instance` input errors.
- Follow-up: added the missing `WRITE_ENTITY_ID : bool permute` keyword
  declaration to `thin_gbuffer.frag.vshader`, so highend vshlib contains both
  ThinGBuffer fragment variants requested by runtime.

## Verification

- `resources/render/default.vrg.json`, `resources/render/xr_view_synthesis.vrg.json`,
  and `builtin/render/universal.vrg.json` parsed with `ConvertFrom-Json`.
- `build/test-project/resources/render/default.vrg.json` and its imported
  render-graph JSON cache parsed with `ConvertFrom-Json`.
- `git diff --check` passed with only pre-existing line-ending warnings.
- `xmake build -y vultra-app` passed after regenerating builtin shader and
  render graph assets.
- Follow-up `git diff --check` passed with the same pre-existing line-ending
  warnings.
- Follow-up `xmake build -y vultra-app` passed after regenerating
  `builtin_highend.vshlib` and `builtin_shaders.hpp`.
- Follow-up `xmake build -y vultra-app` passed after the material graph parse
  cache change.
- Follow-up `xmake build -y vultra-app` passed after cook profiler labels and
  DirectGBuffer draw-record preparation changes.
- Follow-up `xmake build -y vultra-app` passed after per-frame material graph
  override cache and partial material-param upload changes.
- Follow-up `xmake build -y vultra-app` passed after GBuffer format packing and
  shader decode updates.
- Follow-up `xmake build -y vultra-app` passed after restoring back-face
  culling for non-explicitly-double-sided DirectGBuffer materials.
- Follow-up `xmake build -y vultra-app` passed after optional DirectGBuffer
  pre-depth, early-fragment-test variant, default graph DepthPre wiring, and
  octahedral `RG8` normal compression.
- Follow-up `xmake build -y vultra-app` passed after replacing the culled
  meshlet-only declarative DepthPre path with DirectGBuffer's CPU
  `DirectDepthPrePass`.
- Follow-up `xmake build -y vultra-app` passed after registering
  `DirectDepthPre` and upgrading current render graph assets/templates.
- Follow-up `xmake build -y vultra-app` passed after switching current and
  launcher render graphs to `VisibilityBuffer -> ThinGBuffer`, adding optional
  thin entity-id output, and importing declarative GPU scene buffers.
- Follow-up `xmake build -y vultra-app` passed after adding the `ThinGBuffer.depth`
  passthrough contract and rewiring downstream graph inputs through it.
- Follow-up `xmake build -y vultra-app` passed after adding the meshlet cull /
  indirect / drawset producer passes to the visibility graph and switching
  `VisibilityBufferPass` to use drawset output when available.
- Follow-up `xmake build -y vultra-app` passed after removing persistent scene
  buffers from meshlet producer pass input validation.
- Follow-up `xmake build -y vultra-app` passed after regenerating
  `builtin_highend.vshlib` with two `thin_gbuffer.frag` variants.
- Follow-up `xmake build -y vultra-app` passed after adding front-to-back
  ordering for DirectDepthPre/DirectGBuffer prepared direct draws.
- Follow-up `xmake build -y vultra-app` passed after making packaged render
  graph pass discovery permission-safe and linking the default builtin skybox as
  a Windows `RCDATA` resource for single-exe export/runtime fallback.
- Follow-up `xmake build -y vultra-app` passed after adding Linux/macOS
  single-exe fallback via an assembler `.incbin` object instead of generated C++
  texture headers.
- Follow-up `xmake build -y vultra-app` passed after removing the packaged
  runtime's second VPK open during post-configure; render graph URIs now come
  from the already-mounted asset registry.
- Follow-up `xmake build -y vultra-app` passed after preventing packaged
  render graph reload from recursively scanning the physical root path.
- Follow-up `xmake build -y vultra-app` passed after separating OpenXR session
  close from application exit.
- Follow-up `xmake build -y vultra-app` passed after adding an Inspector
  `XRViewComponent` runtime button to request/reopen an XR session.

## Notes

- MCP `vultra` startup timed out, so this was completed by reading repository
  files directly per AGENTS fallback rules.

- 2026-05-31: VisibilityBuffer -> ThinGBuffer remains registered for manual
  testing, but current/default/project-launcher graphs were restored to the
  visible `DirectDepthPre -> DirectGBuffer -> DeferredLighting` path after the
  visibility path produced black output in existing scenes. Verified JSON parse,
  `xmake build -y vultra-app`, and an editor smoke launch; no graph invalid,
  ThinGBuffer shader-load, or ErrorDeviceLost messages appeared in `Vultra.log`.
- 2026-05-31: Direct raster prepared draws now carry a stable uniform parameter
  index and sort by camera-distance front-to-back before both depth prepass and
  GBuffer submission, with material/mesh/index tiebreakers.
- 2026-05-31: Export/runtime VPK mode exposed that project graph pass discovery
  could throw from `recursive_directory_iterator::operator++` on denied
  directories, and that `builtin://textures/...citrus_orchard...vtexture`
  depended on a physical `builtin/textures` directory. The iterator now skips
  permission-denied entries and the default skybox falls back to a PE resource,
  avoiding the huge generated C++ header path.
- 2026-05-31: The same single-exe fallback is wired for Linux/macOS through
  `source/vultra_app/resources/builtin_assets_unix.S`, which embeds the raw
  `.vtexture` with `.incbin` and exposes start/end linker symbols consumed by
  `AssetSystem`. Android/WASM intentionally return unavailable here because
  they do not use the desktop `vultra-app` single-exe path.
- 2026-05-31: Export & Run could appear stuck after `ScriptSystem Initialized`
  because runtime post-configure re-opened the just-packed VPK to enumerate
  render graphs before logging anything. It now enumerates
  `IAssetService::registry()` and logs before/after graph reload.
- 2026-05-31: A second hang point was inside `DeclarativeRenderer::loadProjectGraphPasses`:
  in VPK runtime `res://` resolves to `/`, so physical Lua pass discovery could
  recurse from the filesystem root during render graph reload. Physical scanning
  is now skipped for root-like asset roots; registry/VFS loading still handles
  packaged pass scripts.
- 2026-05-31: Captured packaged runtime rules in
  `ai/knowledge/packaged-runtime.md` and started normalizing editor-only asset
  scans to use permission-tolerant recursive iterators. Remaining scans are
  editor/tooling paths and should follow the same rule when touched.
- 2026-05-31: OpenXR `XR_SESSION_STATE_EXITING` / `LOSS_PENDING` now closes the
  XR session and releases the XR backend instead of requesting app exit. Only
  instance loss remains fatal. A user/session-closed latch prevents immediate
  auto-restart while the scene still requests XR; it is cleared by an explicit
  `requestXRSession(false)`.
- 2026-05-31: The Inspector XR View component now has `Request XR Session`.
  It is only enabled when the serialized `XRViewComponent::enabled` flag is
  already true, and it does not mutate that persistent setting; it only calls
  `requestXRSession(false)` then `requestXRSession(true)` so a user-closed
  session can be reopened deliberately.
- 2026-05-31: Game View's `XR Disabled` empty state also exposes the same
  runtime-only `Request XR Session` action, keeping the recovery control next
  to the disabled XR preview instead of relying on serialized component edits.
- 2026-05-31: Project launcher default and stereo VR render graph templates now
  keep the `Ssao` pass top-level enabled while leaving `params.enabled=false`.
  Declarative graph validation removes top-level disabled passes before checking
  active references, so `DeferredLighting.ao = Ssao.ao` needs the pass node to
  stay active even when the SSAO runtime work is skipped. The launcher default
  template also keeps `Ssr` and `SsrComposite` top-level enabled with their
  params disabled, matching the current `resources/render/default.vrg.json`
  passthrough pattern.
- 2026-05-31: Verified the launcher `kDefaultRenderGraph` and
  `kStereoRenderGraph` raw-string JSON with `ConvertFrom-Json`, including
  `Ssao.enabled=true` and `Ssao.params.enabled=false`, then ran
  `xmake build -y vultra-app` successfully.
