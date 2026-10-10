# Vultra Handoff

## Cleanup and commit preparation — 2026-10-10

- The source tree excludes evaluation FBX/DDS/HDR binaries, asset/shader caches, compiled modules, exported
  SDKs, local editor settings, layouts and QA captures. Upstream asset and dependency attribution remains.
  Existing local assets, captures and layouts are preserved; no bulk ignored-file deletion is performed.
## Research viewport and assessment workflow completed — 2026-10-07

The user authorized all usability items in the audit below. This section supersedes that pending list and
the earlier synchronous/every-frame assessment description. No experimental reconstruction algorithms were added.

- Views supports All / Left / Right, preserving rendered/assessed eyes, aspect fitting and both-axis centering.
  Labels report actual texture dimensions. Ctrl-hover draws a 15x15 texel-center magnifier at 12x through the
  existing GUI texture, with coordinates and the selected texel highlighted; no CPU pixel readback is added.
- Options selects Result, Intermediate snapshot or LDR-FLIP map and saves visible/all original-resolution images.
  PNG preserves the displayed mapping; PFM preserves raw HDR/scalar values. Non-RGB intermediate mappings also
  export an unscaled selected-channel PFM, including alpha hole flags. Metadata records dimensions, domain,
  resource, project label, mapping and snapshot frame. Relative directories resolve against the project file;
  existing directories are rejected. Saves run after render/GUI/platform-window completion.
- Native research editor preview(label, resource, after_pass, mapping) requests a completed-frame snapshot.
  RenderGraph resolves named internal textures and can capture after their last writer when after_pass is empty;
  explicit writer names must be unique. The host retains the last successful graph/image on a rejected request.
  Friendly labels publish with the candidate image and persist through refresh. Editor view controls expose eye,
  content, requested V-Sync, matching and live-quality interval; save_images uses the same deferred save path.
  The version-1 ABI deliberately changed before public release: rebuild project modules against the matching SDK.
- Optional desktop Match viewport debounces the per-eye image region over three stable GUI frames. Presets,
  custom-size application and configuration restore disable it; OpenXR uses runtime sizing and independent
  benchmarks retain configuration dimensions. GUI eye/content selection itself never changes rendering.
- ImageReadback stages copies inside the existing render submission, restoring source states. All quality sources
  and staging buffers validate before any copy is recorded. At the next normal GPU completion, one bounded vtask
  worker job computes display/HDR metrics and CPU FLIP. Live quality samples every N frames only when idle.
  Candidate results and Magma heatmaps publish together on the main thread; failures preserve prior results.
  Resize/settings changes while a job runs do not invalidate its owned images; snapshot frames remain explicit.
  Sampling/copy conversion/publication still have a cost. Temporal pairs require actual consecutive samples;
  deterministic evaluation uses separate synchronous quality replay outside timed benchmark frames.
- GPU Profiler adds current renderer/route/choice context, requested desktop FIFO/Immediate control and acquire,
  fence-wait/present wall times. VRI may substitute a mode and has no resolved-mode getter; do not claim otherwise.
  Fence wait overlaps GPU execution. Swapchain changes occur between completed frames, with prior-mode recovery.
- Magnifier drawing/input was code-reviewed; physical mouse interaction, Linux, physical-headset and full-suite
  acceptance were not repeated. Existing assets, layouts, capture directories and unrelated changes remain intact.
  Temporary QA stays ignored. No commits or pushes were made.

## Viewport dimensions and research UI audit — 2026-10-07

Audit snapshot (all items were implemented by the follow-up above):

2. High: host Ctrl-hover texel magnifier. The reference shows a 15x15-texel region at 12x with a highlighted
   cursor pixel. Current Inspection has completed-frame CPU pixel values, but the main viewport lacks this
   convenient visual inspector. GPU texture cropping must not introduce per-frame CPU readback.
3. High: viewport Save image / Save all actions. Existing CLI captures and Inspection raw/mapped exports
   are available; the main viewport has no direct save menu. Reuse completed-frame capture and preserve
   original render resolution, explicit display/HDR domains and non-overwrite behavior.
4. Medium: project-named intermediate views (warped depth, holes, reconstruction levels) and an LDR-FLIP map.
   General graph texture Inspection already supports manual selection/channel/range/snapshot/export.
   FLIP error images already export after assessment, but the viewport only exposes absolute HDR error.
   Project extensions should define algorithm-specific output names; the host should handle presentation.
5. Medium: optional Match viewport resolution, as in the reference's debounced desktop resize. Current
   presets/custom sizes are intentionally independent of docking. Retain fixed resolutions for captures,
   benchmarks and OpenXR; viewport fitting must never silently change an experiment's render dimensions.
6. Medium: interval-based nonblocking live quality and timing context. The reference delays metric readback
   and samples every N frames. Current quality is manual or every-frame and CPU FLIP can pause interaction.
   Keep measurement workload explicit and outside independent timed benchmarks. Current Profiler has
   stage CPU/GPU/frame timings, but lacks the reference Stats panel's compact active-pipeline summary,
   acquire/fence/present stall breakdown and V-Sync status/control together.

Already present: saved per-project docking, reference/current/HDR-error preview modes, completed-frame
transactional configuration restore, replay command copying, camera track recording/play/step/rewind,
headset projection profiles, renderer presets/custom extents, parameter descriptions and disabled-feature
explanations, image metrics including FLIP, generic texture Inspection, raw/mapped export and GPU stage timing.
RMB look-drag latching across the image boundary and text-input capture are also implemented; do not list them
as missing. Prefer improving these entry points over rebuilding their underlying capabilities.

## Right sidebar and centered research previews — 2026-10-07

- Native research docking now follows the reference tool's proportions: Views fills the left/main area;
  a 26% right sidebar splits into upper project Controls / Experiment / Metrics / GPU Profiler tabs and
  lower Renderer Settings / Inspection tabs. The lower section uses 45% of the sidebar height. Default
  construction selects project Controls and Renderer Settings. Initialization also adopts this arrangement
  for saved layouts with Renderer Settings docked left of Views; existing right-side adjustments remain.
  Restore default docking remains deferred to the next GUI frame and layouts remain project-specific.
- Preview cells use the actual available region instead of a fixed height deduction. Each texture retains
  its aspect ratio and is centered on both axes below its label. This applies to current/reference/error
  previews and each cell in the two-row comparison. Camera hover remains restricted to image rectangles.
  No rendering, metrics, shader, graph or asset behavior changed.
