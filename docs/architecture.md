# Architecture

libvultra is a small C++23 graphics kernel. It provides the pieces a host program composes: a GPU device over
VRI, shader programs, a render graph, asset data, a reference render path, an ImGui layer and application loops.
It does not know about projects, packages, scripting or editors; those belong to the programs built on it.

Everything is one static library, `vultra`. Programs select the modules they need.

## Modules

Each directory under `source/` is one module and one build feature of the same name. Public headers are
`source/<module>/include/vultra/<module>/`, included as `<vultra/<module>/...>`.

```mermaid
flowchart BT
    core
    platform --> core
    gpu --> platform
    assets --> platform
    shader --> gpu
    shader --> assets
    graph --> gpu
    render --> graph
    render --> shader
    ui --> gpu
    app --> gpu
    xr --> gpu
```

| Module | Responsibility | Depends on |
| --- | --- | --- |
| `core` | Logging, command line, stable ids, math (projection, poses, rays), images, typed properties. | – |
| `platform` | Files and processes, the SDL3 window and input, `FileWatcher`, Android and Web platform pieces. | core |
| `gpu` | `Device`, `Frame`, textures and buffers, swapchain, readback, profiler, RenderDoc capture. | core, platform |
| `assets` | Scene, material and texture values, asset sources, VPK archives; optional importers. | core, platform |
| `shader` | Slang programs: compile, reflect, cook, load, hot reload; the builtin shader pack. | gpu, assets |
| `graph` | `RenderGraph`, `PassCatalog`, version-1 `.vgraph` definitions. | gpu |
| `render` | `GpuScene`, render stages and `RenderPath`, the stages and path of `vultra.forward`, environment, tone mapping, blits. | graph, shader, assets |
| `ui` | `EditorGui`: Dear ImGui with window, offscreen and inspector support. | gpu, platform |
| `app` | `BaseApp`, `RenderApp`, `DesktopApp`, `ImGuiApp`, Android and Web apps, camera controllers. | gpu, platform |
| `xr` | OpenXR session, eye swapchains, controller poses and input. | gpu |

`--libvultra_features=<a,b,...>` selects modules; dependencies are added automatically and `all` selects every
module the platform supports. Two options trim a module: `libvultra_shader_compiler=n` keeps only the cooked
shader runtime (no Slang, no file watching), and `libvultra_asset_import=n` keeps asset values and VPK without
importers. `xmake lua tests/build_modules.lua` builds every module on its own; the table above is what that check
enforces. Android and Web pieces are selected by platform inside `platform` and `app`.

## Errors

Vultra reports failures as values and does not throw. Every operation that can fail on initialization, external
input, file or GPU I/O, or misuse of an API returns `std::expected<T, vultra::Error>`, where `Error::message` names
the operation and its cause. Objects whose construction can fail, such as `Device`, `Frame`, `Window`, `Swapchain`
and `EditorGui`, are made by a static `create()` that returns `std::expected`; the ones other objects keep
references to are held by `std::unique_ptr`, while `Texture` and `Buffer` are movable values. A program checks each
result and logs or returns the error. A state no input can reach (an exhausted 64-bit id space, an unknown
enumerator) logs through `vultra::panic()` and aborts.

Libraries that report errors by throwing (argparse, nlohmann_json, `std::filesystem`, the model importers) are
wrapped where Vultra calls them, so their exceptions never cross a Vultra function. Allocation failure is not
handled.

## GPU and frames

VRI is the rendering abstraction. Code uses VRI descriptors and commands directly; there is no second RHI above
it. `Device` owns the VRI device, its interface tables, the graphics queue and, when the adapter has one, a
separate compute queue.
`Device::features()` reports the capabilities a technique may depend on, such as async compute, ray queries,
mesh shading and multiview; [device_features.md](device_features.md) records what VRI exposes on each backend.

