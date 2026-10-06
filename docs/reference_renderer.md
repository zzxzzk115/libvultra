# Reference rendering and graph diagnostics

The progressive reference renderer uses VRI ray queries, the existing GPU scene and RenderGraph. It is available
in `vultra-batch`, the research workbench and the optional Python host. The focused ray examples remain separate.
It requests ray-query and bindless capabilities explicitly; unsupported devices fail rather than changing algorithms.

```sh
xmake build vultra-batch vultra-app vultra-research
env -u DISPLAY -u WAYLAND_DISPLAY ./build/linux/x86_64/release/vultra-batch \
    --project resources/research_lighting.vproject --path reference --seed 42 \
    --width 320 --height 240 --frames 64 --revision YOUR_BUILD_ID \
    --output build/.tmp/reference-run
```

`--path reference` also selects reference rendering in `vultra-app`; use `--offline --export DIRECTORY` to inspect
the same workbench UI without a window. A saved version-1 workspace includes the render path and required `seed`.
The legacy Research example's path selector still demonstrates the two raster paths. Workbench Skybox/IBL toggles
are raster-only; reference transport samples the environment directly and uses its authored intensity.

## Transport and material boundary

Each frame adds one sample per pixel. The integrator samples the current opaque OpenPBR BSDF, analytic lights,
emissive mesh triangles and the environment. Emissive/environment next-event estimation uses multiple importance
sampling with BSDF paths. Russian roulette terminates paths; there is no fixed bounce limit or radiance clamp.
Seeds select a deterministic per-pixel/sample sequence. Script RNGs remain the script author's responsibility.

The material domain is the existing **opaque OpenPBR subset**, including bound base/normal/emission and data textures;
transmission, subsurface, fuzz, thin-film and blended transparency are outside it. Alpha masking and double-sided
surfaces are supported. Material and environment lookup explicitly use bilinear filtering at mip level zero.
Jittered primary samples integrate that pointwise texture model; the reference does not infer raster derivatives,
ray cones or a hidden coarse LOD. Mip/cone filtering would define a different approximation and remains separate.
Environment next-event estimation uses a luminance-weighted alias table with exact latitude-row solid angles,
uniform longitude and cosine-of-latitude within each cell. A 5% uniform-sphere mixture supplies nonzero support
even where bilinear lookup spreads radiance into otherwise dark cells. The same solid-angle mixture PDF is used
for environment and BSDF MIS. The table is uploaded once per environment replacement, after prior GPU use completes.
The reference does not apply the
raster renderer's AO or split-sum IBL approximations. Geometric ray offsets and a scene-scaled minimum distance are
explicit numerical approximations; these tests do not establish a complete OpenPBR renderer.

Primary rays use a pinhole perspective camera, including asymmetric perspective projections. Orthographic and
other ray-origin models are rejected before command recording; a general pinned experiment matrix does not imply
that every renderer supports its projection family.

BLASes retain uploaded geometry. Instance-transform changes rebuild the TLAS at a completed submission boundary.
The GPU instance records follow the pinned VRI descriptor contract, and vertex loads use the CPU struct's byte
offsets explicitly. VRI resources are not interpreted as native Vulkan handles. The current Vulkan implementation
has image tests; D3D12 acceptance remains outstanding.

## Outputs and reset

All reference AOVs are ordinary marked graph outputs, automatically exported by batch and workbench:

| Port | Stored meaning |
| --- | --- |
| `scene.radiance` | RGBA32F accumulated mean, linear scene radiance |
| `scene.hdr` | RGBA16F bridge for existing HDR processing/tone mapping |
| `scene.albedo` | First-hit material base color; RGB, linear |
| `scene.normal` | First-hit signed world-space shading normal; XYZ |
| `scene.depth` | Positive camera view depth; zero for a miss |
| `scene.motion` | Previous minus current unjittered pixel position, top-left origin; XY |
| `scene.sample_count` | Completed samples since reset, replicated in RGB |
| `scene.ray_count` | Cumulative traced camera/continuation/shadow rays per pixel since reset |

