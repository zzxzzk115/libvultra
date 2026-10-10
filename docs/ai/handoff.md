# Vultra Handoff

## Cleanup and commit preparation — 2026-10-10

- The maintainer authorized cleanup, English commits and pushes for both libvultra dev-VRI and PVW.
  The pending work is grouped into asset/import packaging, research/rendering foundations, native host/SDK
  integration and documentation. PVW is a standalone native research vproject, not an engine submodule.
  Four engine batches were pushed successfully. PVW has two local commits and a clean main branch, but no origin.
  Creating the proposed private zzxzzk115/pvw remote was rejected by automatic approval review because the
  destination, owner and visibility need explicit maintainer authorization. No PVW source was uploaded;
  the destination question remains pending. Do not create or push a new remote without that answer.
- The source tree excludes evaluation FBX/DDS/HDR binaries, asset/shader caches, compiled modules, exported
  SDKs, local editor settings, layouts and QA captures. Upstream asset and dependency attribution remains.
  Existing local assets, captures and layouts are preserved; no bulk ignored-file deletion is performed.
- All 86 pending project C++ files in libvultra and 20 in PVW pass clang-format. Generated API --check is
  current. Windows release host/module builds and test-asset-formats, test-research-tools,
  test-research-extension, test-rendering and test-gpu pass. Intentional failure/recovery diagnostics in
  tests remain visible. No new rendering algorithm or API change is introduced by this cleanup.
- The final validated eight-frame PVW run has clean GPU diagnostics; all four 1280x1280-per-eye HDR exports
  remain byte-for-byte identical to the October 7 captures. Evidence stays under each repository's ignored
  build/.tmp/commit-cleanup-20261010. Short validation-enabled startup statistics are not performance results.
  Linux, physical-headset and complete example-suite acceptance were not repeated for this cleanup.
- PVW README now describes background CPU FLIP consistently with the implementation. Its development
  instructions identify the matching libvultra revision after the grouped engine commits are created.

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
- PVW Debug previews names source HDR/depth, warped/filled depth, binary holes, pre-inpainting gathered color and
  selected finite/sky pyramid levels. Optional buffer-to-texture depth previews are culled when not inspected.
  These are explicit channel-mapped snapshots, not continuous previews or additional assessed algorithm outputs.
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
- Windows release host, shipping PVW module and an ignored real-editor QA module build. test-gpu,
  test-research-tools and test-research-extension pass with validation. The tools regression stages two different
  clears/copies in one submission for RGBA32F, RGBA16F and BGRA8, then verifies each original image and invalid use.
  Changed C++ passes clang-format/clang-tidy; generated API --check is current. Human contracts are in
  docs/research_projects.md, docs/research_workflow.md and PVW README.
- Ignored QA is under PVW build/.tmp/research-ui-all-20261007; verify_complete.py independently checks unchanged
  four default HDR images, single/all original-resolution saves, binary alpha/PFM/PNG parity, friendly labels,
  pyramid extents, non-overwrite rejection, interval sampling, resize during assessment and viewport matching.
  V-Sync off/on and invalid-preview recovery pass without GPU diagnostics. The final 240-frame normal release
  1280x1280-per-eye run reports local smoothed 100.0 FPS / CPU 0.67 ms / GPU 4.16 ms; these are not paper results.
  A validated benchmark has two timed rows and six separate quality rows with display/FLIP columns, ROI 256x256
  and zero static temporal residual after the first pair. No FLIP computation enters the timed graph.
- Magnifier drawing/input was code-reviewed; physical mouse interaction, Linux, physical-headset and full-suite
  acceptance were not repeated. Existing assets, layouts, capture directories and unrelated changes remain intact.
  Temporary QA stays ignored. No commits or pushes were made.

## Viewport dimensions and research UI audit — 2026-10-07

- Each native research preview label now reports the displayed texture's actual width and height. Dimensions
  come from the selected method/eye texture descriptor, not a resolution preset, window size or CPU draft.
  Hovering the label explains the distinction between render size and fitted preview size. Existing aspect
  fitting, centering and camera input remain unchanged. Human usage is updated in docs/research_projects.md
  and PVW README.
