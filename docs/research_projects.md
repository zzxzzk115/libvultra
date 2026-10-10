# Research projects in vultra-app

A research `.vproject` uses the standard project asset map, scene and native extensions.
The executable owns the window, VRI device, OpenXR session, RenderGraph, GUI, pipeline cache and captures.
Project modules borrow those objects through `VultraHostApi::research`; they do not link another engine library.

Use this mode for a stereo experiment with authored GPU Passes. The existing workbench remains available
for graph/scene inspection and saved `.vworkspace` documents. Pure Slang and direct C++/VRI experiments remain
independent of game `.vshader` materials. This is a small research host, not a claim of Falcor API compatibility.

## Authoring and delivery

Build the host from the engine repository:

~~~sh
xmake build vultra-app
~~~

From the project directory, export the SDK using that executable:

~~~sh
vultra-app --export-sdk sdk
~~~

The SDK contains the matching C ABI, VRI headers and ordinary Slang libraries/search roots.
Compile only the project's native shared library against `sdk/include`, using the engine's configured
MSVC runtime (release MT, debug MTd). No engine checkout, import library, Java or .NET SDK is required.
The SDK includes the Vultra and VRI MIT notices; the Slang OpenPBR library retains its upstream license.
Rebuild modules with the SDK exported by the host used for delivery. Pre-release ABI versions remain 1;
table sizes detect layout mismatches, not arbitrary binary compatibility between engine revisions.

### Local source development

PVW also accepts a local engine checkout through its xmake configuration. From the PVW directory:

~~~sh
xmake f -m debug --vultra_source=../libvultra-vri
xmake
xmake run pvw
xmake run pvw --xr
xmake run -d pvw
~~~

The source path is the engine repository root, not `source/` or an exported SDK directory.
Relative paths are resolved against the PVW root; absolute paths work as well.
`VULTRA_SOURCE_DIR` supplies the option's default when configuring the project.
The source option overrides `--vultra_sdk`, so an old local `sdk/` cannot supply the active headers or Slang libraries.
To return to the exported-SDK workflow, clear the source path: `xmake f --vultra_source= --vultra_sdk=sdk`.

The build configures the source engine for the project's mode, platform and architecture, builds `vultra-app`
and its generated `build/sdk`, then builds only the project module against the source C API headers and SDK VRI headers.
It does not include the engine's xmake project or link another engine library into the module.
The first debug build may need the engine's debug dependencies; Windows debug uses MTd and release uses MT.
The executable path is queried from xmake, respecting the engine's configured build directory.
The SDK export target currently places the SDK in `<engine>/build/sdk`.

`xmake run pvw` launches that source-built executable with an absolute project path and `--sdk-dir <engine>/build/sdk`.
Additional host arguments follow the target name. The working directory remains the PVW root, including relative
capture paths. `xmake run -d pvw` uses xmake's configured debugger; alternatively launch the source-built host in
Visual Studio with the same arguments, or attach to it. Keep its debug PDBs beside the host and project module
to step through both the native project Passes and engine code. Stop the host before rebuilding a loaded module.

After configuration, run PVW's `scripts/setup_vscode.ps1` or `.sh`. The compilation database uses the selected
source API headers, and Slang search paths point to the engine's `builtin/shaders` and `external` directories.
Personal editor settings remain local. Project `.slang` hot reload is unchanged; engine built-in shader/library
edits require rebuilding the source host and SDK.

You can also build the engine first and select its generated SDK explicitly:

~~~sh
cd ../libvultra-vri
xmake f -m debug
xmake build vultra-app
cd ../pvw
xmake f -m debug --vultra_source= --vultra_sdk=../libvultra-vri/build/sdk
xmake
../libvultra-vri/build/windows/x64/debug/vultra-app.exe --project pvw.vproject --sdk-dir ../libvultra-vri/build/sdk
~~~

`--sdk-dir` selects matching research Slang include roots for source projects and cooking (`--pack`).
An invalid SDK directory fails with its path in the diagnostic. Packaged projects still use their cooked shaders
and require no SDK at delivery; source mode is a local build setting, not part of the `.vproject` manifest.