One frame is in flight. `Frame::begin()` starts it and `Frame::submitAndWait()` is the lifetime boundary for
everything the frame used: destroying a CPU object does not prove the GPU finished with it. A frame is normally
one graphics command buffer. It can also be split into several submissions on the graphics and compute queues,
each signalling a point on its queue's timeline that a submission on the other queue waits for.

`RenderApp` keeps update, UI construction, command recording, submission and presentation as separate phases.
`ImGuiApp` builds UI in `onImGui()` before recording. Offline programs own a `Device` and a `Frame` directly and
need no window.

## Shaders

Shaders are plain Slang. `ShaderProgram` compiles a file for one target (SPIR-V or WGSL), with reflection for
bindings, parameter layouts, compute group sizes, **user attributes** and scalar field initializers. A program
cooks to a `.vshaderc` archive that loads without the compiler. `ShaderPipeline` builds a VRI pipeline from a
source file or a cooked program and reloads it when its sources change; a failed reload keeps the last pipeline
and reports diagnostics. Two compile options are chosen per compile, not by the C++ build mode, and are part of the
cache key: `debugInfo` embeds source text and line information for GPU debuggers, and `optimize` runs the
SPIRV-Tools performance passes over the generated SPIR-V. An optimized shader may compute floating-point results
that differ by about one ulp from the unoptimized one, because the driver may contract multiplies and adds
differently in the two; code that needs bit-exact results across both compiles without `optimize` (Slang does not
carry `precise` into SPIR-V today, so marking arithmetic `precise` does not help). The static Slang build carries no optimizer of its
own, so the compiler links SPIRV-Tools directly. The builtin shader pack is cooked with neither: optimizing it gave no measurable frame-time gain ([roadmap](roadmap.md)).
`ShaderCompileOptions::entries` may name an entry point that a module in `linkSources` or `linkModules` defines
instead of the file; hosts use this to add a vertex shader of their own to a file that has only fragment stages.

User attributes are how a host attaches meaning to shader declarations, for example to describe a render pass
inside its shader:

```slang
[__AttributeUsage(_AttributeTargets.Var)]
struct PassParamAttribute { float minimum; float maximum; string label; };

struct Parameters { [PassParam(0, 8, "Gain")] float gain = 1; };
ConstantBuffer<Parameters> parameters;
```

Slang rejects initializers on global uniforms, so defaults belong on fields of a parameter struct.

## Render graph

`RenderGraph` is code-driven: create or import resources, add passes with their resource uses, export what must
survive, compile, execute. Compilation validates usages, culls passes whose results nobody observes, optionally
reuses transient allocations, and builds an execution plan.

The plan groups live passes into **batches**, each recorded into one command buffer on one queue. A pass marked
`PassQueue::eAsyncCompute` runs on the compute queue; batches on different queues carry the waits their
dependencies require. `execute(Frame&)` submits every batch but the final graphics one, which stays open for the
caller. Graphics batches leave shared resources in a state the compute queue can accept, and resources used from
both queues are created for concurrent queue access. When the device has no compute queue, or async compute is
disabled at `compile()`, the same graph is a single graphics batch and `execute(cmd)` records it into the
caller's command buffer, with the same result.

`writeDot()` writes the compiled plan for Graphviz: batches per queue, culled passes, resources and waits.
`snapshot()` exposes the same information as data.

Textures may be 2D, 2D arrays, cubes (6 layers), cube arrays or 3D, each with a mip chain. A use names a range of
its texture: one mip, one layer (a cube face is a layer), or both; without one it covers the whole texture.
Dependencies, culling, history, async-compute handoffs and barriers work per mip and layer, so a pass may sample
one mip and write the next of the same texture; the uses of one pass must not overlap. A 3D texture has no layers,
and its depth slices share their mip's state. `Texture` keeps that state, and `Texture::view()` makes a view of a
range; a single layer is viewed as 2D, and a cube's faces can be viewed as a 2D array for storage access. History
is reset in every mip and layer (VRI clears mip 0, which the graph copies to the others). `readback()` reads one
mip of one layer or depth slice. `examples/basics/depth_pyramid.cpp` builds a pyramid of depth maxima this way.