- Windows release host build, focused clang-format and clang-tidy pass. Ignored eight-frame validated captures
  under PVW build/.tmp/viewport-resolution-20261007 show 1280x1280 default stereo and a non-preset 1024x768
  reference/current comparison. Labels agree with exported image extents, diagnostics are clean, and the
  four default HDR images remain bitwise unchanged. No Linux or physical-headset acceptance was repeated.
  Existing work remains intact; no commits or pushes were made.
- Reviewed the reference tool's viewport panel/window, save hooks, controls, live-quality sampling and Stats
  against the host and PVW editor. Remaining usability work is listed below; this audit does not claim or
  authorize a bulk migration of experimental algorithms.

Audit snapshot (all items were implemented by the follow-up above):

1. High: host viewport All / individual-eye selection. Current preview modes select reference/current/error
   pairs but cannot enlarge a single eye to the full image area. A source/center view also needs a project-owned
   named output selection rather than hardcoded PVW knowledge in the host.
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
- Windows release host build, focused clang-format and clang-tidy pass. Ignored QA outputs are under PVW
  build/.tmp/right-sidebar-20261007. Eight-frame validated runs confirm fresh and previous-left layouts,
  retention of a custom 488-pixel right sidebar, and centered current/two-row comparison screenshots.
  All four HDR images remain bitwise identical to the earlier performance captures; logs contain no VUIDs
  or application errors. The rejected --view both QA invocation was corrected to documented --view compare.
  Short validation-enabled run statistics include startup and are not performance measurements.
- Human usage is updated in docs/research_projects.md and PVW README. No full-suite, Linux or physical-headset
  acceptance was repeated for this desktop layout change. Existing user layouts and prior work were preserved;
  no commits or pushes were made.

## Research preview and understandable quality metrics — 2026-10-07

- Desktop and headset preview default to the current result. PVW Controls / Preview selects Reference, Current
  result, Reference + current or HDR difference; parameter reference controls are collapsed by default. Preview
  selection changes presentation only. Reference rendering remains available to the existing comparison/metrics
  graph. This is not an on-demand reference-rendering optimization. CLI --view and saved preview selections remain.
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
- Host and shipping PVW module build on Windows release. test-research-tools passes with validation, including
  analytic negative HDR PSNR and a GPU signed/HDR-vs-bounded-display regression. Changed C++ passes clang-format
  and clang-tidy, generated API --check is current, and whitespace checks pass. API layout/version remain unchanged.
- QA is under PVW build/.tmp/metrics-ui-20261007. An ignored copy of the project module requests measurement once
  through the actual editor API so finite-frame screenshots contain real metrics. The ignored root .log manifest
  uses the original asset root; containment checks were preserved and no large assets were copied or modified.
  Current/reference/combined previews, ROI preservation, empty selection and perfect match pass with validation.
  The 1280x1280 authored view reports display PSNR 42.09/39.77 dB and FLIP 0.01385/0.01669; these are QA observations,
  not publication results. Four raw HDR images are bitwise identical to the previous performance captures.
  verify_metrics.py independently checks raw MSE, display formulas, selected counts, FLIP availability and clean
  diagnostics. A two-frame timed benchmark has six separate quality rows (three stereo pairs), valid display
  columns and zero static temporal residual after the first pair. No FLIP work enters the timed graph.
- Human contracts are in docs/research_workflow.md, docs/research_projects.md and PVW README. Physical-headset,
  Linux and full-suite acceptance were not repeated for this UI/assessment change. Previous work is intact;
  no commits or pushes were made.

## Shared scene performance regression — 2026-10-07

- Investigated the reported roughly 10 FPS default PVW/Bistro comparison against the reference renderer.
  Its base raster pass also submits CPU indexed draws: missing GPU-driven rendering alone did not explain the
  regression. The migration omitted conservative primitive visibility and static shadow reuse, and the native
  research host forced Vulkan validation on in release. The reference has camera/light AABB culling, material
  binding reuse, release validation off and a shared cached directional shadow map. Vultra retains its existing
  four-cascade CSM per view; it does not replace this with a lower-quality single map.
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
- Windows release RTX 4080 SUPER QA is under PVW build/.tmp/shared-scene-20261007. The full 1280x1280-per-eye
  default comparison still renders source plus two reference eyes, PCF, PVW, comparison and UI. The final 240-frame
  stationary run reports 98.9 FPS / CPU 1.33 ms / GPU 4.24 ms; continuous 128-frame camera motion reports 78.9 FPS /
  CPU 1.67 ms / GPU 10.64 ms. These are local smoothed window statistics, not publication benchmark estimates.
  The earlier 65.8 FPS intermediate run retained cacheShadows=false because reflected decoding reset the research
  constructor default; the final host merges that default before decoding, preserving explicit false overrides.