## Research preview and understandable quality metrics — 2026-10-07

- Compared the metric domains: the reference project's ordinary metrics use tone-mapped display-referred colors,
  while this host had presented unbounded raw HDR with peak 1. MSE 126 legitimately produces about -21 dB; do not
  clamp raw HDR or change peak per image to hide it. The default Metrics table now shows bounded tone-mapped linear
  display PSNR, Rec.709-luma SSIM, RMSE and LDR-FLIP. All use the same clipped [0,1] linear images before sRGB.
  Raw whole-frame and selected HDR metrics remain separate, with an explicit negative-PSNR explanation.
- Measure quality and the native editor measure() action request one complete assessment, including FLIP. Removed
  the separate GUI button that measured HDR without FLIP. Advanced ROI/masks/PPD and every-frame temporal assessment
  are collapsed under Measurement settings. Snapshots identify their frame and become stale when cameras/settings
  change. Selected-region scope is visible; empty selection is N/A and perfect matches show Perfect/infinite PSNR.
  Full candidate results publish together after success. CPU FLIP remains synchronous and on demand: at 1280x1280
  the two-eye assessment can pause playback for a few seconds. Logs report each eye. This is outside GPU timestamps
  and independent benchmark timing, but it does reduce interactive FPS if requested every frame.
- Capture metrics keeps its existing raw HDR domain. displayMetrics adds explicit bounded-linear domain/peak,
  counts and scores; quality retains FLIP/selected HDR/temporal results. Export preserves an already-assessed ROI
  snapshot rather than replacing it with a whole-frame measurement. Benchmark quality CSV adds display_mse,
  display_psnr and display_ssim; existing whole_*/roi_* columns remain raw HDR. Preview switches no longer reset
  temporal pairing, because they do not change rendered images. Missing FLIP logs N/A rather than a negative sentinel.
## Shared scene performance regression — 2026-10-07

- GpuScene builds indexed local bounds once and updates world bounds with transform revisions. BuiltinRenderer
  prepares camera and light-frustum lists independently, including reflected/nonuniform/sheared transforms and
  touching boxes. Arbitrary game shader displacement retains its draws. Default Bistro has 1591 primitives;
  source/left/right submit 137/138/137 geometry primitives at the authored motorcycle camera.
- Native research defaults static shadow caching on, including when the manifest omits renderer overrides.
  Explicit project/configuration overrides are respected. Generic BuiltinRenderer defaults off. Completed maps
  are keyed by graph resource, cascade matrix, transforms and shadow program generations; alpha/sampler/view and
  sidedness changes invalidate them. Game shader materials disable reuse. Cached maps submit zero casters.
  completeFrame() publishes only after GPU completion; unacknowledged recordings are not cache hits. Native graph
  replacement invalidates caches. Direct callers must invalidate after untracked geometry/alpha writes or map
  replacement. Cached maps reject transient aliasing. Human contracts are in docs/research_workflow.md.
- Native CLI defaults validation on in debug and off in release, with explicit --validation on|off. Core Device
  and test defaults remain on. Logs, Profiler, captures and benchmark metadata report the actual mode. This
  supersedes the October 6 handoff statement that native benchmark runs always retain validation.
- All four final stationary HDR images are bitwise identical to the pre-fix captures, with validation on/off.
  A 32-frame moving-camera capture has identical cache-on/off HDR outputs and redraws all three views' cascades.
  Final desktop/benchmark logs contain no Vulkan VUIDs or errors; the static-FBX animation notice is expected.
  verify_cache_qa.py checks these results and preserves earlier QA. Assets, resolution and shading remain unchanged.
- Final test-rendering, test-research-tools, test-research-extension, test-properties, test-shader-surface and
  test-meshlets pass with validation enabled. Renderer tests include cold/warm image identity, unpublished-frame
  recovery, alpha/transform/camera invalidation and alias rejection. Surface/meshlet parity remains intact.
  Focused clang-format, clang-tidy, generated-API --check and git diff --check pass. Intentional extension failures
  still exercise recovery. No full-suite, Linux or physical-headset claim is made for this performance change.
- GPU-generated indirect draw lists, material batching and occlusion culling remain unimplemented in this host.
  CPU visibility and meshlet/task culling do not establish GPU-driven rendering. Such a change requires separate
  VRI capability/layout acceptance and image tests. No VRI/private reference checkout was edited. Previous user
  work remains intact; no commits or pushes were made during this investigation.

## Native research workflow and measurement tools — 2026-10-06

- `HeadsetProfile` stores per-eye extents, signed asymmetric frustum tangents, rigid eye-to-head poses, runtime
  identity and provenance. Supplied Index/Crystal/8K X profiles identify lab measurements rather than device
  specifications. Capture requires located OpenXR views; the runtime retains authority over XR rendering.
  Desktop internal extents remain independent of native profile extents. The exact located midpoint is shared
  by source-camera construction, profile capture and saved tracking offsets through `XRFrame::headPose()`.
- Version-1 research configuration JSON saves the exact `rigView`, readable camera pose/clipping, frozen tracking
  offset, optional profile/track, per-eye sizes, rendering/display state, both methods' resolved Pass parameters,
  reference snapshot, ROI/masks and PPD. Readers require the current fields; earlier temporary verification
  captures are not migrated. Restore validates/builds a candidate before completed-frame publication. Failure
  preserves the active state; finite CLI failures return failure. Saves validate first and publish atomically.
  Exact `rigView` avoids quaternion-round-trip perturbations of quantized warping. It also preserves authored roll.
- `CameraTrack` evaluates explicit frame-indexed keyframes with linear position/FOV/clips and shortest-path SLERP.
  Interactive recording, load/save, rewind, pause and step are host-owned. Benchmarks sample explicit indices and
  native updates use a fixed step; GPU throughput never advances the camera. Live free-camera input resumes
  the existing FPS world-up convention. Arbitrary state inside a native DLL is not automatically reset.
- Inspection lists named scene/project textures, maps RGB/scalar/luminance ranges, probes original RGBA floats,
  and exports PFM/PNG/metadata on request after completion. Optional endpoint/after-Pass selection inserts the
  existing `RenderGraph::captureAfterPass()` copy before an in-place overwrite; benchmarks omit this copy.
  Detached GUI viewports finish borrowing an old inspection preview before that texture is replaced.
