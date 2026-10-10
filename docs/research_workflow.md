# Reproducible stereo research

These tools belong to the native research-project mode of `vultra-app`. Pure Slang and project-owned native Passes remain the rendering entry points. No SceneTree scripting or engine library is required in a project module. The host owns experiment state, rendering, inspection and measurement; the project owns algorithms, their parameters and the meaning of quality masks.

## Configuration and replay

The **Experiment** tab saves a version-1 `vultra.research_configuration` JSON document. It contains camera pose and clipping planes, per-eye render extents, optional headset geometry and camera track, renderer and display settings, both method names and every active Pass parameter, reference-snapshot status, ROI/masks and FLIP pixels-per-degree. Asset overrides and the engine shader-pack identity are also recorded.

```sh
vultra-app --project pvw.vproject --frames 8 --save-configuration experiment.json
vultra-app --project pvw.vproject --configuration experiment.json --frames 1 --quality --export replay
```

The output files/directories must be new. The UI can copy the replay command. A capture also includes `configuration.json`, so the displayed frame can be reproduced. Restoring a track pauses at its saved frame; `--camera-track track.json` starts playback. CLI configuration takes precedence over initial method/resolution/display defaults. Explicit model/environment overrides must agree with its recorded overrides. Source and packaged projects use the same configuration format.

The configuration retains the exact column-major `rigView` alongside the readable position/quaternion, and validates
that they describe the same rigid pose. Replay uses that matrix until free-camera input moves it. This avoids a
quaternion round trip changing floating-point view elements and quantized warping decisions. Edit camera poses
through the host or camera tracks; hand-edited configuration poses must also update their exact matrix.

Restore validates input and all method parameters, builds a candidate graph and publishes at a completed frame before GUI images borrow resources. Failure retains the current graph, camera and parameters. Configuration, profile and track saves validate first and use atomic file publication. Asset overrides require reopening: restore does not silently reimport geometry or replace environments. A changed engine-pack hash is reported; matching state alone does not imply matching shader code. Native DLLs and graph routing are project content rather than configuration overrides.

## Headset geometry

**Experiment / Headset projection** selects measured Valve Index, Pimax Crystal or Pimax 8K X Large-FOV geometry, or loads a captured profile. Profiles include separate eye extents, signed tangent bounds `(left, right, down, up)`, column-major rigid eye-to-head transforms, runtime identity and measurement provenance. IPD is a user/session setting, not a universal device constant. The supplied profiles are lab captures from September 2026; they are not manufacturer specifications or measurements of the reader's device.

```sh
vultra-app --project pvw.vproject --xr --frames 120 --capture-headset my-headset.json
vultra-app --project pvw.vproject --headset-profile my-headset.json
```

Capture needs located, renderable OpenXR views. The active runtime always supplies XR projection, pose and swapchain extents; desktop profiles never override it. Render scale changes internal resolution only. Saved configurations retain internal extents independently of the profile's native extents. Desktop resolution presets change size only; headset geometry is selected separately. With a profile, the source camera uses the rig midpoint and left-eye projection. Symmetric desktop mode retains its own FOV and IPD. All matrices use right-handed, Y-up, zero-to-one depth conventions.

An XR experiment configuration also records the located head-to-tracking-origin `trackingPose` as a column-major
rigid matrix. Desktop replay freezes that offset and composes it with the saved rig or track; live XR uses the
runtime's current tracking pose instead. Thus headset calibration alone does not lose the captured head's actual
position/orientation. Track FOV is used in symmetric desktop mode; a headset profile or live XR supplies the frusta.

The tracked midpoint is taken directly from located OpenXR poses, rather than recovered from world-view matrix
inverses. Captured and desktop eye transforms can still differ by floating-point roundoff when a rotated head pose
is factored into head/eye-relative transforms; desktop replay is not a bitwise guarantee for arbitrary XR sessions.

## Camera tracks

The **Camera track** section records keyframes at explicit unsigned frame indices, saves/loads a track, and exposes playback, rewind, pause and step. Position, vertical FOV and clipping planes interpolate linearly; orientation uses shortest-path quaternion SLERP. Evaluation depends on the frame index, not previous calls or wall-clock time. Frames outside the key range hold the endpoint. Tracks preserve roll; free FPS input returns to its usual world-up convention when the camera is moved. Uncheck **Use track** to position the free camera before recording another key.