- Six independent configurations have 16 warmup and 32 measured frames, tested with validation both on and off.
  Normal-release stationary stereo/PVW median frame totals are 2.96/2.17 ms, GPU 1.88/1.83 ms; moving stereo/PVW
  totals are 7.26/4.25 ms. Before the fix, forced-validation totals were 153.04/87.16 ms and CPU recording
  120.76/64.36 ms. Keep validation/clock/workload differences explicit; isolated methods omit reference/comparison,
  display and UI and must not be reported as the full interactive FPS. Shadow-off runs are diagnostic only.
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

- Implemented the requested medium/high-priority generic research tools in the native `vultra-app` host.
  Project modules still own algorithms and their UI; PVW remains a small `.vproject` with pure Slang and native
  Passes. No new RHI, graph executor, registry, experiment manager or project-side tests/docs were introduced.
  Human-facing contracts and commands are in `docs/research_workflow.md` and `docs/research_projects.md`.
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
- Independent benchmark plans select one method per graph and explicit parameter sweeps (up to 256 runs).
  Each run gets its own warmup, timestamps, CPU phases, per-frame/per-Pass CSV, resolved configuration and metadata.
  The timed loop excludes comparison/reference/display/GUI/present/readback/export/hot-reload polling, and retains
  validation. Geometry is imported/uploaded once. Optional quality replay uses a separate graph with matching
  warmup, shader-frame sequence and camera samples; it never enters timing summaries. PVW provides
  `benchmarks/default.json` with reference plus compute/graphics and plain/depth-aware inpainting combinations
  for center-to-stereo and left-to-right paths. Short QA runs are not publication benchmarks.
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
- QA lives under PVW `build/.tmp/research-workflow-20261006`, preserving earlier outputs and original assets.
  `capture-exact`/`replay-exact` have identical three-view matrices and four A/B eye HDR images (MSE 0).
  Nine `benchmark-validated/run_N` runs have three timing samples and four separate quality rows each, static
  temporal residual 0, no display/GUI/Present timing rows and no Vulkan VUIDs. Earlier `benchmark-warmed` outputs
  exposed the descriptor issue and are retained as failed QA, not accepted benchmark results.
  `profile-track-final`/`profile-track-final-replay` use asymmetric 256x256 and 240x256 eyes plus a six-frame track;
  their three camera matrices and four HDR images are identical. `delivery-final` starts with only the latest host
  and immutable cooked VPK, runs four finite frames, performs quality assessment with no runtime shader compilation,
  and matches all four source HDR outputs exactly. A hard link avoids duplicating the existing 1 GB VPK data.
  `xr-final` captures Meta XR Simulator 1.72.0's 1440x1584 geometry at 25% render scale, with 11 rendered stereo frames
  over 12 application frames. `xr-final-replay` preserves all three camera matrices/projections and four HDR images
  exactly for this stationary simulator pose. CPU regression also verifies nonzero tracked height/orientation to
  1e-5 matrix tolerance; arbitrary real-headset sessions are not claimed bitwise. Simulator logs retain only the
  deferred pipeline-cache VUID and its existing undestroyed-space warning. Desktop/package/benchmark logs are clean.
  Invalid unbounded quality and missing inspection-resource CLI requests return failure and publish no output.
- Accepted verification commands and logs are under Vultra `build/.tmp/build-research-final.log`,
  `run-final-test-rendering.log`, `run-final-test-research-tools.log`, `tidy-binding-*` and `tidy-pose-final-*`.
  PVW's ignored `verify_workflow.py` independently checks exact replay images, matrix values, benchmark sample
  counts, untimed quality pairs and diagnostics. Generated API checks and all 71 changed project C++ formatting
  checks pass. All temporary files remain ignored; the original FBX/DDS assets remain unchanged.