Current limits: depth is D32; a use names one mip or all of them, not a span of several. The per-pass profiler
works with single-batch plans.

`PassCatalog` holds named pass definitions with typed ports and numeric parameters. A `.vgraph` file lists pass
instances and connections and builds through the same executor as C++ `addPass()`. Built with a view name, its
passes and the resources and history they create are named `<view>:<id>`, so one graph holds it once per view.

## Rendering

`GpuScene` uploads `SceneData` geometry, materials and textures. A **render path** turns a scene into named
graph resources. It is an ordered list of **slots**, each filled by a **stage**:

- `StageDesc` describes a stage without a device: its name, the slots it can fill (a stage with several fills
  them together), its scope, the resources it needs (required or optional) and the resources it provides.
  A **frame** stage runs once per graph before every view; a **view** stage runs once per view.
- `RenderStage` is the stage itself: `addPasses()` appends its passes for one view (or the frame) and returns
  exactly the resources it declares; `prepare()` updates per-frame data from the cameras and lights.
- `RenderPath` is a concrete class created from a `RenderPathDesc` (name and slots) and a list of `StageType`
  (description and factory). There is no global registry: the caller passes the stages it knows.
  `addPasses()` takes a list of views and returns each view's resources; with several views, view resources
  are named `<view>:<resource>` in the graph and frame resources exist once.
- A view stage can render **auxiliary views**, such as a shadow map's light view: `StageDesc::auxiliaryViews`
  names each one and the slots it runs, and `StageBuild::addView()` runs the stages of those slots for it at a size
  and with a camera the stage computes each frame from the view's unjittered camera and the lights. They are views
  of the graph after the path's own (`<view>.<name>:<resource>`, or `<name>:` with one view), with their own
  `camera.data`, no jitter and `StageFrame::auxiliary` set; `addPasses()` returns only the path's views. Their slots
  come before the stage, hold view stages and ask for no auxiliary views themselves.
- `validateRenderPath()` checks a path from the descriptions alone: every required resource is provided by an
  earlier stage, no resource is provided twice, a frame stage reads no view resource, and the stages together
  provide `scene.hdr` (RGBA16F, scene-linear) and `scene.depth` (D32). Hosts call it offline; `create()` calls it
  as well. Stage-specific resources use the stage's own namespace, such as `clustered.light_grid`.

Later passes depend only on resource names, so they work with any path that provides them.

Every path provides two resources itself, before its first stage (`pathInputs()`): `scene.lights` once per frame and
`camera.data` for each view. The path owns a `LightBuffer`, which validates a frame's `RenderLight` values and
uploads them as one storage buffer of at most 64 `GpuLight` records (`LightData` in
`resources/scene_lights.slangh`); `StageFrame::lightBuffer` gives stages its CPU side, as `vultra.forward_shading`
uses it. `camera.data` is a constant buffer of `CameraData` per view: matrices and their inverses, the previous
frame's view projection, position, image size and clip planes, the view's index, the count of the path's views
and the tangents of the four half angles of its unjittered projection, written by `prepare()`. Frusta may be
asymmetric, as a headset's eyes are: the path and its stages use the full matrices. A `RenderView` may be
derived: the path runs no view stage for it and gives it only the frame's resources and its `camera.data`; the
caller makes its `scene.hdr` from other views' resources before `addDisplay()`. The first view is never derived.

`vultra.forward` is the reference path, made of two stages: `vultra.mesh` (slot `geometry`, frame) imports the
scene's vertex, index and primitive transform buffers as `scene.vertices`, `scene.indices` and
`scene.instances`; `vultra.forward_shading` (slot `shading`, view) draws a skybox and one opaque OpenPBR forward
pass with analytic lights and image-based lighting. It has no shadows, transparency or mesh shading. A stage returns
what `StageBuild::requested` names and may leave out other resources it provides: forward shading writes the
surface layers (`scene.normal` to `scene.motion`) only when something reads one. Motion compares each primitive
with where the previous frame drew it, so a host calls `GpuScene::beginFrame()` once per frame. Stages that
rasterize the scene with its materials use `SceneMaterials` and the Slang module `vultra.material`.

