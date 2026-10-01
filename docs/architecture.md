# Early Engine Architecture

Vultra has two entry paths. A C++ research program links `vultra` and uses VRI, shaders, RenderGraph, asset import or the built-in renderer directly. A packaged project uses `vultra-runtime` with an external VPK or appends that VPK to a copy of the executable. The scene tree, script host and future editor do not sit between direct C++ code and VRI.

## Modules and identity

| Module | Responsibility |
| --- | --- |
| `core`, `platform` | Values, diagnostics, files, windows and input. |
| `drivers` | VRI device/resources/swapchain, profiling and OpenXR GPU integration. |
| `assets` | CPU `SceneData`, import/cache, project manifests and stable asset IDs. |
| `servers` | RenderGraph, GPU scene ownership and `RenderingServer` RIDs. |
| `scene` | Optional Node/Resource tree, runtime `ObjectId` and persistent node IDs. |
| `ui`, `main` | EditorGui/VGui and the application frame lifecycle. |
| `api` | Generated C UI/scene tables and Inspector descriptions. |
| `scripting` | Optional native, Lua and C# host, linked as `vultra-scripting`. |

A RenderGraph resource is valid only inside its graph. A server RID identifies a live GPU resource in one rendering context; it is never serialized. `ObjectId` identifies a live scene object, while VPKs persist separate asset and node IDs. `RuntimeContext` owns Window, Device, Swapchain/Frame and RenderingServer in dependency order. The packaged renderer currently bakes mesh transforms at startup; it does not propagate later node changes to GPU geometry. `example-scripting` instead reads node translations each frame for its small direct renderer.

## Extension and script boundary

The binding direction is annotated C++ API → deterministic IR → versioned C ABI → language-specific author API. The C boundary passes fixed-width values, borrowed callback-scoped scene/UI frames and status codes; it does not pass C++ or ImGui objects. Native C plugins can use that boundary directly. Ordinary C++ applications do not need the script host.

A `VultraExtension` is a project-declared native library with load, update, GUI and stop callbacks. It is independent of scene nodes. A native C++ script is a class instance attached to a persistent node ID; an extension can register several such classes and the host loads their library once. This follows the separation between Godot's [GDExtension C boundary](https://docs.godotengine.org/en/4.4/tutorials/scripting/gdextension/gdextension_cpp_example.html) and [node scripting model](https://docs.godotengine.org/en/stable/getting_started/step_by_step/scripting_first_script.html). Lua modules also attach to nodes, using a script table with `_ready`, `_process`, `_editor_gui` and `_exit_tree` methods; Lua remains Lua, not a GDScript interpreter.

C# scripts reference the safe `Vultra.Scripting` assembly, derive from `Node` or `Node3D`, and override `_Ready()`, `_Process(double)`, `_EditorGui` or `_ExitTree`. They can use inherited `Position` without `unsafe`. `Vultra.ManagedHost` handles the native entry and collectible loading; `VultraBindings.g.cs` contains internal ABI layouts. This borrows Godot's [node-subclass syntax](https://docs.godotengine.org/en/4.4/tutorials/scripting/c_sharp/c_sharp_basics.html) and its separation of [public API from native interop](https://github.com/godotengine/godot/blob/master/modules/mono/glue/GodotSharp/GodotSharp/GodotSharp.csproj), but it is an early slice: public node bindings are still handwritten, and managed node wrappers do not yet share object identity across scripts.

Ready/start runs before the first update; scene reads and writes are valid only during ready/process. The separate GUI callback can show state and queue changes for the next update. Exit/stop has no scene frame. There is no fixed-step callback, `[Export]` persistence, generated signals or cross-language event bus. C# manifests currently name the fully qualified class. A complete binding pipeline needs two distinct generators: libclang IR for public engine API members, and a Roslyn source generator on user `partial` scripts for script paths and property/signal metadata. Godot's [script-path generator](https://github.com/godotengine/godot/blob/master/modules/mono/editor/Godot.NET.Sdk/Godot.SourceGenerators/ScriptPathAttributeGenerator.cs) illustrates the second role.

`ScriptHost(scene, true)` checks module files between callbacks. It stages replacement extensions and their dependent C++ script instances before stopping the old callbacks, then unloads the old library. A bad replacement leaves the old code active. Lua VMs and C# collectible contexts are replaced similarly. Reload does not transfer script-local state; scene data remains owned by the host. Future callbacks that retain GPU resources will also need an explicit GPU retirement point.

## Package contract and open boundaries

The runtime statically links its engine libraries and the optional Lua host. A project VPK carries its native modules, C# assemblies and assets. The player currently extracts project and built-in VPK contents for file-based loading; native libraries require a real file path. C# projects also need an installed .NET 10 runtime. On Linux, system Vulkan, display-stack and libc libraries remain required, so “single executable” does not mean a fully static ELF.

Before the first public release, every serialized-data and file-structure version stays at `1`; format edits are breaking changes across writer, reader, examples and tests. The C ABI version is a separate call contract. Stream-backed asset/UI reads, live GPU scene transforms, broader resource/server/editor bindings, and `vultra-app` remain open. A Python adapter would need batch scene/data calls for AI simulation rather than per-node C calls; [Infernux](https://infernux-engine.com/) is a reference for that use case.