- Source, shader, asset, configuration and native module identities enable auditing, but matching a configuration
  is not a guarantee across different drivers or arbitrary XR floating-point head/eye factorization. Physical
  headsets, Linux and the full example/test suite were not revalidated for this change. The previously deferred
  simulator pipeline-cache VUID remains outside this task. Earlier user work is intact; no commits or pushes.

## PVW lighting comparison and constant ambient — 2026-10-06

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
- PVW declares `[0.15,0.15,0.15]` in research.renderer, retaining IBL off, white sun intensity 2, original camera
  and exposure. The host's Environment panel exposes Constant ambient (linear). Regenerated property metadata/IR
  supports serialization and package defaults; codegen --check passes. Human docs explain the approximation.
- Ran the existing pixelwise Windows binary read-only against PVW's unchanged official FBX/DDS copy, writing all
  outputs/layouts under PVW build/.tmp/bistro-lighting-20261006/pixelwise. The left-eye camera was matched to Vultra's
  independently rendered left eye: (-1.9919588474,0.3047,0.8688324732), yaw -19.332, pitch 1.404, vertical FOV 39.6,
  1280x1280. Area lights, HBAO, SSR and FXAA were disabled, and ACES was explicitly enabled for the common display
  comparison. The first dump requested Original and fell back to the window; the accepted rerun selects Left and
  produces 1280x1280. The optional LTC LUT warning is immaterial because area lights are disabled.
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

## Official Bistro FBX materials and PVW default — 2026-10-06

- This supersedes the legacy-glTF/material limitations in the two snapshots below. PVW now selects the original
  ORCA BistroExterior.fbx and DDS textures, with per-asset `fbx_import` metadata (`orca`, `directx`). No source
  files were converted or rewritten. All 409 copied files match the originals by SHA-256; large assets remain ignored.
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
- Official FBX declares +Z front; Vultra imports to -Z front. PVW's model scale is now (0.8, 0.8, -0.8), explicitly
  compensating this axis conversion to preserve the paper's source coordinates, model translation and camera pose.
  Default IBL remains disabled, with the previously requested paper directional light. Exact paper shading parity
  is not claimed: Vultra uses its existing OpenPBR renderer.
- Windows release vultra-app and test targets build; focused clang-format/clang-tidy pass. test-asset-formats,
  test-asset-pipeline, test-scene and test-research-extension pass. ORCA fixtures cover cold/warm caches, separate
  conventions, missing properties, invalid manifest settings, mixed-case extensions and packaged metadata/DDS.
  Deferred GPU readback independently checks G/B roughness/metalness, DirectX BC5 normal direction/Z, unit emission
  and occlusion=1. Existing failure/recovery tests still run; intentional shader errors in their logs are expected.
- Eight-frame official-Bistro desktop runs capture the corrected motorcycle view and renderer defaults.
  A 427-entry VPK automatically includes all 405 DDS references; FBX/DDS archive payloads match the sources by SHA-256.
  The package runs from a fresh directory containing only host/VPK, without SDK or shader source files. All six
  1280x1280 HDR outputs are finite and byte-identical to source execution. Neither actual run logs runtime errors
  or Vulkan VUIDs; packaged execution does not compile shaders. Static FBX animation is still logged and ignored.
  QA is in PVW build/.tmp/bistro-official-fixed-20261006-2101 and Vultra build/.tmp/orca-*.log.
- Initial cold texture preparation/import measured about 1.53 s, and the later warm import about 1.07 s on this
  Windows machine. The cache is about 0.1 MiB and contains generated metadata/data only. These are local observations,
  not benchmark guarantees. Full-suite, new meshlet/ray-query runs and physical-headset validation were not performed
  for this material import change. Private originals and earlier work remain intact; no commits or pushes.

## Research renderer settings and PVW lighting — 2026-10-06

- The host now owns a Renderer Settings window on the left, Views in the center and a project-named
  extension panel on the right. Metrics/GPU Profiler remain lower-right tabs. Old layouts adopt this
  arrangement once; layouts with both new panels are retained. PVW no longer duplicates resolution
  controls or holds a separate resolution draft. Its input routing, comparison and algorithm controls remain project-owned.
- Version-1 research manifests accept a `renderer` object, stored by ProjectManifest without a server dependency
  and decoded by the host through the existing RenderSettings reflection. Unknown fields/types fail explicitly;
  `path` is reserved for `render_path`. Missing fields use library defaults. Zero light directions and unsupported
  mesh shading are rejected. Captures now include actual renderer settings/path. No API generation or ABI change.