- Extended quality retains whole-frame raw-linear-HDR RGB MSE/PSNR and Rec.709 Gaussian SSIM, adding ROI/binary-mask
  reductions, NVIDIA CPU LDR-FLIP/error maps and consecutive-frame temporal reconstruction residual. Empty selections
  are unavailable; SSIM requires complete 11x11 windows. Mask meaning belongs to the project. Temporal pairs reset
  across gaps/settings/parameters/shader changes but permit camera movement. Official unmodified FLIP header and
  BSD-3-Clause license are pinned at b475eb4bf394ab877c42166c9eb0a84a02cc5b14 under `external/flip`; notices enter the
  engine pack. LDR-FLIP evaluates tone-mapped linear RGB clipped to [0,1] at recorded PPD, not HDR exposure search.
- Shared rendering exposes ACES/None/Reinhard and exposure. None preserves signed exposed float HDR; UNORM display
  clips and encodes sRGB once. XR float output remains linear. Raw HDR metrics precede display transforms.
  Tone operator property metadata/C API IR/C# bindings were regenerated through the existing generator.
- Memory snapshots use VRI budget/usage and tracked allocator bytes, separating scene geometry/meshlets/textures,
  deduplicated active graph allocations and total VRI ownership. All memory locations enter the tracked total;
  driver VRAM usage is a separate measurement. No shadow allocator accounting or per-frame enumeration is added.
  Captures/benchmarks record asset hashes, engine pack, actual published project SPIR-V/compile/dependency hashes
  and native module hashes. Failed shader/pipeline replacements retain the previous published identity.
- Fixed two integration regressions found during QA: distinct live A/B parameters for the same method must not
  alias when resizing or enabling inspection, and built-in geometry must bind a graphics pipeline before its frame
  and meshlet descriptors. VRI chooses the descriptor bind point from the current pipeline; independent graphs
  ending in compute exposed the old ordering, which GUI rendering had masked. Renderer regression readback covers
  forward/deferred geometry after compute in the same command buffer and across repeated recordings.
- Windows release `vultra-app` and six affected tests build. `test-image`, `test-graph-definition`, `test-properties`,
  `test-rendering`, `test-research-extension` and `test-research-tools` pass. Intentional shader failures in the
  extension test exercise recovery. Focused clang-format/clang-tidy and codegen checks pass; tidy uses the existing
  `_CRT_USE_BUILTIN_OFFSETOF` workaround for MSVC UCRT constexpr offset checks. No full-suite/platform claims.
- Source, shader, asset, configuration and native module identities enable auditing, but matching a configuration
  is not a guarantee across different drivers or arbitrary XR floating-point head/eye factorization. Physical
  headsets, Linux and the full example/test suite were not revalidated for this change. The previously deferred
  simulator pipeline-cache VUID remains outside this task. Earlier user work is intact; no commits or pushes.

- The previous Bistro validation proved material channels and source/package consistency, but did not compare
  the lighting against the original renderer. Its nearly black backlit regions were caused by a missing term:
  pixelwise's raster OpenPBR evaluator adds `0.15 * baseColor * materialAO` even with IBL disabled, including metals.
  Directional direction/intensity already matched. This corrects the earlier description of the darkness as simply
  the intentional result of disabling IBL.
- RenderSettings now has explicit nonnegative, finite linear RGB `ambientColor`, default zero for existing projects.
  CPU/Slang frame layouts grow by one vec4 with updated offset/size assertions. Forward/deferred and the shared
  meshlet/game-surface lighting use the same fill. It is independent of IBL and direct-light shadows; reference
  path tracing remains unchanged and does not include this raster approximation. Only the existing frame uniform
  changes during edits; no geometry uploads, shader/pipeline creation or graph replacement is required for the fill.
- Visual side-by-side results clearly restore the wall, pot, backlit motorcycle and metal trim. Display RGB mean
  absolute difference from pixelwise decreases from 23.42 to 7.32 levels out of 255. This is a diagnostic comparison,
  not radiometric equivalence: current Vultra uses public OpenPBR and sRGB transfer; pixelwise uses its own OpenPBR
  implementation and gamma 2.2. Camera matrices/method selections match between the before/after Vultra captures;
  all six HDR outputs are finite. Comparison PNG/JSON and raw outputs are in the same ignored QA directory.
- Windows release host/targets build. test-rendering, test-properties and test-research-extension pass, covering
  analytic ambient RGB/AO on a backlit metallic surface in both raster paths, zero default/reset/live uniform updates,
  settings serialization, existing upstream BRDF/CSM/PCF/PCSS checks and extension shader failure/recovery.
  Focused clang-format/clang-tidy pass; clang-tidy uses _CRT_USE_BUILTIN_OFFSETOF for MSVC UCRT constexpr offset checks.
- Fresh complete Bistro VPK runs eight frames from a new host/package-only directory, with no SDK/source shaders.
  Ambient/IBL metadata survives packaging; all six HDR outputs are byte-identical to source execution. Source/package
  runs have no runtime errors or Vulkan VUIDs and the package compiles no shaders. Logs are in Vultra build/.tmp/lighting-*.
  Original pixelwise/VRF files and previous work remain intact. No new physical-headset/full-suite run or commits/pushes.

- FbxImportOptions separates default Phong import from explicit ORCA metallic/roughness. Specular G/B is
  roughness/metalness with unit factors; the reserved zero R channel is not AO. Base-color alpha uses mask cutoff
  0.5; emission textures use unit emission regardless of absent/zero Phong factors. `.DoubleSided` selects sidedness.
  Safe Properties70 reads avoid OpenFBX 0.9's uninitialized absent-property accessors. Generic Phong behavior remains
  separate. Tangent handedness accounts for flipped V and DirectX normals; existing raster/ray BC5 reconstruction
  already supplies positive Z and needed no shader change.
- Public VRF origin/master and GitHub HEAD both identify 0139ce5a4efda005f4794e5f32b6a67e001231a7. Its FBX
  material/normal conventions informed this change; its MIT notice is retained in external/vrf_fbx_license.txt
  and the embedded engine pack. Vultra keeps its own existing parallel geometry/image/texture pipeline.
- FBX import settings are stored and validated on ProjectAsset, forwarded by scene import and preserved in VPKs.
  Global CLI options are `--fbx-materials phong|orca` and `--fbx-normal-maps opengl|directx`; authored asset settings
  override them. Convention/orientation participate in the FBX cache recipe without a serialized-format version bump.
  Mixed-case FBX extensions follow the same behavior. VPK packaging collects consumed FBX texture dependencies
  automatically and rejects paths outside the project root. Raw DDS payloads are not duplicated into the cache.
