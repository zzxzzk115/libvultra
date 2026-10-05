# Early Engine Architecture

Vultra has two entry paths. A C++ research program links `vultra` and uses VRI, shaders, RenderGraph, asset import or the built-in renderer directly. A packaged project uses `vultra-runtime` with an external VPK or appends that VPK to a copy of the executable. The scene tree, script host and separate editor do not sit between direct C++ code and VRI.

## Modules and identity

| Module | Responsibility |
| --- | --- |
| `core`, `platform` | Values, diagnostics, files, windows and input. |
| `drivers` | VRI device/resources/swapchain, profiling and OpenXR GPU integration. |
| `assets` | CPU `SceneData`, import/cache, project manifests and stable asset IDs. |
| `servers` | RenderGraph, GPU scene ownership and `RenderingServer` RIDs. |
| `scene` | Optional Node/Resource tree, runtime `ObjectId` and persistent node IDs. |
| `ui`, `main` | EditorGui/VGui and the application frame lifecycle. |
| `api` | Generated C UI/scene/experiment tables, language layouts and Inspector descriptions. |
| `scripting` | Optional native, Lua and C# host, linked as `vultra-scripting`. |

A RenderGraph resource is valid only inside its graph. A server RID identifies a live GPU resource in one rendering context; it is never serialized. `ObjectId` identifies a live scene object, while VPKs persist separate asset and node IDs. `RuntimeContext` owns Window, Device, Swapchain/Frame and RenderingServer in dependency order. The packaged renderer bakes mesh geometry, updates GPU instance transforms after script callbacks, and rebuilds geometry after mesh membership or model changes at a completed-frame boundary. Scripted group/mesh creation and reparenting use the same scene tree; reparenting preserves local transforms. Camera/light nodes remain CPU-owned by the tree. A reused `SceneRenderState` resolves their global transforms and selected camera into frame data consumed by both renderer paths, without rebuilding geometry or graphs. Scene-owned material resources persist by stable asset ID; mesh overrides resolve into per-instance numeric slots while retaining imported texture bindings. Their changes synchronize existing constants after GPU completion without reuploading geometry. Removing a resource clears references and invalidates its runtime handle. An explicitly selected non-spatial EnvironmentNode owns CPU HDR asset/intensity settings. Source changes replace preprocessed lighting at a completed-frame boundary while retaining the renderer and graph; scalar intensity changes reuse textures. Camera/light/material/environment value layouts and C wrappers are generated from the same annotated declarations. Direct C++ renderers can bypass the scene tree or pass an explicit light span to the renderer.

SceneTree owns per-domain `SceneChanges` revisions; attached nodes/resources borrow stable notification storage that survives tree moves. Detached subtrees stop notifying the former tree. Consumers keep independent revision snapshots, so synchronization never drains events. `SceneGpuSync` applies transforms/materials to one upload, matching mesh ranges by persistent node ID; `SceneRenderState` refreshes camera/light data only for relevant changes or a new extent. Structure revisions also invalidate dependent selection/state. Failed synchronization does not advance its cursor. Runtime, Research and the workbench share these helpers at completed-frame boundaries. Geometry membership/model changes require reimport; plain reparenting does not. Revisions are process-local and stay out of version-1 scene documents.

## Extension and script boundary

The editor's SceneInspector retains only object IDs and resolves tree/resource ownership each frame. Its containers, transforms and asset references are explicit C++; numeric camera/light/environment/material rows are generated alongside RenderSettings from the same annotated declarations and libclang IR. They edit value snapshots and commit through scene setters. Context-owned property drawers can customize these rows without a global registry. Pending HDR selection is scoped to a scene root's runtime ID and applied at frame completion; scene replacement cancels old operations. Transform deltas retain shear/signed scale. The Inspector remains editor code and does not add a scene dependency to the GUI layer or a second binding pipeline.

The research frontend uses the context-owned `PassCatalog` and version-1 `GraphDefinition` to compile project passes into the existing RenderGraph. The Research compute example verifies the same pass through direct C++ and JSON connections. `vultra-app` owns the editing draft separately from the active scene and graph: it compiles a candidate before retiring GUI texture descriptors and replacing the graph at a completed-frame boundary. Failed edits keep the active graph and renderer settings. Pass and material parameter changes reuse GPU state; project or changed scene snapshots stage a new scene. Saved workspaces embed current SceneTree state while project assets remain external, so edited materials survive reopening without modifying source project scenes. The same panel and VRI GUI renderer support an offline UI capture without a window. The editor target remains separate from the library/player. Broader built-in pass contracts, Python and the reference path tracer follow the [research milestones](research_milestones.md).