- PVW defaults use IBL off, white directional intensity 2 and directionToLight (-0.09, 0.9, 0), opposite the
  original light's propagation direction (0.09, -0.9, 0). Skybox/HDR remains independent. The image is intentionally
  darker without environment illumination; material conversion and full paper-lighting parity are still not claimed.
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

## PVW Bistro Exterior startup view — 2026-10-06

- PVW now selects Bistro Exterior and a scene-authored motorcycle camera instead of Damaged Helmet.
  The pipeline-figure pose is position (-1.9812, 0.3047, 0.8995), pitch 1.404 degrees and vertical FOV
  39.6 degrees. The original yaw -19.332 degrees equals Vultra yaw 70.668 degrees; the serialized camera
  transform carries the matching orientation. Model translation is (7, -1, 0), uniform scale 0.8,
  clipping is 0.1/1000, and default desktop per-eye extent is 1280x1280.
- ResearchProjectApp initializes its world-Y-up FPS rig from the main scene's selected camera via
  SceneRenderState. It retains bounds framing when no camera is selected or --model overrides the scene.
  XR still composes runtime eye poses/projections with this rig; authored roll is not retained.
- The local project contains the original figure-era Exterior.gltf and its reachable dependencies,
  plus Citrus Orchard Pure Sky 4K HDR. Large assets are ignored; upstream Bistro LICENSE/README and
  public sky attribution remain in source control and the manifest. No private source paths were added.
  This glTF export lacks the official FBX/DDS material bindings. Lighting/algorithm defaults are unchanged;
  the startup pose does not establish full paper-figure or current official-Bistro material parity.
- Windows release host/module build, focused clang-tidy/format, test-scene and test-camera --offline pass.
  An eight-frame desktop run captures the motorcycle at the expected angle; exported matrices match the
  recipe and all six 1280x1280 HDR images are finite. All 409 copied Bistro files are byte-identical.
  Expected static-animation/BLEND-to-cutoff warnings remain; no runtime error or Vulkan VUID was logged.
  A separate two-frame --model run verifies bounds-camera selection. Full-suite, new Bistro VPK and
  physical-headset runs were not repeated for this change. Original research files remain untouched;
  no repository was committed or pushed.

## PVW editor extension and minimal debug tool — 2026-10-06

- The research host owns docking, previews, scene/camera controls, Metrics and GPU Profiler. PVW now owns
  Controls content through `register_editor` and its native `on_gui`, implemented in
  `source/editor/project_editor.cpp`. There is no engine/ImGui link or second ImGui context in PVW.
  `research_editor_api.h` supplies scoped host settings and parameter access. Its header is included in
  both build-time and executable SDK exports. The generated UI API now supplies editable scalar/integer,
  checkbox and newline-separated choice controls, headers with an initial state, wrapped text and tooltips.
  Exact ABI table sizes remain required; all versions remain 1. Rebuild old modules against this SDK.
- Default docking places Views left, Controls upper-right and Metrics/GPU Profiler in lower-right tabs.
  Old floating-only layouts adopt this layout; already docked layouts are preserved. Explicit restore
  rebuilds on the next frame. PVW resolution controls are initially collapsed; algorithm controls carry
  distinct widget labels. Metrics explain linear HDR/peak 1 and staleness; Profiler groups stage timings
  across eyes/levels and keeps individual events. Shared scene work is not an independent per-method total.
- PVW separates source viewpoint (center-to-stereo, left-to-right, independent eyes) from algorithm settings.
  Its forward type is now `pvw.forward_warp` (`ForwardWarpPass`), replacing the prototype `pvw.compute_warp`.
  Backend 0 is the original global atomic-min single-pixel splat; backend 1 is an indexed triangle-strip image
  grid with geometry-stage stretch rejection and hardware depth visibility. Raster payload conversion is
  included in the forward stage. Both consume/emit the common uint depth payload and reuse filling/gathering.
  This graphics adapter is not the original direct color-interpolating pipeline; do not claim full port parity.
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