A track has `format: "vultra.camera_track"`, `version: 1` and a `keys` array. Each key contains `frame`, `position: [x,y,z]`, `orientation: [x,y,z,w]`, `verticalFov` in radians, `near` and `far`. Indices must strictly increase and quaternions must be unit length. A benchmark samples a track explicitly, so GPU throughput cannot change the camera sequence.

## Independent benchmarking

Interactive profiler rows describe the composed comparison, with shared scene rendering. Independent end-to-end measurements use a separate graph containing one method and only its required source views. The host creates its desktop resources during initialization; the measurement loop does not build/draw UI, acquire/present swapchain images, render a reference/comparison/display pipeline, read images, export files or poll shader hot reload. Graph/pipeline construction and initialization precede sampling.

Native research mode defaults to Vulkan validation **off in release** and **on in debug**. Select it explicitly with
`--validation on|off`; startup logs, the GPU Profiler, captures and benchmark manifests record the actual choice.
Use `--validation on` for rendering correctness checks, and compare performance only at matching validation settings.
Earlier captures and benchmarks made before this option forced validation on, including release builds.

```sh
vultra-app --project pvw.vproject --benchmark benchmark.json
vultra-app --project pvw.vproject --validation on --benchmark correctness-benchmark.json
```

```json
{
  "format": "vultra.research_benchmark",
  "version": 1,
  "output": "results/benchmark-01",
  "warmup_frames": 60,
  "frames": 240,
  "quality_frames": 2,
  "start_frame": 0,
  "step_seconds": 0.011111111,
  "source_revision": "record-the-project-commit",
  "runs": [{
    "name": "PVW implementations",
    "method": "PVW Center to stereo",
    "parameters": {},
    "sweeps": [
      {"passes": ["warp_left", "warp_right"], "parameter": "backend", "values": [0, 1]},
      {"passes": ["inpaint_left", "inpaint_right"], "parameter": "mode", "values": [1, 2]}
    ]
  }]
}
```

Paths are relative to the plan. Each run may specify a `configuration` filename; otherwise it starts from the host's current configuration. The named method is selected in the current slot. Parameter overrides address unprefixed graph Pass IDs. Omitted values use the selected graph/catalog defaults. A sweep may use one `pass` or a synchronized `passes` group. Explicit sweeps form a Cartesian product, capped at 256 total runs; they are never inferred from keywords. A configuration file supplies camera/renderer/reference state, while the run supplies the selected method's parameter overrides.

Every configuration gets its own graph and warmup. Warmup holds the starting camera pose; measured frame `i` evaluates track frame `start_frame+i`. Native update callbacks use the fixed `step_seconds`; shader frame indices include warmup. The current native research methods are stateless across frames. Stateful extensions must define their reset/warmup protocol before claiming cross-run reproducibility: the host does not reset arbitrary DLL-private state. Geometry is imported/uploaded once. GPU times come from VRI timestamps; CPU preparation and submission/wait are reported separately. Samples are retained in memory and exported afterwards through `BenchmarkCapture`.

Each `run_N` contains `frames.csv`, `passes.csv`, `summary.csv`, `manifest.json`, the fully resolved configuration, plan,
asset dependency hashes, engine identity, native module hashes, active project SPIR-V hashes/compile keys/dependency hashes and a VRI memory
snapshot. Shader provenance changes only after a pipeline is successfully published. `quality_frames > 0` adds a
separate, untimed replay against the configured reference, with the same warmup and camera/shader-frame sequence,
`quality.csv`, final raw HDR and FLIP maps. Those frames never enter timing summaries. Do not equate these controlled
desktop replays with headset presentation latency or use short verification runs as publication results.

## Intermediate inspection

The **Inspection** tab lists active graph textures, including source/left/right HDR, depth and G-buffer outputs and project intermediates. Read a completed frame on request, select RGB/a scalar channel/luminance and an explicit range, then hover the snapshot to probe the original RGBA floats. Export writes raw RGB PFM, mapped PNG and resource/frame/mapping metadata to a new directory.