Materials have **types**. `vultra.material` declares `IVultraMaterial`: `surface()` gives the surface a rasterized
point has (what the required G-buffer layers hold) and `shade()` the radiance it reflects from a light towards the
viewer. `vultra.openpbr` is the built-in type, the one of model files. `GpuScene` keeps a header per material,
`scene.materials` (type number and parameter offset), and the parameter blocks as words, `scene.material_data`;
`vultra.mesh` provides both. A draw pushes the primitive's transform index, material number, type and offset; set 1
holds the material's textures and `scene.material_data`. Every material starts as `vultra.openpbr` with its
`SurfaceMaterial`'s values, which `RenderPath::prepare()` writes again each frame so that edits to
`GpuScene::materials` show; `GpuScene::setMaterialType()` gives one another type and block. Shaders call the types
through the Slang module `vultra.materials`: the builtin one knows only `vultra.openpbr` and dispatches nothing; a
host that uses more types generates its own with the same three functions (`vultraMaterialSurface`,
`vultraMaterialStandard`, `vultraMaterialShade`) and gives `forwardShadingStage()` forward shading cooked with it.
Forward shading lights a type whose standard layers are not enough through its `shade()`. The `display`
slot comes last: `RenderPath::addPasses` stops before it and gives `scene.output` (`scene.hdr` without an upscale
stage), the caller appends its own passes, and `RenderPath::addDisplay` turns their image into `final`;
`vultra.tone_mapping` is the reference display stage. Every path also imports the environment's textures
(`scene.environment`, `_diffuse`, `_specular`, `_brdf`) once per frame, so passes after the geometry can light with
them. The builtin pack carries the sources of `vultra.material` with its cooked passes; `extractBuiltinPack()`
writes the pack once per content hash, and its `builtin/shaders` and `external` are the include roots of programs
that compile shaders importing it. Stage parameters (`StageDesc::parameters`) are changed with
`RenderPath::setParameter` and reach the stage in `StageFrame::parameters`. Those a stage reads while it adds its
passes, such as the size of an image, are listed in `StageDesc::buildParameters` and reach it in
`StageBuild::parameters`; after one of them changed, `RenderPath::needsBuild()` is true until the caller builds the
graph again. Deferred,
clustered or shadowed rendering are other stages and paths, written against the same public interfaces.
`ToneMappingPass`, `Environment` and `TextureBlit` are independent of the path.

## Assets

`SceneData`, materials and textures are CPU values that own no GPU resources. Importers read glTF/GLB, OBJ and
FBX and cache derived data keyed by source and dependency hashes. Geometry is stored in model space with the
file's node transforms applied; the glTF importer also keeps the node hierarchy in `SceneData::nodes`, each
node with its own transform and primitives. `AssetSource` reads from the filesystem or a
VPK archive, including one embedded in an executable or mapped from memory; persistent references are paths,
never GPU handles. Meshlet data is built with meshoptimizer. All file formats are version 1.

## UI

`EditorGui` wraps Dear ImGui for a window or an offscreen target, each instance with its own context, plus
left-label/right-control inspector rows over typed properties. An offscreen `EditorGui` renders to a texture and
accepts events through ImGui's input queue, which is what world-space panels and tests use. Layouts are isolated
by application name.

## Hot reload

`FileWatcher` (platform) watches directory trees. Operating-system callbacks only queue paths; `poll()` returns
debounced, de-duplicated changes on the caller's thread, so reload work runs between completed frames. Shader
reload uses it today; hosts use the same class for graphs, scripts and assets.

## What is not here

Projects, packages, scripting, the command line host and editors live in other repositories that build on this
library. The generated C ABI, the retained GUI plan, the `.vshader` language, the RmlUi layer and the monolithic
renderer were removed; [docs/ai/handoff.md](ai/handoff.md) records the last commits that contain them.