- Windows release vultra-app and test targets build; focused clang-format/clang-tidy pass. test-asset-formats,
  test-asset-pipeline, test-scene and test-research-extension pass. ORCA fixtures cover cold/warm caches, separate
  conventions, missing properties, invalid manifest settings, mixed-case extensions and packaged metadata/DDS.
  Deferred GPU readback independently checks G/B roughness/metalness, DirectX BC5 normal direction/Z, unit emission
  and occlusion=1. Existing failure/recovery tests still run; intentional shader errors in their logs are expected.
- Initial cold texture preparation/import measured about 1.53 s, and the later warm import about 1.07 s on this
  Windows machine. The cache is about 0.1 MiB and contains generated metadata/data only. These are local observations,
  not benchmark guarantees. Full-suite, new meshlet/ray-query runs and physical-headset validation were not performed
  for this material import change. Private originals and earlier work remain intact; no commits or pushes.

- Version-1 research manifests accept a `renderer` object, stored by ProjectManifest without a server dependency
  and decoded by the host through the existing RenderSettings reflection. Unknown fields/types fail explicitly;
  `path` is reserved for `render_path`. Missing fields use library defaults. Zero light directions and unsupported
  mesh shading are rejected. Captures now include actual renderer settings/path. No API generation or ABI change.
- The host panel exposes all current RenderSettings fields, including path, shadows/map size, cascade controls,
  material/debug overrides and disabled unsupported mesh controls, plus camera/display/resolution settings.
  Renderer edits are staged until the next frame; graph rejection restores previous renderer settings too.
  Scalar light/IBL edits do not compile shaders or upload geometry each frame.
- Desktop size presets mirror study extents: 1024x1024, 1280x1280, 1440x1600, Index 2016x2240,
  Crystal 3234x3826 and 8K X Large FOV 6254x2962. All research extent boundaries now accept [11,8192].
  These presets change size only, preserving FOV/IPD; full desktop headset-frustum emulation was not added.
  XR retains runtime-native extents/projections and the existing render-scale control.
- Windows release builds and focused formatting/tidy pass. test-research-extension, test-gui-settings and
  test-properties pass, including renderer metadata roundtrip/rejection and a 6254-wide manifest.
  All three headset extents render finite runs using the shared stereo reference and an external small model.
  Bistro runs/captures confirm the light/IBL defaults and docking; HDR outputs are finite.
  A small deferred-project VPK runs from a fresh source/SDK-free directory, retains renderer defaults and yields
  six HDR images byte-identical to source execution, without runtime compilation or Vulkan diagnostics.
  Invalid property types, unknown keys and zero directions are rejected by the real host with specific diagnostics.
  Full-suite, interactive widget automation and physical-headset validation were not performed for this change.
  Existing work/private originals are preserved; no commits or pushes.

- ResearchProjectApp initializes its world-Y-up FPS rig from the main scene's selected camera via
  SceneRenderState. It retains bounds framing when no camera is selected or --model overrides the scene.
  XR still composes runtime eye poses/projections with this rig; authored roll is not retained.
- Windows release host/module build, focused clang-tidy/format, test-scene and test-camera --offline pass.
  An eight-frame desktop run captures the motorcycle at the expected angle; exported matrices match the
  recipe and all six 1280x1280 HDR images are finite. All 409 copied Bistro files are byte-identical.
  Expected static-animation/BLEND-to-cutoff warnings remain; no runtime error or Vulkan VUID was logged.
  A separate two-frame --model run verifies bounds-camera selection. Full-suite, new Bistro VPK and
  physical-headset runs were not repeated for this change. Original research files remain untouched;
  no repository was committed or pushed.

- Depth filling can bypass by copying its immutable input. Pull-push modes are 0 bypass, 1 donor depth
  filtering disabled, 2 depth-aware (default), all retaining shared sky/finite classes. Disabled graph
  stages still incur barriers/profiler overhead. ImageGrid uploads device-local indices only on first use
  or grid-step/extent changes, with an explicit upload fence; do not benchmark those initialization frames.
  No per-frame compilation, pipeline creation or scene geometry upload is added.
- Desktop resolution can change at runtime. XR render scale [0.25, 1] changes graph extents while eye blits
  still target native runtime image sizes. The UI API handles the pre-render XR waiting state without
  requiring a configured graph. Existing parameters survive resizing, and shared settings carry between
  input routes. Numeric/discrete/read-only edits are validated by host contracts.
- Save current parameters as reference gives slot 0 independent Pass instances even for the same graph as
  slot 1. Parameters freeze; poses remain live. Source/pipeline candidate failure retains the active graph
  and rolls back reference state/selections/extents; restore uses the named independent-eye reference.
  Snapshots remain session-local. Capture JSON now records each configuration's parameters, per-eye extent
  and referenceSnapshot flag. The host profiler capacity is 512 for two stereo pyramid configurations.

Verification on Windows x64 release/Vulkan:

- Desktop source and source-free VPK graphics-grid captures at 193x145 match per-eye HDR/difference PFM and
  display PNG bytes. The VPK runs from a fresh directory without SDK/project sources and logs no shader compilation.
  All tested HDR captures are finite. Existing published/previous temporary deliverables were not replaced.
- Forward visibility uses one uint per target pixel, a per-frame clear and ordinary global atomic min.
  Filling retains an immutable raw payload and emits a separate filled buffer. Gather filters bilinear
  taps by depth and emits RGBA32F color plus depth/validity alpha. FP32 deliberately avoids merging distant
  finite depth with the exact sky marker through half conversion; this is not a claim of bit parity with
  the old FP16 intermediate. Pull-push uses 2x2/floor-halved levels to 1x1, separate sky/finite donors,
  raster coarse levels and sparse in-place compute refinement at level zero. Retaining pre-fill pixels
  requires a capture/snapshot before the in-place update.
- CPU push constants are 80 bytes for warp/fill/gather and 16 for pyramid stages; matrices are explicitly
  column-major. View selectors (0 center, 1 left, 2 right) must match the graph imports and agree across
  the three reprojection stages. Read the frame during execution, never freeze poses at graph build.
  Descriptor sets, pipelines and pyramid allocations are created once; parameter edits do not upload geometry.