```sh
vultra-app --project pvw.vproject --frames 8 --inspect source.depth --inspection-export depth-snapshot
```

Inspection observes final resource contents and does not uncull unused rendering. Formats follow the existing readback contract: RGBA/BGRA8, RGBA16/32F and D32F. Integer payload buffers are inspected through project-authored visualization Passes. If a Pass later overwrites a texture, final contents cannot recover its previous value; use the existing `RenderGraph::captureAfterPass()` in the project to create an explicit intermediate snapshot. Readback, mapping and preview upload occur only on request after GPU completion.

The host can also insert that snapshot copy without changing a native project: specify the method's `A_`/`B_`-prefixed
`Pass.port` endpoint and an exact command-Pass name from the profiler, or use **Enable snapshot copy** in Inspection.
It exports `Inspection.snapshot`. This opt-in copy runs until disabled, and is excluded from independent benchmarking.
For PVW, capture the gathered color before pull-push updates it in place:

```sh
vultra-app --project pvw.vproject --frames 8 --inspect B_gather_left.hdr --inspect-after B_gather_left --inspection-export gathered-before-inpainting
```

## Quality and display

**Metrics / Measure quality** takes one completed-frame snapshot of both eyes. The main table shows PSNR, SSIM,
RMSE and LDR-FLIP for the current result against its reference. Measurement is manual unless **Live quality**
is enabled. ROI, selection textures and viewing density are under **Measurement settings**.

PSNR/RMSE use RGB from the tone-mapped **linear** display images clipped to `[0,1]`, with peak 1. SSIM uses Rec.709
luma and complete 11x11 Gaussian windows in that domain. LDR-FLIP uses those same images, before sRGB transfer.
Exposure and tone mapping affect these scores. These are display-referred linear colors, not unbounded raw HDR.
PSNR/SSIM increase with agreement; RMSE/FLIP decrease. Perfect matches report infinite PSNR; empty selections are N/A.

**Raw HDR (full frame)** retains the original RGB MSE/PSNR and Rec.709-luma SSIM on unbounded scene-linear values,
with peak 1; selected HDR reductions remain available there and in exports. Negative HDR PSNR is valid:
`PSNR = 10 log10(peak^2 / MSE)`, so MSE 126 at peak 1 gives approximately -21 dB. Bright specular outliers can dominate
HDR squared error. The host does not clamp raw HDR, choose a changing peak per image or silently normalize its values.
Display settings do not affect these raw metrics.

- A zero width and height ROI selects the full frame. SSIM requires complete 11x11 windows inside the ROI and includes selected window centers. The mask does not replace neighboring pixels in those windows.
- Optional masks name active graph textures; red `>= 0.5` selects a pixel. Mask extents must match the eye. The project defines what “valid”, “hole” or “disoccluded” means. Empty selections report unavailable values, never perfect agreement.
- FLIP uses tone-mapped **linear** RGB clipped to `[0,1]`, before sRGB encoding. The full frame supplies filter context; ROI/masks change reduction only. Record pixels-per-degree. Raw scalar maps are saved as PFM and grayscale PNG. This is LDR-FLIP, not HDR-FLIP's automatic multi-exposure search.
- Temporal residual is RGB mean `abs((current[t]-current[t-1])-(reference[t]-reference[t-1]))`. It requires consecutive sampled frames with unchanged rendering/method/assessment settings; masks use their temporal intersection. Camera motion is permitted. Gaps and configuration changes reset the pair. This measures temporal reconstruction residual, not motion-compensated perceptual video quality.

Use **Measure quality** for one snapshot, or enable **Live quality** and choose **Sample every N frames**.
The host records copies in the sampled render submission and consumes them after its normal GPU completion,
at the next update. A single background `vtask` job computes display/HDR metrics and CPU FLIP; another snapshot
is not queued while it is busy. CPU FLIP does not run on the interactive thread. Copies, decoding and publishing
the heatmap still have a cost, and live frame timings include that work. The result retains its sampled frame;
camera/settings/selection changes mark it stale. A failed assessment retains the previous complete snapshot.
An interval of 1 permits consecutive samples only when the previous assessment finishes in time; it does not
guarantee a consecutive-frame sequence. Deterministic temporal evaluation belongs in the separate quality replay.
**Views / Options / LDR-FLIP map** displays the last assessment with NVIDIA's Magma palette and a 0-to-1 legend.
Its PNG viewport export uses that palette; its PFM retains scalar error. `--quality` waits for the final finite
frame's assessment; `--export`
saves its FLIP maps and metadata. Plain exports omit FLIP unless that frame was assessed. Separate benchmark quality
replay produces consecutive pairs without contaminating measured frames.