The binding direction is annotated C++ API → deterministic IR → versioned C ABI → language-specific author API. The C boundary passes fixed-width values, borrowed callback-scoped scene/UI frames and status codes; it does not pass C++ or ImGui objects. Native C plugins can use that boundary directly. Ordinary C++ applications do not need the script host.

A `VultraExtension` is a project-declared native library with load, update, GUI and stop callbacks. It is independent of scene nodes. A native C++ script is a class instance attached to a persistent node ID; an extension can register several such classes and the host loads their library once. This follows the separation between Godot's [GDExtension C boundary](https://docs.godotengine.org/en/4.4/tutorials/scripting/gdextension/gdextension_cpp_example.html) and [node scripting model](https://docs.godotengine.org/en/stable/getting_started/step_by_step/scripting_first_script.html). Lua modules also attach to nodes, using a script table with `_ready`, `_process`, `_editor_gui` and `_exit_tree` methods; Lua remains Lua, not a GDScript interpreter.

C# scripts reference the safe `Vultra.Scripting` assembly, derive from `Node` or `Node3D`, and override `_Ready()`, `_Process(double)`, `_EditorGui` or `_ExitTree`. They can use inherited `Position` without `unsafe`. `Vultra.ManagedHost` handles the native entry and collectible loading; `VultraBindings.g.cs` contains internal ABI layouts. This borrows Godot's [node-subclass syntax](https://docs.godotengine.org/en/4.4/tutorials/scripting/c_sharp/c_sharp_basics.html) and its separation of [public API from native interop](https://github.com/godotengine/godot/blob/master/modules/mono/glue/GodotSharp/GodotSharp/GodotSharp.csproj), but it is an early slice: public node bindings are still handwritten, and managed node wrappers do not yet share object identity across scripts.

Ready/start runs before the first update; scene reads and writes are valid only during ready/process. The separate GUI callback can show state and queue changes for the next update. Exit/stop has no scene frame. There is no fixed-step callback, `[Export]` persistence, generated signals or cross-language event bus. C# manifests currently name the fully qualified class. A complete binding pipeline needs two distinct generators: libclang IR for public engine API members, and a Roslyn source generator on user `partial` scripts for script paths and property/signal metadata. Godot's [script-path generator](https://github.com/godotengine/godot/blob/master/modules/mono/editor/Godot.NET.Sdk/Godot.SourceGenerators/ScriptPathAttributeGenerator.cs) illustrates the second role.

`ScriptHost(scene, true)` checks module files between callbacks. It stages replacement extensions and their dependent C++ script instances before stopping the old callbacks, then unloads the old library. A bad replacement leaves the old code active. Lua VMs and C# collectible contexts are replaced similarly. Lua and native script-local state resets on reload; the C# host restores supported user fields. Scene data remains owned by the host. Future callbacks that retain GPU resources will also need an explicit GPU retirement point.

## Package contract and open boundaries

The runtime statically links its engine libraries and the optional Lua host. A project VPK carries its native modules, C# assemblies and assets. The player currently extracts project and built-in VPK contents for file-based loading; native libraries require a real file path. C# projects also need an installed .NET 10 runtime. On Linux, system Vulkan, display-stack and libc libraries remain required, so “single executable” does not mean a fully static ELF.

Before the first public release, every serialized-data and file-structure version stays at `1`; format edits are breaking changes across writer, reader, examples and tests. The C ABI version also remains `1`; rebuild native and managed modules after breaking API changes. SDK bootstrap checks require exact table sizes. No compatibility adapters are provided. Stream-backed asset/UI reads and broader resource/server/editor bindings remain open. The current `vultra-app` is a static-scene research workbench, not a complete engine editor. The optional Python adapter exposes experiment sessions and owned NumPy images; batch scene/action calls for AI simulation remain future work. [Infernux](https://infernux-engine.com/) is a reference for that use case.

## Offline research ownership

`ExperimentSession` belongs to the public static core and borrows a Device; it owns its scene/server, GPU upload,
renderer, pass instances, graph, frame and profiler. Rendering is synchronous with one completed frame before the
next update. The optional `ExperimentHost` adds scripts, context-checked session IDs and the generated experiment
ABI. `vultra-research` owns a native Device and exposes that table to the safe Python package; Python remains outside
the core/player. Both the batch tool and Python consume the native version-1 `ExperimentDescription` loader.

`ReferencePathTracer` composes ordinary RenderGraph passes and explicit history with the existing renderer's shared
ToneMappingPass. It borrows the same GpuScene and Environment, retaining BLAS geometry while updating the TLAS after
transforms. The graph must be discarded before pass/tracer owners. Camera/material/light/transform/environment/seed
and shader changes reset accumulation. Exact-description transient reuse is opt-in; it never aliases imports,
exports or history. See [reference rendering and diagnostics](reference_renderer.md) for the material/AOV boundary.