~~~sh
vultra-app --project pvw.vproject
vultra-app --project pvw.vproject --xr
vultra-app --project pvw.vproject --method-a "Stereo reference" --method-b "My method"
vultra-app --project pvw.vproject --frames 8 --export captures/run-001
vultra-app --project pvw.vproject --pack PVW.vpk
vultra-app --project pvw.vproject --pack PVW.vpk --embed PVW.exe
vultra-app --project PVW.vpk
~~~

SDK, package, embedded executable and capture destinations must be new.
An embedded executable opens its own project when `--project` is omitted. A recipient needs that executable
or a matching host plus the project VPK, a Vulkan driver and, for VR, an OpenXR runtime. Native modules are
packaged and materialized by AssetSource, then unloaded before the extracted files are removed.
Project Slang sources are cooked to `.vshaderc`; packaged execution does not compile or watch them.

Native research mode defaults Vulkan validation to **on in debug** and **off in release**. Use `--validation on` for
diagnostic runs or `--validation off` for throughput measurements; captures and benchmark metadata record the actual
mode. Release measurements with validation enabled are not representative of normal release execution.

Model asset entries can carry `fbx_import` settings for the [ORCA FBX convention](asset_pipeline.md#fbx-and-dds-scope).
The host and packaged project use those settings; referenced FBX textures are collected into the VPK automatically.

`--model` and `--environment` preserve external asset overrides for local experiments; those files are not
automatically added to the package. Asset import/cache options are shared with the existing examples.
`--width`/`--height` override desktop eye extents. The editor can resize desktop rendering at runtime or apply
an XR render scale in [0.25, 1]. XR submission always uses the runtime's acquired image extent and field of view.
The selected `current_camera` in the main scene initializes the interactive research rig's world position,
yaw/pitch, vertical FOV and clipping planes. The rig uses world Y as up; authored camera roll is not retained.
In XR this is the tracking-origin transform; eye poses and projections still come from OpenXR.
With no selected camera, or with a `--model` override, the host frames the imported model's bounds instead.

`--layout-file` overrides the project-specific layout. Default layouts live beside the project under
`.vultra/<research.name>/imgui.ini`, outside source control.

Desktop preview and headset output default to the current result. The project Controls preview selector can show
the reference, both configurations or HDR differences; `--view a|b|compare|difference` selects the desktop preview.
Preview selection does not change the independent reference used for quality assessment.

## Manifest and graph contract

The optional `research` member of a version-1 project contains:

- `name`: nonempty application/layout name, without path separators or a drive prefix.
- `size`: desktop left/right eye extent, each dimension in [11, 8192].
- `render_path`: `forward` (default) or `deferred`.
- `renderer`: optional object of reflected `RenderSettings` defaults, such as
  `{"ibl":false,"directionToLight":[-0.09,0.9,0],"lightColor":[1,1,1],"lightIntensity":2}`.
  Property names/types are checked by the host; missing properties use library defaults. Select the path with
  `render_path`, not `renderer.path`. These defaults survive project packaging and apply to both compared configurations.
  `ambientColor` is an optional nonnegative linear RGB constant fill (default zero), independent of IBL and shadows.
  Raster shading adds `ambientColor * baseColor * materialAO`, including metals. Use it explicitly for reference
  protocols that contain this approximation; disabling IBL alone does not disable it. The host exposes it under
  Environment. It does not affect raw metrics through an exposure or display-curve adjustment.
- `features`: requested VRI feature bits, enabled when the device is created.
- `methods`: ordered objects with unique `name` and a graph `AssetId`.
- `comparison`: graph `AssetId` for the two raw HDR comparison outputs.
- `reference_method`: optional named independently rendered reference; the default current graph is another method.
- `configuration_label`: optional label for the generic current graph selector (default `Configuration`).

Declare graph and shader files in `assets`, and the native module paths in `extensions`.
Graph IDs survive cooking/path renames. Each method graph exports exactly two matching-extent RGBA16F or RGBA32F textures,
ordered left then right. Available imports are `source.hdr/depth`, `left.hdr/depth`, `right.hdr/depth`.
The source extent matches the left eye. Deferred graphs also receive, for each view:

~~~text
position_metallic, normal_roughness, albedo_weight, emission_occlusion,
specular, geometric_normal_ior, coat
~~~

These names follow the built-in GBuffer contract; inspect the existing shaders for channel meanings/formats.
Unused source rendering is culled by the existing RenderGraph. Identical method selections share one output and
parameter instance, except when a parameter reference is captured. That reference has independent Pass instances
even if it uses the same method. It freezes parameter values while both configurations use the live camera poses.
The view import namespaces source/left/right are reserved for method Pass IDs.
The host prefixes method Pass IDs with `A_`/`B_` before construction.

The comparison graph imports `a.left`, `a.right`, `b.left`, `b.right`, and exports two RGBA32F differences.
PVW implements absolute RGB differences as a real project-owned compute Pass.
FP32 storage preserves the subtraction result instead of introducing another FP16 image-store conversion.
Image writes encode to the destination format as specified by [Vulkan image writes](https://docs.vulkan.org/spec/latest/chapters/images.html#_image_writes).
Exposure/tone mapping and difference preview gain are downstream of the raw HDR outputs.

## Native Pass lifetime

`research_api.h` is a small bridge to the existing PassCatalog and RenderGraph. It is not another RHI.

1. During `vultra_plugin_init`, register named Pass definitions with typed ports, extent constraints,
   numeric parameter bounds, feature requirements and an instance creation callback. Metadata is copied.
2. Each instance creates its VRI layout/descriptors and a host-owned ShaderPipeline once. The builder
   receives Slang's VRI shader descriptors and constructs the VRI pipeline directly; use the borrowed
   device-owned pipeline cache. Project-relative shader paths must be declared assets.
3. Build declares texture/buffer resources and Pass uses in a fresh candidate graph. The host checks ports,
   stages/resource states, topology and outputs using its existing graph rules. Never retain a graph-frame token.
4. Execute receives a command buffer and a fresh token. Resolve borrowed VRI resources, read persistent
   numeric parameter arrays, and request source/left/right camera matrices through `frame`. Camera snapshots
   are execution-time data; they cannot be frozen during build.
5. GPU completion precedes parameter edits, shader publication, graph replacement and Pass destruction.
   Graph callbacks disappear before instance stop callbacks; instances disappear before module unload.

Input/parameter spans survive with the owning graph instance. Matrix arrays are column-major,
right-handed/Y-up, with zero-to-one projection depth. Desktop uses a synthetic IPD rig; XR composes the
tracked eye poses with the rig. The source camera uses the tracked midpoint and left-eye projection.
Module callbacks are trusted native code on the owning main thread. Catch exceptions inside modules;
do not throw across the C ABI, retain borrowed GUI/graph frames, destroy host resources or release XR images.

Source ShaderPipeline reloads retain their last successful pipeline on compilation/builder failure.
Source projects keep their development shader cache under `<project>/.vultra/shaders`, independent of the
host's temporary engine-pack directory. Matching SDK search paths remain stable across processes; unchanged
programs can be restored without running the Slang frontend. Cooked project shaders create no compiler or watcher.
The GUI displays diagnostics, and recovery updates the pipeline between completed frames. No per-frame
compilation, pipeline creation or geometry upload is introduced by the host. DLL replacement itself
requires stopping the application and rebuilding the module. Graph definitions are loaded at startup.

Native `on_gui` callbacks run during GUI construction, after updates and before command recording.
The viewport fills the left/main area. Preview images retain their source aspect ratio and are centered
horizontally and vertically below their labels, including each cell in reference/current comparison mode.
Each label reports that image's actual render texture width and height, including runtime-scaled OpenXR eyes.
Preview scaling and dock resizing do not change this render resolution.
A right sidebar occupies 26% of the default width: project Controls,
Experiment, Metrics and GPU Profiler share its upper tabs; Renderer Settings and Inspection share its lower tabs.
The default selects project Controls above and Renderer Settings below. Saved left-sidebar layouts adopt this
arrangement at startup; right-side and floating custom layouts are retained. **Restore default docking** applies
on the next GUI frame. Layouts remain project-specific and ignored.

The Views toolbar selects **All**, **Left** or **Right** without changing the rendered or measured eyes.
**Ctrl+hover** shows a 15x15-texel magnifier at 12x with pixel coordinates and the selected texel highlighted.
It samples texel centers through the existing GUI texture; it performs no CPU readback.
**Options / Content** chooses the result, a completed intermediate snapshot or the last LDR-FLIP heatmap.
Snapshots identify their frame and retain their original extent. Project extensions supply their friendly names.

**Save visible images** exports the displayed selection; **Save all images** exports both reference/current
stereo pairs for result mode, both error/FLIP eyes for those modes, or the selected intermediate snapshot.
PNG preserves original texture resolution, independently of dock fitting. PFM preserves raw RGB/HDR or FLIP
scalar error; an intermediate's selected scalar/alpha channel also has `intermediate_channel.pfm` so it is not
lost in RGB PFM. `images.json` records sizes, domains, resource/mapping and snapshot identity. Relative output
directories resolve against the project folder. Existing directories are refused; use a new name for each capture.

**Renderer Settings / Match viewport** is an optional desktop mode: the per-eye image area must remain stable
for three GUI frames before its extent is applied at a completed-frame boundary. Selecting a preset, applying
a custom size or restoring a configuration disables matching. Captures record the resulting dimensions;
independent benchmarks and OpenXR retain their explicit/runtime dimensions.

GPU Profiler reports the current input route, renderer and shared parameter choices. **Desktop V-Sync** requests
FIFO or Immediate by rebuilding the desktop swapchain between completed frames. VRI can substitute an available
mode and does not expose its resolved mode; the UI labels this as requested. **Presentation timing** separates
acquire, fence wait and present wall time. Fence wait overlaps GPU execution and must not be added to GPU work.

Renderer Settings exposes the host's shared rendering path, directional light, environment/IBL, shadow controls,
material/debug overrides, camera and display settings. Mesh controls are disabled without imported meshlets.
Rendering edits publish at the next completed-frame boundary; path and shadow-map-size changes rebuild the graph.
A rejected rebuild restores the previous rendering settings as well as the active graph. Captures record the actual
`rendererSettings` and path, rather than only the manifest's initial values.

Desktop resolution presets use per-eye extents 1024x1024, 1280x1280, 1440x1600, Valve Index 2016x2240,
Pimax Crystal 3234x3826 and Pimax 8K X Large FOV 6254x2962. Selection applies on the next frame;
custom dimensions use Apply custom resolution. These are study render sizes, not display-panel specifications or
headset emulation: selecting one preserves camera FOV and IPD. With OpenXR, the panel exposes render scale instead;
the runtime owns native extents and projections.

The Experiment tab separately selects complete measured/captured headset geometry, including asymmetric frusta
and eye-to-head poses, and saves configurations and deterministic camera tracks. See the
[research workflow](research_workflow.md) for replay, independent benchmarking, inspection and extended quality assessment.

## Project editor extensions

An extension may register one project owner for the right-hand project panel through `register_editor` during initialization.
Its `on_gui` callback draws project-specific controls through the generated `VultraUiApi`: sections with an initial
open state, wrapped descriptions, tooltips, checkboxes, integer/double sliders and newline-separated dropdown choices.
The host owns Renderer Settings, previews, Metrics and GPU Profiler. It does not identify project
algorithms by name. Without an editor registration, it builds numeric parameter controls from Pass metadata.
Labels, descriptions, choices and shared-instance flags in that metadata are copied into the existing PassCatalog.
Discrete values and checkbox values are validated, including defaults. Unknown choices and nonfinite values fail.

`research->editor` becomes available after host initialization; check its version and exact table size. Calls require
the current `on_gui` frame from the owning host. Expired or foreign frame tokens are rejected. `get_view` reports
requested/active methods, requested and actual resolution, display/headset selection and reference status. While XR
is waiting for its first renderable frame, `active_method` is `UINT32_MAX` and active dimensions are zero.
`set_view` stages a method or resolution change for the next completed-frame boundary. `method_name` returns borrowed
manifest text valid for the host lifetime. `get_parameter` reads slot 0 (reference) or 1 (current); `set_parameter`
updates matching Pass instances in the current configuration, preserving their other values. Native modules use
these host widgets and tables without linking an engine library or a second ImGui context.
Graph-routing parameters marked read-only cannot be edited through this UI table.

`capture_reference` snapshots active current parameters; `use_rendered_reference` restores the manifest reference.
Candidate graph construction is transactional: failure keeps the active graph and restores its reference state,
method and extent. Resize preserves active parameter values. Shared parameters of matching Pass types carry between
input routes. Captures are session state and do not modify the manifest or graph files. `get_view` / `set_view`
also expose preview eye/content selection, desktop matching, requested V-Sync and live-quality interval controls.
`measure` queues one completed-frame assessment and background CPU FLIP. Camera/settings changes mark its snapshot
stale. The default Metrics table uses bounded tone-mapped linear RGB; raw scene-linear HDR reductions remain separate.
`preview(label, resource, after_pass, channel, minimum, maximum)` stages a named intermediate snapshot; an empty
writer name selects the resource's last writer. Explicit names must identify a unique writing command Pass.
Pass outputs and named internal pyramid textures are supported; missing names retain the active graph and diagnose
the failure. Snapshot readback/mapping runs once after completion. `save_images` queues the same non-overwriting
viewport export as Options. These calls require the owning live GUI frame and the matching version-1 SDK.
Profiler stage rows aggregate both eyes and all levels; raw events remain inspectable. Scene rendering is shared
within the composed graph, so those rows are not independent end-to-end timings for each method.
The stereo host imports a static mesh/material scene and optional environment; it does not run scene scripts
or expose mutable scene nodes to research extensions. Projects with scene scripting use `vultra-runtime`.
The existing workbench's `--offline`, `--graph`, `--path` and `--seed` options belong to that workbench mode.

## Capture and verification

The [research workflow](research_workflow.md) documents configuration save/restore, headset-profile capture/replay,
deterministic camera tracks, independent benchmark plans and sweeps, intermediate texture inspection, ROI/mask and
LDR-FLIP/temporal assessment, display transforms and VRI memory telemetry. These are host facilities shared by native
research projects; algorithms and mask semantics remain project-owned.

Finite `--export` runs save per-eye A/B HDR PFM, encoded display PNG, raw difference PFM, difference preview PNG,
the final desktop mirror and `frame.json` with cameras, method names, per-Pass parameter values, resolution,
reference-snapshot status, asset hashes, engine pack identity,
render path, source-culling status and completed Pass timings. PSNR/SSIM compare linear HDR RGB with peak 1.
A shared A/B baseline is expected to have zero MSE and infinite PSNR, stored as JSON null plus `perfectMatch`.
Timings cover the composed graph; image readback/export occurs only on request after GPU completion.
The host configures `Profiler` for 512 events per frame so two stereo pyramid configurations fit without dropping timings.
Other callers retain its default capacity of 64 and can choose a capacity at construction.

PVW provides four native Pass types for forward warping (compute atomic-min or indexed graphics image grid),
scanline depth filling, backward gathering and pull-push with optional depth filtering.
Its center-to-stereo and left-to-right graphs reuse those types with different view
imports. The target-image ports provide extent metadata without consuming target pixels; the graph still culls
unneeded view rendering. Project code reads camera matrices during execution and owns its algorithm buffers,
descriptors and pyramid levels. Those implementations remain in the project, outside the host renderer.

The engine's `test-research-extension` exercises actual GPU compute/readback, parameter updates, expired/foreign
frames, metadata persistence, registration checks and shader compilation/pipeline failure recovery.
Windows x64 Vulkan desktop, project VPK and embedded executable runs are verified on the development device.
Meta XR Simulator verifies submission and mirror/capture lifetime; a physical headset is not verified.
The previously deferred simulator pipeline-cache validation diagnostic remains visible.