Capture `metrics` retains whole-frame raw HDR values and its existing domain metadata. `displayMetrics` explicitly
records the bounded linear display domain, peak, selected pixel/window counts and scores. Benchmark quality CSV retains raw
HDR `whole_*`/`roi_*` columns and adds `display_mse`, `display_psnr`, `display_ssim` alongside `ldr_flip`. ROI/masks select
the same pixels for display, selected HDR and FLIP reductions.

Renderer Settings exposes ACES, None and Reinhard plus exposure. Desktop UNORM receives exactly one sRGB transfer; OpenXR float output stays linear. None preserves exposed linear HDR in float output and the desktop UNORM target clips its display range. HDR metrics/captures precede all display transforms. Display choices are serialized and reported, because they do affect LDR-FLIP even though they do not affect HDR metrics.

## Shared scene rendering

The current built-in raster path submits draws on the CPU. It computes indexed primitive bounds once, maintains
world bounds when transforms change, and prepares separate conservative camera and cascade visibility lists.
Offscreen geometry and shadow casters outside each light frustum are not submitted. Bounds handle reflected,
nonuniform and sheared affine transforms; touching boxes remain visible. Explicit game vertex programs retain their
draws because arbitrary shader displacement can exceed imported bounds. The ray-query reference path is independent.

The GPU Profiler and frame captures report visible geometry and per-cascade primitive counts for source/left/right
views. Its Shared scene row includes every active source/reference eye and their shadows; the default comparison
therefore renders three scene views. Independent benchmarking renders only the selected method's required views.
Captures also retain the most recent smoothed title FPS/CPU/GPU string (`titleStatistics`); it includes the interactive
loop and presentation, and is not an independent benchmark sample.

Native research projects enable **Renderer Settings / Shadows / Cache static maps** by default. Completed maps are
reused while their light-space matrix, primitive transforms, alpha material state and shadow programs are unchanged.
Camera or light movement refreshes the affected cascades. Filtering, light intensity and surface shading changes do
not invalidate depth contents. Cached cascades report zero submitted primitives; graph barriers and timing rows remain.
The benchmark records this setting: a stationary scene measures warm maps, while a camera track can require redraws.

Direct C++ `BuiltinRenderer` callers opt in with `settings.cacheShadows`, use `graph.compile(false)` and call
`completeFrame()` only after the recorded GPU submission completes. Call `invalidateShadowCache()` after in-place GPU
writes to geometry/alpha textures or replacement of shadow-map resources; transform and alpha material edits are
tracked automatically. The native host invalidates on graph replacement. Unpublished recordings never become cache
hits. Transient aliasing is rejected because another pass can overwrite cached maps. Arbitrary game shader materials
disable caching because their resource/displacement dependencies are not described by this static-scene contract.

Meshlet/task-shader culling is already available within submitted primitives. GPU-generated indirect draw lists,
material batching and occlusion culling are not implemented in this host. CPU visibility lists do not establish
GPU-driven rendering. These remain a separate renderer change with VRI capability/layout acceptance and image tests.

## Memory

Refresh **Inspection / Memory** to query VRI's device-memory budget/usage and tracked object allocations. The panel
separates driver-wide usage, actual VRI-owned bytes, scene geometry (including meshlets)/textures plus environment,
and deduplicated active RenderGraph allocations, and lists the largest objects. Internal renderer/GUI allocations
remain visible in the VRI total. Tracked totals include all memory locations; they are not exclusively device-local
VRAM. Wrapped or aliased objects own zero allocator bytes. Driver usage is not the sum of tracked objects; unsupported
queries are reported as unavailable. This is an on-request snapshot, not per-frame enumeration.