- Engine changes required by this graph: `Profiler(Device&, eventCapacity = 64)` preserves the existing
  default and lets the research host allocate 256 events. GPU regression records/resolves 80 actual draw
  events and checks zero capacity and overflow. `ShaderPipeline`'s options constructor accepts a cache
  directory; the research bridge uses `<project>/.vultra/shaders`, fixing caches disappearing with the
  extracted engine pack. Cooked loading still creates no compiler. The native regression checks this path
  alongside its existing compilation/pipeline-builder failure recovery.

Verification on Windows x64 release/Vulkan:

- Host/module builds, modified C++ clang-tidy and formatting pass. Targeted `test-gpu` and
  `test-research-extension` pass; the full 45-test suite was not rerun for this migration.
- An ignored native GPU harness exercises the real module through the extension bridge: exact identity
  splats, nearest collision resolution, repeated-frame clear/live poses, both scanline directions,
  single/multiple holes, sky sentinels, immutable payloads, gather bilinear/point updates and depth filtering,
  known-pixel preservation, finite and sky-only pyramid holes. Final shaders pass all analytic readbacks.
- New embedded executable and host-plus-VPK run in fresh directories with no project sources or SDK.
  Per-eye HDR, difference PFM and display PNG files match source captures byte-for-byte for both methods
  (host-plus-VPK checked for center-to-stereo). Logs show no runtime shader compilation.
  A second source-host process hits every project program cache, including separate refine graphics/compute entries.
- Meta XR Simulator runs both methods together: four application frames, three stereo submissions, mirror
  and raw/display captures, 157 timed Passes. The system-selected Varjo runtime reports no available headset;
  validation selected Meta only with a child-process `XR_RUNTIME_JSON`, leaving system settings unchanged.
  The deferred pipeline-cache VUID and simulator teardown-space warning remain visible. Physical XR is unverified.
## Local research host development — 2026-10-06

- Host `--sdk-dir` selects explicit research Slang include roots, including cooking. Invalid roots are rejected.
  The bridge regression puts a failing include in a stale project SDK and a valid include in the selected SDK;
  actual compute/readback and failure/recovery pass with the selected roots.
## Research vproject host — 2026-10-06

- `vultra-app` now dispatches projects with research metadata to a stereo research host. Legacy workbench
  documents retain their existing path. Directory, VPK and embedded-project executable loading are implemented.
- Native extensions receive optional `VultraHostApi::research`: borrowed VRI tables/device/cache, context-owned
  PassCatalog registration, scoped RenderGraph declaration/execution, current camera matrices and host-owned
  ShaderPipeline compilation/cooked loading. Registration ends before graph construction. Graph callbacks and
  instances stop before DLL unload; AssetSource outlives modules. No second RHI/graph executor or global registry.
- ABI versions remain 1. The HostApi layout deliberately changed; rebuild native/.NET modules against this
  revision. The existing generator and checked-in C# layout include the optional research pointer.
- `vultra-sdk` exports C ABI/VRI headers plus ordinary Slang libraries and upstream notices. The SDK is embedded
  in the host and exported by `--export-sdk`. File-by-file copying keeps repeated exports/builds from nesting trees.
  The VRI MIT notice is copied from the public v0.1.17 tag (6da269bf6c9ce39de037cf9c131f9da3df5fb50f).
- Research methods use source/left/right HDR/depth imports, plus deferred GBuffer imports when requested.
  Methods may return RGBA16F or RGBA32F; comparison outputs are RGBA32F. ToneMappingPass now accepts both
  HDR input formats and still validates unsupported inputs. FP32 difference storage removes the observed
  FP16 image-store truncation from raw scientific differences. No image-error tolerance was relaxed.
- Project controls include named A/B selection, numeric Pass parameters, exposure/difference display controls,
  desktop/XR mirror, external model/environment overrides, asset import/cache options and finite captures.
  Source rendering is culled until consumed; identical selections share one method instance. Source/left/right
  are reserved method Pass namespaces. Shader reload failures retain the last successful pipeline and show diagnostics.
Verification on Windows x64 release/Vulkan (RTX 4080 SUPER):

- Final all-target build and all 45 xmake tests pass, including the new native extension regression for metadata,
  duplicate/late registration, expired/foreign graph tokens, bounded compute readback, live parameters,
  compilation failure, pipeline-builder failure, recovery and completed-frame instance destruction.
- Forward/deferred baselines, nonzero swapped-eye differences and an active source-consuming method are verified.
  GPU FP32 differences equal CPU `abs(A - B)` pixel-for-pixel. Source activation/culling matches graph dependencies.
- Copied standalone host plus VPK, and an embedded project executable, run in an isolated directory without SDK
  or project source files. HDR PFM and display PNG pixels match the directory baseline; no runtime compilation occurs.
  QA outputs/logs are in ignored `build/.tmp/research-project-qa-20261006/`.
- Meta XR Simulator submits 7 stereo frames in an 8-frame run, with mirror and raw/display capture.
  The deferred pipeline-cache VUID and simulator teardown-space warning remain visible. Physical XR is unverified.


## Current state

`dev-VRI` retains one public static `vultra` library and direct VRI/RenderGraph access. Scripting and VGui are
optional static targets. All owned file and ABI versions remain `1`; changes deliberately break older files.
Human-facing contracts and commands are in `docs/guide.md`, `docs/architecture.md` and `docs/shader_system.md`.

- RuntimeContext owns desktop resources, RenderingServer, PassCatalog and ObjectTypeCatalog. GPU RIDs, runtime
  ObjectIds and persistent asset/node IDs are distinct. SceneGpuSync applies transform/material changes without
  geometry reupload; mesh membership/model changes rebuild after GPU completion. Native/Lua/C# setters share
  the scene's camera, light, environment and material state.
- EditorGui wraps ImGui; VGui wraps RmlUi with embedded texture styles. VGui supports window-backed and offscreen
  construction, but advanced masks/transforms/layers/effects remain unsupported. Its event bindings detach before
  their callbacks expire. The workbench edits existing scene objects and graphs, not arbitrary scene topology.
- AssetSource owns an explicit directory or VPK. Player, batch/Python, CPU importers, cooked game shaders and
  VGui read project resources directly. Package paths identify entries, not extracted files. Native/.NET sidecars
  and currently Lua scripts are materialized selectively and removed after script hosts stop. Only the embedded
  engine-shader bootstrap still extracts its resource pack.