Sample/ray counters use float channels; integer precision is finite, and accumulation rejects a sample count
beyond its exact float range instead of overflowing silently. Diagnostic surface AOVs describe the current sample, rather than an accumulated anti-aliased surface estimate.
Motion reprojects the current surface through the previous camera and rigid instance transform. Deformation/skinning
and the complete jitter/depth/exposure contract required by DLSS are not yet implemented. PFM exports store raw RGB,
including negative normal/motion channels. The workbench Outputs panel selects RGB, individual RGBA channels
or Rec.709 luminance and a finite increasing display range. Mapping writes a separate preview texture; it never
changes the experiment's raw attachments or timing events. A requested pixel probe reads original float RGBA at
top-left pixel coordinates after GPU completion. PNG exports still clamp raw values; use PFM/NumPy for quantitative
data, or Python `preview()` for an explicitly mapped display array.

Camera, transform, material, light, environment, seed and shader-generation changes invalidate accumulation.
Explicit `RenderGraph::resetHistory()` also resets it without recreating textures. A resized session/graph owns a
new history. Call `ReferencePathTracer::prepare()` between completed frames, then execute/submit/wait, then
`completeFrame()`. Scene, environment, tracer and pass objects must outlive graph callbacks and their last GPU use.

Independent validation includes normalized environment PDFs, constant/black-map solid-angle uniformity,
high-contrast alias sampling frequencies and a known spherical integral. GPU white-furnace results for diffuse,
dielectric, metal and coat materials are compared with deterministic hemisphere quadrature using upstream
OpenPBR's C++ evaluator, independent of the Slang integrator's sampler and MIS. A filtered high-contrast environment
uses independent CPU bilinear lookup and hemispherical integration. The existing directional C++/Slang BRDF tests
remain in `test-rendering`. These fixtures cover the declared opaque subset, not arbitrary material conformance.
Sampling follows [PBRT's infinite-area light model](https://pbr-book.org/4ed/Light_Sources/Infinite_Area_Lights);
the explicit pointwise texture boundary distinguishes it from
[footprint-based texture filtering](https://pbr-book.org/4ed/Textures_and_Materials/Texture_Sampling_and_Antialiasing).

`test-reference-path-tracer` also covers a textured emissive surface, alpha masking, mirrored sidedness, analytic
Lambertian lighting, camera motion, state/shader resets and Cornell Box convergence. The earlier Linux fixture's
MSE against an independent 2048-sample reference decreases from about 0.04338 at 16 samples to 0.002712 at 256.
Repeating a seed gives identical float readbacks on that device. Cross-device/driver bit identity is not promised.

## History, reuse and reports

Graph-owned texture history is declared with `createHistoryTexture()` and initialized by a VRI storage clear before
first use or after `resetHistory()`. History producers stay live across frames. The caller owns invalidation policy;
the reference renderer implements it for the changes above.

`compile(true)` enables exact-description transient reuse. Two resources may share one VRI allocation only if their
active use intervals do not overlap. Imported, exported and history resources never participate. This is reuse of
whole VRI resources, not overlapping heaps or a second allocator. The compiled aliased plan is immutable; edit and
compile a new candidate instead. Normal `compile()` retains the existing behavior. Alias-on/off readbacks and
history resets are regression-tested.

Batch/workbench exports include `graph_report.json` (`vultra.graph.report`, version `1`). It associates marked images
with graph resources and producer passes; resources include active use intervals, physical allocation IDs and
VRI-reported memory. `graph_owned_bytes` deduplicates graph-owned physical allocations. Imported scene resources,
BLAS/TLAS and private renderer textures are outside that subtotal. Unsupported memory/timestamp queries are `null`,
never estimated zero. CPU/GPU events include parent/depth, inclusive durations and barrier intervals; summing only
root events avoids counting nested work twice. `Profiler` supports up to 64 events, including nested scopes.

An offscreen RenderDoc capture can be recorded without a desktop window:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY renderdoccmd capture -w \
    ./build/linux/x86_64/release/vultra-batch --experiment resources/research.vexperiment \
    --renderdoc-frame 0 --revision YOUR_BUILD_ID --output build/.tmp/gpu-capture
```

Missing injection fails explicitly. The report records the actual saved `.rdc` path and identifies capture-affected
timings. GPU labels and debug Slang information also support Nsight Graphics inspection of the Vulkan path.
RenderDoc capture generation has been tested offline; automated replay and Nsight capture have not. Async compute
and NVIDIA integrations remain separate gates, requiring validated VRI synchronization/interop and suitable signals.