- Source-host and exported-SDK-only PVW builds pass. `test-gui-settings`, `test-native-plugin`,
  `test-research-extension`, `test-graph-definition` and `test-scene-rendering` pass, including generated
  C#/native ABI rebuilds. The full 45-test suite and all examples were not rerun for this editor change.
  Generated API consistency, focused clang-tidy and formatting pass.
- The ignored real-module GPU harness retains the previous analytic tests and additionally checks graphics
  identity/sky coverage at grid steps 1/4/16 on a non-square extent, live step changes, finite plane depths,
  depth-fill bypass and exact inpainting bypass. A foreground/background fixture yields green-only depth-aware
  recovery versus mixed red/green ordinary pull-push. These are correctness checks, not paper benchmarks.
- An ignored wrapper module calls the real PVW editor plus the real host editor API. It checks expired frames,
  rejected fractional choices, resizing without parameter loss, independent same-route references, shared
  parameters across routes, reference restore and measurement. Injected source failure in the isolated fixture
  rejects reference publication; restoring the file recovers on the next candidate. User/private sources are untouched.
- Desktop source and source-free VPK graphics-grid captures at 193x145 match per-eye HDR/difference PFM and
  display PNG bytes. The VPK runs from a fresh directory without SDK/project sources and logs no shader compilation.
  All tested HDR captures are finite. Existing published/previous temporary deliverables were not replaced.
- Meta XR Simulator executes the scripted configuration/resolution changes, 16 application frames and
  15 stereo submissions; the half-scale capture is 720x792 per eye. Native-size submission/mirror succeeds.
  The previously deferred simulator pipeline-cache VUID remains; physical headset testing is unverified.
  Neither repository was committed or pushed. PVW still has no tests/, docs/ or AGENTS.md.

## PVW reconstruction Pass migration — 2026-10-06

- The adjacent PVW vproject now owns four Pass types: `pvw.compute_warp`, `pvw.depth_filling`,
  `pvw.backward_gather` and `pvw.pull_push`. Their C++ lives under `source/passes`; pure Slang lives under
  `assets/shaders/{lib,passes}`. Registration stays in `source/project.cpp`. No engine library is linked
  into the module; GLM 1.0.1 supplies CPU camera matrices. PVW still has no tests/, docs/ or AGENTS.md.
- `PVW Center to stereo` is the default method and reconstructs both eyes from the center import.
  `PVW Left to right` returns the rendered left eye and reconstructs the right. Both reuse the same four
  Passes. `Stereo reference` uses the host's independent eye rendering for comparison. Imported target
  textures supply extents without a GPU read, so rendering unused target views remains culled.
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
- Only the requested opaque depth/color reconstruction path was migrated. The original private checkout
  is unchanged. Other experiment variants, visibility-buffer dependencies, ray-tracing fill, AA, EWA,
  foveation and atomic profiling remain outside PVW. Publication header, figures, evaluation configurations,
  citation and downloadable release information remain explicit README TODOs for the maintainer.
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
- Three-frame desktop captures validate center-to-stereo at 193x145 and left-to-right at 97x81, including
  partial dispatch groups. The latter's left output matches independently rendered left pixels exactly.
  All captured HDR values are finite. These are correctness checks, not paper benchmarks or quality claims.
- New embedded executable and host-plus-VPK run in fresh directories with no project sources or SDK.
  Per-eye HDR, difference PFM and display PNG files match source captures byte-for-byte for both methods
  (host-plus-VPK checked for center-to-stereo). Logs show no runtime shader compilation.
  A second source-host process hits every project program cache, including separate refine graphics/compute entries.
- Meta XR Simulator runs both methods together: four application frames, three stereo submissions, mirror
  and raw/display captures, 157 timed Passes. The system-selected Varjo runtime reports no available headset;
  validation selected Meta only with a child-process `XR_RUNTIME_JSON`, leaving system settings unchanged.
  The deferred pipeline-cache VUID and simulator teardown-space warning remain visible. Physical XR is unverified.
- QA captures, logs, package and analytic harness stay in ignored `build/.tmp/pvw-migration-*` (PVW)
  and `build/.tmp/pvw_migration_qa.*` (engine). Existing release deliverables and personal layouts were preserved.
  No commits, pushes or publication were performed.

## Local research host development — 2026-10-06