- Native extensions, C++ node scripts, Lua and safe C# node scripts share the generated ABI. C# uses .NET 10;
  its current reload host preserves serializable instance fields, while native/Lua local state resets. Failed
  replacement keeps the old module. General node wrappers/discovery remain partly handwritten. C# player exports
  require .NET/host files; the base native/Lua player has no engine DLL sidecars.
- The first E0 slice supplies PropertyValue, typed accessors, defaults, JSON codecs and an explicit type catalog.
  Generated descriptors belong to their camera/light/environment, material and renderer modules. Inspector and
  persistence consume them; scene setters still validate domain relationships and mark revisions. Safe C# settings
  records/conversions now come from the same libclang IR. There is no global registry or second binding pipeline.
  Workspaces save all annotated renderer settings: `renderer.path` is numeric and `seed` is top-level. Scene JSON
  retains its existing keys/color arrays. This workspace change is deliberately version-1 breaking.
- ExperimentSession is the shared windowless renderer; optional ExperimentHost adds scripts and checked session
  handles. CLI/Python replay version-1 experiment descriptions. Offline script GUI callbacks and hot reload are
  disabled; scripts must make their own RNG deterministic. Batch/player can run without xmake on the target.
- PassCatalog installs shared tone mapping; project HDR compute has direct C++/JSON readback parity. HDR and
  display-encoded RGBA8 outputs are supported. Internal shadow/G-buffer/lighting catalog adoption is incomplete.
  ReferencePathTracer supplies progressive opaque OpenPBR transport, deterministic seeds and seven AOVs; remaining
  material/filtering/motion limits are in `docs/reference_renderer.md`.
- RenderGraph has explicit texture history, opt-in exact-description transient reuse and hierarchical CPU/GPU
  events. Reports associate resource allocation, producers and images. Offline RenderDoc creates a real `.rdc`;
  automated replay and Nsight inspection remain unverified. Async compute is not implemented.
- Game `.vshader` and native `.slang` sources cook to typed, checksummed `.vshaderc` programs. SPIR-V reflection
  owns shader layouts. Packaged built-ins use cooked programs without watchers; source editing retains Slang and
  FileWatch. Failed compilation/publication retains the prior valid artifact/pipeline. Linux dependency watches
  cover include precedence without recursively watching build/metadata trees; synchronous reload publication
  handles delayed notifications. The pinned VRI cube-array patch is documented in `external/vri/README.md`.

## Verified evidence

Current Linux work uses SDL3/release/Vulkan. GPU checks are offscreen with display variables unset; no desktop
window or workspace switch was used. Workspace 5 is reserved for necessary native-input checks.

- `build/.tmp/pull-offline-m3LE3l/`: all-target build, 36 CPU/offscreen regressions, shader include/reload failure
  recovery, Surface/cooked mesh/cube-array readbacks, standalone batch/workbench/Python QA and shader diagnostics.
- `build/.tmp/package-source-UG2jNf/`: all-target build, 16 affected tests plus selective module cleanup, corrupt
  cache recovery and archive lifetime across working-directory changes. CLI/Python copied-artifact and multilingual
  VPK image parity pass; source-free game shaders match authored-source output.
- `build/.tmp/player-source-amxwOo/`: all-target build and four affected regressions. VGui directory/VPK/embedded
  images match exactly, including RCSS/font/PNG/checkbox resources and failure recovery. The real Research HUD was
  composited over its scene and visually checked. Standalone native/Lua/C# batch parity passes.
- `build/.tmp/merged-roadmap/`: affected E0 builds including Research, four selected tests with two offscreen GPU
  regressions, eight generator test methods and real codegen consistency. Managed safe-value/default/ABI round
  trips and callback-phase/zero-allocation checks pass. Inspector/workspace readbacks cover held-drag updates,
  invalid-edit recovery and save/reopen parity. Focused clang-tidy, formatting and whitespace checks pass.
- `build/.tmp/cleanup-ekTVbI/`: final all-target build and nine CPU/offscreen regressions pass. Generator cleanup
  preserves checked-in outputs; eight generator methods, managed-control, 81 changed C/C++ format checks and
  AssetSource clang-tidy pass. The final test log has no unexpected GPU validation diagnostics.

### Windows continuation, 2026-10-06

Pulled `dev-VRI` from `cee4959d` to `2fe875ee` with a clean initial working tree. Current verification uses
MSVC/MT, GLFW/Vulkan, RTX 4080 SUPER and .NET 10.0.301. Logs and isolated layouts/captures are under
`build/.tmp/windows-pull-20261006-1791276446331/`; standalone scripts also print their unique output directories.

- All-target builds pass. The pulled revision passes all 43 registered xmake tests. After the local RT stage fix,
  all 13 affected shader/GPU/meshlet/reference-renderer regressions pass. These test logs have no Vulkan VUIDs.
- Standalone offline QA passes 37 invocations, including copied-executable VPK/native/Lua/C# parity. Workbench QA
  passes 15 invocations, including save/reopen, reference AOVs, lighting edits, batch parity and invalid-input
  recovery. Python checks pass owned NumPy/CLI HDR parity, closed-session/thread rules and copied-library/VPK use.
- Eight generator methods, real codegen consistency, managed safe-value/default/ABI and zero-allocation controls,
  and native Slang completion/definition/source-mapped diagnostics pass.
- All 22 supported example configurations complete six frames and produce PNGs: all basic, scene, ray and XR
  modes; UI and scripting; research Forward/deferred/data graph; and game shader cooked/edit/deferred/meshlet
  modes. Representative Workbench, Sponza meshlet, game material, ray and XR mirror images were visually checked.
  This is finite-frame coverage, not a manual interaction or headset-display acceptance test.
- A copied standalone player runs with an external VPK and with an embedded VPK, from a directory without project
  sources/build tools. Both render six frames with no warning/error/VUID diagnostics and identical 1024x768 PNGs.

Local fixes are uncommitted: Workbench QA now writes numeric `renderer.path = 2` and top-level `seed`, matching
the deliberate workspace schema change. Shared shader compilation and archive validation previously omitted all
RT stages; this broke both ray-triangle and ray-cornell despite the full registered suite passing. Compile-stage
mapping, explicit selection, save validation and archive decoding now accept all six RT stages. A regression
covers discovered/explicit entries and source-free bytecode/stage roundtrips; combined stage bits remain rejected.
Both actual ray examples now capture successfully without validation diagnostics. The generated renderer
descriptor correction contains whitespace only; current Windows codegen consistency passes.