- PVW accepts `--vultra_source=<checkout>` (default from `VULTRA_SOURCE_DIR`), overriding the exported SDK option.
  Before building its native module, it configures/builds the source host in a separate working directory with
  matching mode/platform/architecture. This avoids nested xmake configuration-lock contention. Native C API
  includes use the source checkout; VRI includes use its generated SDK. No engine submodule or static engine link.
- `xmake run pvw` queries the actual source executable path and passes absolute project/SDK paths; capture paths
  retain the project working directory. `xmake run -d pvw` forwards to xmake's configured debugger.
  Editor setup reads the selected xmake source/SDK configuration and preserves personal local settings.
- Host `--sdk-dir` selects explicit research Slang include roots, including cooking. Invalid roots are rejected.
  The bridge regression puts a failing include in a stale project SDK and a valid include in the selected SDK;
  actual compute/readback and failure/recovery pass with the selected roots.
- Verified Windows x64 release source build, three-frame PVW stereo/capture run, SDK-path failure diagnostic,
  targeted native research test, formatting and clang-tidy. Exported-SDK mode also builds and runs through PATH
  host discovery. Clear source mode with `--vultra_source=`; xmake's `auto` value is unsuitable for this path option.
  Capture baseline has zero MSE and SSIM 1 for both eyes.
  Full MTd/debug host builds and interactive debugger launch have not been exercised in this follow-up.

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
- PVW in the adjacent project directory is now a vproject with three native source files, pure Slang, two graph
  definitions, a scene/manifest and the unchanged public DamagedHelmet files/notices. Its engine submodule and
  standalone App/renderer sources were removed. tests/, docs/ and AGENTS.md remain absent as requested.
  Only stereo reference and comparison are implemented; maintainer-owned paper methods are not migrated.

Verification on Windows x64 release/Vulkan (RTX 4080 SUPER):

- Final all-target build and all 45 xmake tests pass, including the new native extension regression for metadata,
  duplicate/late registration, expired/foreign graph tokens, bounded compute readback, live parameters,
  compilation failure, pipeline-builder failure, recovery and completed-frame instance destruction.
- Modified C++ passes clang-tidy and clang-format; generated API consistency and whitespace checks pass.
  PVW compiles against exported headers alone; its refreshed compilation database contains no engine include paths.
- Forward/deferred baselines, nonzero swapped-eye differences and an active source-consuming method are verified.
  GPU FP32 differences equal CPU `abs(A - B)` pixel-for-pixel. Source activation/culling matches graph dependencies.
- Copied standalone host plus VPK, and an embedded project executable, run in an isolated directory without SDK
  or project source files. HDR PFM and display PNG pixels match the directory baseline; no runtime compilation occurs.
  QA outputs/logs are in ignored `build/.tmp/research-project-qa-20261006/`.
- Meta XR Simulator submits 7 stereo frames in an 8-frame run, with mirror and raw/display capture.
  The deferred pipeline-cache VUID and simulator teardown-space warning remain visible. Physical XR is unverified.

Limits are explicit in [research_projects.md](../research_projects.md): this mode imports a static scene,
rejects scene scripts, loads graph definitions at startup, and requires stopping the host before DLL replacement.
The old workbench supplies offline UI/scene/graph editing; its options are not silently applied to stereo projects.
The new host/module path has not been verified on Linux, SDL3 or a physical headset. PVW's current module path is
Windows-specific. No commits, pushes or publication were performed in this task.


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

### Embedding for PVW, 2026-10-06

The maintainer authorized committing/pushing the verified library changes before pinning a PVW submodule.
xmake now preserves a parent project's metadata/tooling, defaults embedded examples/tests off, exports the core
target without standalone applications/managed/game-UI targets and omits editor-canvas/RmlUi/Lua package requests.
Shader fingerprints read the library's source root while generated headers remain in the parent build directory.
Dependency versions/runtime/VRI patches are unchanged. Human instructions are in docs/guide.md.

A separate xmake parent in `build/.tmp/pvw-embedding-1791295344199/consumer` builds and executes a real VRI clear/readback.
Use `-P .` for this nested temporary fixture so xmake does not select the ancestor project. The core-only
subproject and standalone all-target build pass. PVW will pin the pushed commit; its research methods stay owned by
the maintainer, outside the library. No paper-specific algorithm or private assets are part of this library change.