Remaining limits:

- `example-shader --meshlets --deferred` fails explicitly: NaiveDeferred still requires indexed geometry.
  Forward meshlets and indexed deferred pass separately; mesh-driven G-buffer drawing is not implemented.
- The active runtime is **Pimax OpenXR**, not Meta XR Simulator. Both XR examples complete six application/eye
  frames and capture their mirror, but report `VUID-VkDeviceCreateInfo-pNext-02830`: an extra timeline-semaphore
  feature structure accompanies Vulkan12Features. The pinned VRI source enables timeline semaphores in
  Vulkan12Features, without a separate timeline structure; Vultra forwards that create info unchanged through
  `xrCreateVulkanDeviceKHR`. This points to XR runtime/layer insertion, but is not a proven root-cause diagnosis.
  Do not suppress the diagnostic or report clean headset acceptance. This differs from the previously deferred
  pipeline-cache issue.
- Built-in path-tracer cooking reports Slang E41012 (implicit profile upgrade). It is not a GPU validation error,
  but capability/profile declarations still need a deliberate review.
- Modified C++ files pass clang-format 18.1.8 and clang-tidy with no project diagnostics. A broader dry run of the
  81 pulled C/C++ files still reports formatting differences in 16 unchanged files. They were not bulk reformatted;
  reconcile formatter versions/outputs before claiming a clean cross-platform formatting gate.

The proposed internal replacement shader design is in `docs/shader_system_design.md`. It defines typed Boolean/
exclusive Enum keywords, Material/Pipeline/Pass ownership, link/module/preprocessor lowering, legal selections,
explicit cooking coverage and separate selection/program/layout/pipeline identities. vshadersystem is not a
dependency. Keyword domains and retention remain proposals; the scoped module/program reuse slice described
below is implemented. The existing
runnable shader guide links this design and records the indexed-deferred limitation.

Earlier Linux continuations and Windows logs do not establish D3D12, Windows SDL3, clean-machine delivery or
physical headset display/input correctness. Retain those separate gates.

### Shader compilation performance, 2026-10-06

Local uncommitted implementation and QA are under
build/.tmp/shader-performance-20261006-1791283066759/. No experimental shader-system dependency was added.

- ShaderCompiler owns one lazy Slang global session and a bounded set of 16 primary-module IR snapshots.
  Every request uses an isolated session before linking constants/modules. Native sweeps can retain a context;
  source-backed ShaderPipeline retains one across reloads. One game asset compilation shares a context across
  Passes/variants. Static convenience APIs still work; source-free cooked loading never creates a compiler.
- Both authoring paths use the same checksummed development program cache (.vultra/shaders/*.vshadercache).
  The hash only selects a file: full canonical requests, captured content hashes and ordered include resolution
  establish a hit. Hits return owned SPIR-V/reflection without Slang initialization. Failed compilation retains
  successful cache files; corruption and lookup collisions explicitly recook. Shipping .vshaderc version 1 and
  strict typed readers remain unchanged; development cache request text is not packaged.
- Device owns an in-memory VRI pipeline cache. Built-in renderer/IBL/tone mapping/reference compute, ShaderMaterial,
  TextureBlit, VGui and the direct graphics/compute examples pass it in ordinary VRI descriptors. This supplements
  existing pipeline-object reuse. No disk driver cache or RT pipeline-cache support is claimed.
- The cooker now accepts all six RT stage names as explicit --entry values. Cooking raygen/miss/closesthit from
  the real triangle source succeeds with its actual common/built-in/external include roots.

Measured on the existing Windows/RTX 4080 SUPER setup, with fresh output paths to avoid whole-asset cook skipping:

| Cooker input | Previous cold process | New cold process | Program-cache process median, five runs |
| --- | --- | --- | --- |
| PaintedMetal game asset, all generated Passes | 4.121 s | 1.804 s | 180.46 ms |
| Native built-in Forward Slang | 0.834 s | 0.585 s | 61.95 ms |

Cold figures are single observations, not statistical guarantees. Process times include startup and artifact
writing. The small compute GPU probe separately measured frontend cold 129.6 ms versus reused IR 4.0 ms, and
reopened program-cache loading 1.1 ms. These gains do not remove new specialization linking/SPIR-V costs.

Verification:

- All-target builds pass. All 44 registered xmake tests pass after the shared compiler and device-cache changes.
  After retaining the native reload context, all six affected native/GPU/compiler/meshlet tests pass again.
- New GPU checks cover native link-constant isolation/reflection, same-timestamp include edits, earlier search
  roots, macros, changed linked files, compilation failure/recovery, corrupt caches and checksum-valid lookup
  collisions. Actual Low/High game variants produce distinct expected values and survive source-free loading.
  Native compute produces the same readback with and without the device driver cache.
- Eleven finite-frame examples pass with PNG captures and no Vulkan VUIDs: basic mesh, all three ray modes,
  research Forward/deferred/data graph, and game cooked/edit/deferred/meshlet. Game cooked/edit/meshlet images
  match exactly, as do the cooked game and ray-triangle images against the previous QA captures.
- The new copied runtime renders six frames from an external VPK with only system directories on PATH and no
  project source files in its run directory. Its image matches the previous standalone player capture.
- Modified C++ code passes configured clang-tidy checks without project diagnostics. All modified C/C++ format
  checks, generated-code consistency and git whitespace checks pass.

Limits: native source reload is still synchronous. Game candidate compilation uses the existing vtask worker;
GPU creation/publication remains on the main thread. Module IR is only in memory and only reuses the primary
module; linked modules/composed groups are not separately cached. Game metadata edits conservatively invalidate
dependent programs. Typed keyword domains, Used/AllLegal retention, domain projection and selection-to-program
deduplication remain proposed gates in docs/shader_system_design.md. D3D12/DXIL and the earlier XR issues were
not validated or changed by this performance work.

### Research correctness and composition, 2026-10-06

The requested high-priority slice is implemented in the existing Vulkan research path. Logs and captures are under
`build/.tmp/research-gates-20261006-1791291146781/`. Changes remain uncommitted alongside the earlier shader performance work.

- Shared built-in shadow, skybox, G-buffer and deferred-lighting implementations now have typed PassCatalog
  contracts. `examples/research/deferred.vgraph` is a complete explicit composition. ExperimentSession and
  ResearchWorkspace bind owned renderer contexts and omit the automatic prelude for such graphs. Direct C++ can
  call the same stage builders without a SceneTree. An intermediate-only G-buffer graph culls shadows/lighting;
  rejected duplicate stages and mismatched ports retain the previous graph and completed image. The first marked
  output still obeys the existing HDR/display-color contract; additional marked outputs expose raw AOVs.
- Reference environment sampling uses exact lat-long cell solid angles and a luminance alias table with a 5%
  uniform-sphere mixture. The stored float alias probabilities determine the PDF used by both MIS paths. GPU
  upload/readback occurs only when the environment handle changes, between completed frames. Float RNG midpoints
  use 23 bits so rounding cannot produce 1. Material/environment lookup explicitly remains bilinear mip 0;
  primary jitter integrates that pointwise model. No implicit ray-cone or raster-derivative approximation was added.
- The existing libclang generator reuses scene PODs in the experiment ABI and generates frozen Python camera,
  light, environment and material dataclasses from the same reflected fields/defaults. Transform arrays convert
  between ordinary Python 4x4 indexing and the native column-major ABI. Persistent UUIDs resolve only within the
  session's SceneTree; direct-model sessions and wrong kinds fail explicitly. Numeric edits update existing GPU
  data on the next step, without geometry uploads.
- Reports share ExperimentSession provenance: the scene/graph snapshot, build mode/Slang toolchain, declared asset,
  entry-scene/environment and available shader-artifact hashes, plus actual importer-consumed model/buffer/texture
  dependency hashes. Cache hits preserve those dependencies and scene imports merge/check shared-source records.
  Reporting hashes outside measured frames. Declared file hashes are read at report time; consumed dependency
  hashes describe import-time bytes. The caller still supplies a source/build revision and retains external state.
- ImageView maps RGB, individual RGBA channels or luminance into an explicit finite increasing range without
  mutating raw floats. Workbench mapping writes a separate preview texture outside experiment timings. Pixel
  readback happens only on request after completion. Python returns owned mapped/raw arrays and raw RGBA probes.
  Blit push constants use eight scalar fields (32 bytes), checked against Slang target reflection; a vector padding
  member had introduced a 44-byte SPIR-V layout during QA and was removed. The preview explicitly transitions from
  color attachment to shader resource before ImGui sampling.

Verification on Windows/MSVC-MT/GLFW/Vulkan, RTX 4080 SUPER:

- All-target build and the final UI target build pass. All 44 registered tests pass in the final full run with no
  Vulkan VUIDs. Expected malformed-input/cache/reload fixtures retain their diagnostic coverage.
- Environment tests establish constant/black-map uniformity, normalized PDFs, bright-cell sampling frequencies and
  a known sphere integral. Independent upstream OpenPBR C++ midpoint quadrature versus GPU white furnaces covers
  diffuse, dielectric, metal and coat (largest observed channel error about 0.0012). A high-contrast filtered HDR
  fixture uses independent CPU bilinear lookup/integration (red CPU 1.14019, GPU 1.14607). Existing analytic,
  normal/material, mirror/alpha, reset/failure recovery and deterministic Cornell convergence checks remain.
- Python/CLI raster and reference HDR parity, typed edit/restore/error handling, environment changes, matrix layout,
  raw probes, mapped channels, independently computed file hashes, explicit-stage parity and copied-library/VPK
  delivery pass in `python-final.log`. Actual importer dependencies survive cold/warm cache paths in native tests.
- Workbench QA passes 17 invocations, including explicit stages followed by the same post-processing as the
  default graph, byte-identical final PNGs, save/reopen, marked AOVs, reference rendering, failed edits and batch
  parity. The actual mapped Outputs panel was visually inspected in
  `build/.tmp/workbench-qa-3q7mhjpf/lighting/workbench.png`.
- Offline QA passes 37 invocations, including copied-executable VPK and native/Lua/C# project parity with build tools
  absent from PATH. Ten generator test methods and real generated-code consistency pass. All 57 changed/new C/C++
  files pass clang-format 18.1.8; configured clang-tidy passes on the 21 affected research C++ units without project
  diagnostics, including a repeated check of the final UI fix. Git whitespace checks pass.

Scope remains the documented opaque OpenPBR subset and alpha masking on Vulkan. Game Surface functions are not
automatically ray-traced. Indexed deferred composition does not imply mesh-driven G-buffer support. Orthographic
ray origins, footprint-filtering approximations, deformation/SDK temporal signals, other material domains,
D3D12/DXIL, clean-machine deployment and physical XR/native interaction remain separate capability/acceptance gates.
Typed keyword retention and persistent driver-cache/async compilation work remain in the shader design/performance
handoff above; do not label these unimplemented features as completed merely because the current research tests pass.

## Next gates

Use `docs/research_milestones.md` for the merged E0–E7/M0–M7 roadmap and `docs/future_tasks.md` for the audited
legacy feature inventory. The dev-next audit uses commit `d8fe93850d7dbeeebc6992476566d7f70bc6ca86`; it does not
justify bulk ports.

- Finish E0 object/property references, arrays, factories and generic generated ABI access. Then implement explicit
  script attachments/exports/reload state and editing history in `example-scripting`. Safe records alone do not
  complete E0 or the multi-language engine model; Undo/Redo and E1–E7 remain open.
- Complete embedded engine-resource direct reads and Lua stream loading. Base runtime exports should retain the
  single-EXE contract; C# and optional vendor-plugin dependencies require explicit delivery manifests.
- M7 remains open: VRI implements D3D12, but Vultra selects Vulkan and pinned static Slang disables DXIL. Establish
  reproducible DXIL cooking and actual Windows draw/compute/readback before exposing backend selection. Linux
  development binaries reference GLIBC_2.43; choose a release sysroot for clean-machine delivery.
- Current Windows GLFW/Vulkan AssetSource/E0 evidence is above. Still validate Windows SDL3, manual native input,
  Wayland/X11 and clean Windows/Linux deployment.
  Retain unresolved Hyprland requested-resize/detached-viewport assertions and interactive Wayland picking.
- Implement the internal shader design one verified gate at a time; do not import the experimental shader runtime.
- Physical XR display/input remains unverified. Retain the current Pimax feature-chain issue and earlier simulator
  pipeline-cache/timestamp/extension diagnostics; neither establishes clean headset acceptance. The .NET WASM AOT
  probe established toolchain/ABI evidence only. Web, NVIDIA SDK passes, async compute and optional browser streaming
  need separate input, synchronization and delivery validation.

