# Development Guide

This guide describes Vultra's current contracts and limitations. For setup and example commands, see the [README](../README.md). Development constraints are in [AGENTS.md](../AGENTS.md).

## Source Layout

Each module keeps public headers under `source/<module>/include/vultra/<module>` and implementation files under `source/<module>/src`. Public includes remain `<vultra/...>`.

```text
core/       Logging, math, images and basic values
platform/   Desktop windows, input and OS operations
drivers/    VRI device/resources/swapchain, profiling and OpenXR GPU integration
assets/     CPU SceneData, import, derived cache and texture preparation
servers/    RenderGraph, GpuScene, BuiltinRenderer and RenderingServer
scene/      Camera controllers, editable Node/Resource identities and SceneTree
ui/         EditorGui (ImGui) wrappers and optional VGui (RmlUi) adapter
main/       BaseApp, DesktopApp, ImGuiApp and RuntimeContext
api/        Checked-in reflection and C ABI generated from annotations
```

The one public `vultra` static library contains these modules. `assets` uses CPU-owned `TextureFormat` and `SceneSampler` values; VRI conversion happens during upload. Import implementations call `platform` file and memory services, while public asset types remain independent of the window and GPU layers. A direct C++ experiment can use VRI or RenderGraph without creating a scene tree or a rendering server. `GpuScene(Device&, SceneData)` remains available alongside `RenderingServer::uploadScene`, which returns a move-only handle. Server RIDs are transient and must not be saved in assets.

Useful starting points are [DesktopApp's loop](../source/main/src/app/desktop_app.cpp), [the research example](../examples/research/main.cpp), [the glTF Viewer](../examples/scene/helmet.cpp) and [the built-in renderer](../source/servers/src/rendering/builtin/builtin_renderer.cpp).

## Generated API and Scripting

Annotated C++ UI, scene, renderer-settings and experiment declarations are parsed by `scripts/codegen.py` using the current `.vscode/compile_commands.json`. The generator writes a deterministic IR, Inspector descriptions, C function tables, matching C# and Python ABI layouts and Lua value-table marshaling. Run `xmake codegen` after changing an annotation, then `xmake codegen --check`; ordinary builds do not load Python or libclang. `VULTRA_BIND_POD` marks camera, light, translation and material value types; their ordered float fields generate C/internal C# layouts and Lua table conversion from the same IR. Other POD field types and unsupported annotated signatures fail generation.

The GUI layer exposes `EditorGuiFrame` widgets, `EditorGuiWindow` and `EditorGuiLayout` regions, and `EditorGuiInspector` property rows. An `EditorGui` context owns the property-drawer table: register by property ID with `setPropertyDrawer`, keep callback `userData` alive until removal, and draw only during the active GUI frame. Research customizes its generated render-path field through this table; the generated descriptors also work with the default drawer.

The C tables have a version and `struct_size` (ABI version 1). Strings use UTF-8 pointer/length pairs. `VultraUiFrame` is valid only during `on_gui`; `VultraSceneFrame` is valid only during update/ready callbacks. The scene table exposes root/child/name queries, local node translation, node/mesh creation, reparenting, mesh duplication, model selection, removal, camera selection, camera/light settings and shared material creation/parameters/mesh references through process-local `ObjectId` values. Invalid or foreign IDs, wrong node types and invalid values return `VULTRA_STATUS_INVALID_ARGUMENT`; a frame used after its callback returns `VULTRA_STATUS_INVALID_FRAME`. Rejected settings leave the node unchanged. Returned name bytes are borrowed and must be copied if retained. A native plugin includes `<vultra/api/native_plugin.h>` and exports `vultra_plugin_init`; it does not link a second copy of `vultra`. Bootstrap checks require exact table sizes; larger or smaller layouts are rejected. Before release, the ABI remains version 1; rebuild native and managed modules after any breaking API change. To try the C-only example:

```sh
xmake build example-native-plugin example-ui
VULTRA_NATIVE_PLUGIN=build/linux/x86_64/release/libexample-native-plugin.so xmake run example-ui --frames 60
```

The plugin updates a counter in the application update phase and draws through the generated UI ABI in the GUI phase. The same module can be declared in a project's `extensions` array without attaching it to a node. When hosted by a project runtime, it also reads the root's first child through the generated scene ABI and shows its name. It is unloaded after callbacks stop. Native C++ scripts use `<vultra/scripting/native_script.hpp>` for `NativeScript`, `actor()` and typed callbacks; a single `VULTRA_NATIVE_SCRIPT(MyScript)` line exports the required C entry. The adapter translates exceptions and scene access to the generated C ABI. Ordinary C++ programs can instead link `vultra` directly. Lua and C# remain optional, and direct C++ users still depend only on `vultra`.

## Scripting Example and Reload

`example-scripting` is a separate target because the C# sample needs the .NET SDK. Its xmake target builds the managed sample into ignored `build/.tmp/scripting-managed`; the running executable does not invoke xmake or dotnet. It renders a ship, a beacon and a throttle marker from one `SceneTree`. A C ABI extension adds diagnostic UI, while a C++ extension provides the ship script class. Three node scripts have different jobs: native C++ moves the ship using the throttle value, Lua detects pickups and relocates the beacon, and C# animates the throttle by default and its buttons switch to manual control. Each script is attached to a persistent scene node ID: `Ship` for C++, `Beacon` for Lua and `Throttle` for C#. The host resolves that ID when loading and rejects a missing target. Scene writes happen in the update phase; EditorGui callbacks only display state and collect button clicks. The C-only ABI plugin remains in `example-ui` as a separate minimal example:

```sh
xmake build example-scripting
xmake run example-scripting --frames 120
```

The three author-facing entry points are [the C++ `ShipOrbit`](../examples/scripting/native_cpp.cpp), [the Lua `BeaconPickup`](../examples/scripting/lua/scene_probe.lua) and [the C# `ThrottleController`](../examples/scripting/csharp/ThrottleController.cs). C# follows Godot's node-subclass style: `ThrottleController : Node3D` directly uses `Position` and overrides `_Ready()`, `_Process(double)`, `_EditorGui` and optionally `_ExitTree`. The script project references only `Vultra.Scripting` and does not enable unsafe code. Lua returns a table with `self.node`, `self.scene` and `_ready`/`_process`/`_editor_gui`/`_exit_tree` methods. C++ derives from `vultra::scripting::NativeScript`, overrides `onStart`/`onUpdate`/`onGui`/`onStop`, and accesses the attached node through `actor()`. Ready/start runs at the first update, before processing. Exit/stop has no scene frame. Scene access outside ready/process is rejected; GUI operations are valid only during the GUI callback. No fixed-step physics hook, exported script properties or cross-language event bus is implemented yet.

The version-1 project manifest has a separate `extensions` list of native library paths. The runtime starts these modules before the `scripts` list; an extension may update the scene and draw through EditorGui without owning a scene node. A project manifest script entry names `language` and `path`. C# entries additionally require `type` (the fully qualified class name) and `node` (a persistent scene node UUID). A C++ script entry sets `type` to a class registered by its native module, and may set `node`; the C-only plugin needs no class name. Lua may also set `node`. Native and Lua entries without a node attach to the root. One native library can register several C++ script classes with `VULTRA_NATIVE_MODULE(VULTRA_NATIVE_CLASS(First), VULTRA_NATIVE_CLASS(Second))`; the one-class shorthand is `VULTRA_NATIVE_SCRIPT(First)`. Declaring that same C++ library in `extensions` makes it the class provider for matching native script entries, so the host loads it once and creates separate node instances. On hot reload, the host validates replacement classes and instances before stopping the old module; missing classes keep the old code active. The extension ABI also supplies scene/UI callbacks, but resource/server/editor type registration is not implemented. These are pre-release format-1 contracts, so changes deliberately break older local packages. Native, C# and currently Lua modules are materialized selectively for their loaders; each Lua module owns its VM. The source-backed ABI generator currently emits the C UI/scene tables and C# low-level layouts. Native/Lua high-level adapters and the safe managed `Node`/`Node3D` API are still handwritten. `VultraBindings.g.cs` contains internal ABI layouts. `VultraValues.g.cs` generates safe public settings records and their conversions; generating node wrappers, script metadata and generic object/property access from the same IR remains open work.

For live reload, run without `--frames`, edit `examples/scripting/lua/scene_probe.lua`, rebuild `example-native-cpp-plugin` with xmake, or run `dotnet build examples/scripting/csharp/VultraScript.csproj -c Release -o build/.tmp/scripting-managed` in another terminal. The host checks the source files during `onUpdate`. Native modules load from unique copies so their original DLL/SO can be rebuilt. C# module code is loaded from bytes into a collectible `AssemblyLoadContext`; the process loads one compatible CoreCLR runtime. The replacement loads before the old module stops; a load or initialization failure leaves the old callbacks running. A later exception inside the new process callback is a runtime error, not a rollback.

On C# reload, JSON-compatible instance fields declared by the user script are copied from that slot's previous instance by declaring type and field name. A new instance starts with its constructor defaults. Static, readonly and `[NonSerialized]` fields are not copied; fields that cannot be serialized or restored log an error and retain their new default value. Each field is serialized separately, so shared object identity and cyclic graphs are not preserved. Restoration happens before `_Ready` runs again; initialization code can overwrite restored values. Native and Lua script-local state still resets. Scene node translations persist because the scene belongs to the host.

The GUI button retries failed replacements on the next update, even if their files have not changed again. Native DLL, Lua and .NET 10 replacement failure/recovery are also tested on Windows. Restoring a last-known-good file remains detectable even when its original timestamp is preserved.

On Windows, an incremental build after editing only the user C# script replaces `VultraScript.dll` while the example runs. A full `dotnet build -t:Rebuild` also tries to replace the shared `Vultra.Scripting.dll`, which the running CoreCLR process keeps locked; stop the example before rebuilding the scripting API or managed host. Rebuilding a native plugin in place can briefly log a copy failure while the linker writes its DLL; the host retries on the next update and reloads the completed file.

After building `example-scripting` once, this command watches C# source and rebuilds only the user assembly on save while the example runs:

```sh
dotnet watch build --project examples/scripting/csharp/VultraScript.csproj -c Release -o build/.tmp/scripting-managed --no-dependencies --no-hot-reload
```

`--no-dependencies` leaves the loaded engine scripting API alone. Rebuild that API and restart the example when its declarations change. The .NET watcher builds the DLL; Vultra detects and swaps it. This does not use .NET's in-process Edit and Continue.

The managed UI-phase control can be checked without a GPU using `dotnet run --project tests/managed_control/ControlTest.csproj -c Release`; it checks that a button click changes the scene on the next update and that prewarmed process/UI callbacks do not allocate managed memory. The `ScriptHost` entry point is in `<vultra/scripting/script_host.hpp>` and requires `add_deps("vultra-scripting")`. It is optional for a pure C++ program. The generated C# layouts live in `source/api/csharp/VultraBindings.g.cs`; normal C++ builds do not run codegen or require the .NET SDK. The optional [Python adapter](python_research.md) provides offline experiments and owned NumPy images. Batch scene/action calls for AI simulation remain future work. The [architecture guide](architecture.md) records the ownership boundaries and references, including [Infernux](https://infernux-engine.com/) for Python-oriented simulation design.

## Project Package and Runtime

A `.vproject` manifest assigns persistent `AssetId` values to project-relative files and names a `.vscene` entry scene. A scene node has its own persistent node ID and a separate runtime `ObjectId`; neither is a `RenderingServer` RID. Renaming an asset path preserves its asset ID. The initial `SceneTree` format and project manifest are versioned JSON and reject missing assets, duplicate identities and paths that leave the project root. The runtime and Research `--project` import every mesh node, apply parent and local transforms, and combine them into one GPU scene. Repeated references import the model and prepare its textures once. Geometry and numeric material slots are per instance, so overriding one instance cannot recolor another. The packaged runtime updates each imported instance's GPU transform after script callbacks, including changes inherited from parent nodes. The packaged runtime detects mesh additions, removals and model swaps after script updates and reimports the GPU scene only for those structural changes. An empty scene still renders the environment and UI. Research `--project` remains a static import. Direct C++ renderers can keep using `GpuScene(Device&, SceneData)` without a scene tree.

Scene-owned `MaterialResource` values have stable asset IDs and runtime `ObjectId`s. Mesh `materials` entries map imported material slots to these IDs. The required scene-level `materials` table embeds named OpenPBR numeric parameters; an empty table and empty mesh overrides retain imported values. Resources may be shared by several meshes. Texture bindings and double-sided raster state remain in the imported model. A resource supplies the complete numeric parameter set, not a partial patch. `MaterialParameters::fromMaterial()` copies a source material's numeric values when authoring an override.

After previous GPU use completes, `SceneGpuSync::update()` applies changed transforms and material constants. Material synchronization validates references/slots before changing constants; clearing an override restores the imported numeric baseline. Removing a resource clears mesh references and invalidates its runtime handle. The runtime calls this after script updates; Research and the workbench call it before recording. Geometry, textures, descriptors and the compiled graph are retained. Direct C++ renderers can edit `GpuScene::materials` without constructing a scene tree, or call `syncSceneMaterials()` explicitly.

`SceneTree::changes()` exposes process-local revisions for structure, transforms, materials, camera, lighting, environment and metadata. Setters notify only after successful changes; assigning an unchanged value does nothing. Each consumer keeps its own snapshot, so observing changes does not consume another view's notifications. Structure changes invalidate dependent state, including camera/environment selection when a subtree is removed. These revisions are neither persistent IDs nor serialized versions. Scene edits and synchronization belong to the application thread; there is no asynchronous event bus.

Keep one `SceneGpuSync` per uploaded scene and its `SceneMeshInstance` ranges. `needsImport()` distinguishes actual mesh additions/removals/model changes from reparenting: ranges are matched by stable node ID, so a different traversal order does not require geometry upload. `environmentChanged()` gates source resolution/preprocessing. Failed synchronization leaves the cursor dirty for retry; it is not a transaction over all GPU constants. Reconstruct the cursor after replacing an upload. Moving a tree retains attached nodes' notifications; a detached subtree stops notifying its former tree until attached again. Idle frames skip material, transform and topology scans. The workbench previews transform/reparent edits through this path; mesh membership/model changes still require applying a new scene candidate.

The common generated ABI exposes material creation, names, parameters, mesh assignment/clearing and deletion. Native C++ uses `createMaterial()`, `SceneMaterial::parameters()/setParameters()` and `SceneNode::setMaterial()`; Lua uses `self.scene:create_material()`, `material:parameters()/set_parameters()` and `node:set_material()`; safe C# uses `Scene.CreateMaterial()`, `Material.Parameters` and `Node3D.SetMaterial()`. Getters return value snapshots. Assign modified values back to the resource during a ready/process callback. Assigning `null`/`nil` or calling C++ `clearMaterial()` clears the mesh override. Mesh slots must exist in the imported model; the import/synchronization boundary validates their bounds.

```csharp
var paint = Scene.CreateMaterial("Paint");
paint.Parameters = paint.Parameters with { BaseColor = new Vector4(0.2f, 0.6f, 1, 1), Roughness = 0.7f };
GetChild(0).SetMaterial(0, paint);
```

`resources/research_lighting.vproject` demonstrates an authored camera, three light kinds, an HDR environment node and the helmet's shared material. The workbench Scene inspector selects nodes and material resources, edits local transforms and typed settings, assigns material slots and chooses the current camera/environment. Camera, light, environment and material property descriptions/drawing functions now come from the same libclang IR as RenderSettings. Node selection, transform editing and asset references remain explicit editor code; C++26 reflection is not used.

`vultra-pack` writes an uncompressed VPK with the manifest, scene, declared assets and script modules. A C# module also packs the sibling safe API `Vultra.Scripting.dll`, internal `Vultra.ManagedHost.dll`, its required `.runtimeconfig.json`, and any `.deps.json` files. Declare other managed dependency DLLs as project assets. The project VPK contains no engine shaders. The `vultra-runtime` binary embeds a separate checked VPK of cooked built-in SPIR-V programs and OpenPBR attribution at build time. A project VPK can stay external or be appended to a copy of the runtime. The appended archive has a versioned, checksummed footer; the runtime reads it from its own executable when no package path is supplied. An explicit package path selects an external VPK. Both forms read project assets directly through `AssetSource`. Modules that require filesystem paths are materialized selectively and removed after their hosts stop. The embedded engine-shader bootstrap still extracts its separate resource pack until shutdown. VPK entries have XXH3 checksums and reject corrupt data and traversal paths.

Build the tools once, then use the standalone packer directly. Exporting and running do not invoke xmake:

```sh
xmake build vultra-pack vultra-runtime
./build/linux/x86_64/release/vultra-pack resources/research.vproject build/.tmp/research.vpk
VULTRA_WINDOW_SYSTEM=wayland ./build/linux/x86_64/release/vultra-runtime build/.tmp/research.vpk --frames 60
./build/linux/x86_64/release/vultra-pack --embed ./build/linux/x86_64/release/vultra-runtime build/.tmp/research.vpk build/.tmp/research-game
VULTRA_WINDOW_SYSTEM=wayland ./build/.tmp/research-game --frames 60 --capture build/.tmp/research-game.png
```

An editor can call `VpkArchive::packProject()` and `VpkArchive::embedProject()` directly; these operations require neither xmake nor a build tool on the target machine. The output path must be new, the source runtime must not already contain a project, and Unix execute permissions are preserved. On Windows, use a `.exe` output path. Both external and embedded VPK modes have been verified from isolated working directories without xmake; deployment to a separate clean machine still needs validation. The footer must remain at the end of the executable, so code signing is not yet supported for this export mode.

The runtime includes ImGui today because native C ABI plugins may draw through its generated UI bridge. The built-in debugger is opt-in: launch either package form with `--debug-ui` to show render settings, previous-frame CPU/GPU time and the compiled RenderGraph pass list; press F1 to hide or restore it. Project plugin GUI callbacks still run when the built-in debugger is hidden. This is a visibility option, not yet a build variant that removes ImGui from the executable. The current runtime debugger is smaller than the interactive Research example and does not show intermediate attachment previews.

Game-facing UI uses [RmlUi 6.2](https://github.com/mikke89/RmlUi/tree/6.2) (MIT) through the optional `vultra-vgui` static target. `VGui` owns a RmlUi context and converts compiled geometry, premultiplied RGBA textures, scissor regions and input into VRI operations. The runtime links it statically; direct C++ rendering projects that only depend on `vultra` do not link RmlUi. The research project declares `ui_document` and `ui_font` asset IDs for `ui/hud.rml` and the Lato Latin font; its RCSS and font license are also listed as project assets. The packer includes those assets in external or embedded VPKs. The runtime gives VGui the project `AssetSource`; RmlUi reads documents, relative RCSS, fonts and PNG textures directly from checked VPK entries. A styled checkbox toggles the skybox and updates a status label. The sample font is distributed under the SIL Open Font License in `resources/ui/LICENSE.txt`.

`EditorGui*` names identify the existing ImGui editor/debug layer. It remains available to research examples and native UI plugins, with `--debug-ui` opt-in for the runtime's built-in panel. `VGui` is the authored in-game layer. It automatically applies a neutral style with an embedded PNG sprite atlas when loading a document; document RCSS can override it. Xmake embeds the atlas into `vultra-vgui`, so the runtime needs no UI texture sidecar. The style covers panels, rows, buttons, checkboxes, radio buttons, text/password fields, text areas, dropdowns, range sliders, progress bars and scrollbars. `example-ui` shows these controls beside raw ImGui and EditorGui; its Kenney UI Pack skin demonstrates texture-backed button states, icons, toggles, radio buttons and a slider alongside text, dropdown and progress controls. The Kenney PNGs are example-only CC0 assets; the packaged runtime uses the built-in skin for its settings panel. RmlUi creates internal parts for sliders and dropdowns, which the built-in style also covers; see the [RmlUi control style guide](https://mikke89.github.io/RmlUiDoc/pages/style_guide.html). A font still needs to be loaded by the application. `bindChange()` reports control changes; `value()`/`setValue()` handle text, sliders and dropdowns, while `isChecked()`/`setChecked()` handle toggles. `setText()` inserts literal text and escapes RML markup. Call these methods on the owning VGui context during its active application lifetime. The renderer supports 2D geometry, PNG image sources, generated font textures and rectangular scissoring; RmlUi's optional transform, mask, layer, shadow and filter hooks are not implemented. Project skins can use `<img src>` and RCSS `decorator: image(...)` with PNG files; declare those files as project assets before packing a VPK. See RmlUi's [image](https://mikke89.github.io/RmlUiDoc/pages/rml/images.html), [sprite sheet](https://mikke89.github.io/RmlUiDoc/pages/rcss/sprite_sheets.html) and [image decorator](https://mikke89.github.io/RmlUiDoc/pages/rcss/decorators/image.html) references. Both UI layers use the same VRI device and frame timing. VGui documents are authored as RML/RCSS.

Pass a borrowed `AssetSource` to load documents, relative stylesheets, fonts and PNGs directly from a directory or
checked VPK entries. The source must outlive VGui. `VGui(device, extent, format, source)` renders without a window,
using framebuffer pixels as logical coordinates. Complete previous GPU work before updating, replacing documents
or destroying VGui. One live VGui owns [RmlUi's process-wide interfaces](https://github.com/mikke89/RmlUi/blob/6.2/Source/Core/Core.cpp).
Resource reads fail explicitly; failed document or stylesheet reads retain the active document and listeners.

```cpp
vultra::AssetSource assets(vultra::VpkArchive("game.vpk"));
vultra::VGui gui(device, extent, VriFormat_RGBA8_UNORM, &assets);
gui.loadFont("ui/font.ttf");
gui.loadDocument("ui/hud.rml");
```

`test-vgui-offscreen` checks filesystem, VPK and embedded-VPK pixel parity, PNG/font/relative RCSS reads,
checkbox input, interface teardown and resource failure/recovery without opening a desktop window.

The packaged runtime renders the entry scene's mesh instances with live transforms. Structural mesh changes trigger a full import and GPU upload after the current update; this is intended for infrequent edits, not per-frame spawning. Native C++, Lua and C# scripts can duplicate an existing mesh instance under a chosen parent or copy the model asset reference from another mesh. Duplication shares the mesh asset and material resources and copies the local transform, creates a fresh persistent node ID, and triggers a GPU scene rebuild after the update; it does not copy child nodes or script attachments. Scripts can remove any non-root node, including their own node or an ancestor, through C++ `SceneNode::remove()`, Lua `node:remove()` or C# `Node.Remove()`. The host stops instances bound to removed nodes after the current update callback and skips their later callbacks; detached instances are not reattached automatically. Removing the last mesh leaves the scene renderable. A script can select a model declared in the project manifest by its stable UUID: C++ `SceneNode::setMeshModel(id)`, Lua `node:set_mesh_model(id)` or C# `Node3D.SetMeshModel(id)`. The API rejects malformed, missing and non-model asset IDs before changing the node. A model swap triggers the same completed-frame GPU scene rebuild. Pass the manifest as a borrowed argument when constructing a project script host, for example `ScriptHost host(scene, false, &project);`; a project-free host cannot use this operation. Scripts can also create a group node and a mesh node directly from a declared model asset under any live parent: C++ `createChild`/`createMeshChild`, Lua `create_child`/`create_mesh_child`, or C# `CreateChild`/`CreateMeshChild`. `reparent`/`Reparent` changes a node's parent while preserving its local transform, so its world transform may change. Root, foreign and cyclic reparenting are rejected. Newly created nodes receive fresh runtime and persistent IDs; they do not inherit script attachments.

The optional `vultra-scripting` static library hosts declared native, Lua 5.4 and C# modules. Native and C# modules are project payloads in the VPK and are extracted to real files for loading; Lua 5.4 and engine dependencies are linked into the executable. A C# project needs the installed .NET 10 runtime; `hostfxr` is resolved when its first module starts. Build scripts with the .NET 10 SDK. The safe `Vultra.Scripting` API assembly holds internal generated ABI layouts; `Vultra.ManagedHost` loads user `Node` subclasses into collectible assembly contexts. User scripts compile without unsafe code. This reload model does not use Native AOT, whose shared libraries cannot be unloaded. Offline sessions read CPU assets directly from checked VPK entries and materialize script files individually. The project runtime and VGui share the same direct VPK source. Native/.NET modules, and currently Lua scripts, are materialized selectively. Only the embedded engine-shader bootstrap still extracts its resource pack. Before the first public release, all serialized and file-format versions stay at `1`. Format changes deliberately break old files and caches; there are no migration or compatibility readers.

A Linux release executable still relies on operating-system facilities such as the ELF loader, libc, Vulkan loader/GPU driver and the chosen X11/Wayland stack. “Single executable” means no engine or third-party `.so` sidecars are shipped alongside `vultra-runtime`; it does not mean a fully static Linux ELF. Inspect `readelf -d` and `ldd` before distributing a build, and verify on a clean target system.

The Linux delivery link rule applies `--as-needed` around inherited libraries. The current batch/player ELF files
directly require only the Vulkan loader, libc/libm and the system loader; window libraries are resolved when the
selected backend initializes. The optional research shared library also uses the system C++ runtime. Inspect
`readelf --version-info` for each artifact: a development-machine build can require newer glibc symbols than an
older distribution provides. Export templates need a deliberately chosen target sysroot, not only a platform name.
Native VPK modules require an extraction filesystem that allows executable mappings. Set `TMPDIR` to a writable,
executable filesystem before launch if the default temporary directory is mounted `noexec`; loading fails explicitly
otherwise. Lua source does not require an executable filesystem.

## Desktop Platforms

Windows x64 and Linux x86_64 share the VRI Vulkan renderer and application lifecycle. `libvultra_window_backend` selects `glfw` (default, 3.4) or `sdl3` (3.4.0) at build time. Both Linux backends include X11 and native Wayland. Set `VULTRA_WINDOW_SYSTEM=x11` or `VULTRA_WINDOW_SYSTEM=wayland` before launch to select explicitly; an unset or empty value leaves selection to the backend, while an invalid value fails initialization. Other Linux architectures are outside this build's scope. DesktopApp requires a display connection; ExperimentSession and `vultra-batch` do not. GPU tests still require their documented Vulkan features.

`Window` exposes backend-independent title, size, minimization and event-wait operations. Its opaque `handle()` is a backend pointer only for platform interop; use `platform::nativeWindow()` for VRI's native descriptor. VRI's GLFW/SDL3 integration constructs Win32, X11 or Wayland handles directly. Multiple owned windows retain their backend runtime until the last owner closes; borrowed ImGui windows never terminate it. Wrap each backend window once, and destroy a borrowed wrapper before its native window. Window and GUI operations run on the application thread.

Wayland compositors control placement, focus, minimization and restoration. Resize and output-scale changes arrive asynchronously. `framebufferSize()` supplies physical pixels and `size()` supplies logical input coordinates; swapchain acquisition tracks negotiated sizes. Do not assume a requested size is immediately accepted, or require programmatic restoration on Wayland. Both pinned ImGui platform backends disable detached native viewports there; Vultra reports this and keeps docking inside the main window. Use X11/XWayland for detached GUI windows.

Linux reads the executable path from `/proc/self/exe` and available memory from `/proc/meminfo`. Cache publication writes a sibling temporary file and atomically replaces the destination with POSIX rename. Readers see the previous complete file or the replacement; failure removes the temporary file and preserves the destination. This is an atomic visibility contract, not a power-loss durability guarantee.

The Linux model picker uses Native File Dialog Extended v1.3.0's portal backend and UTF-8 paths. X11 supplies a parent handle. NFD 1.3 has no Wayland parent-handle interface, so Wayland opens a parentless portal dialog with an explicit warning. It requires the session D-Bus, `xdg-desktop-portal` and a desktop-specific portal backend. Cancellation returns no path; portal failures propagate to the caller. Interactive selection/cancellation on Wayland remains unverified. Windows retains its Win32 picker. The native VRI descriptor declaration lives in `platform/window.hpp`; each window backend implements it. Swapchain render areas use VRI's actual image extent, which can differ from the latest window size during resize. The [pinned VRI validation patch](../external/vri/README.md) forwards that query through validation; device creation fails explicitly if it is unavailable.

ISPC uses the host executable name and target operating system, with position-independent code on Linux. OpenXR stays at 1.1.49; Linux builds its static loader with the SDK-vendored JSON parser to avoid the [system JsonCpp export issue](https://github.com/KhronosGroup/OpenXR-SDK-Source/issues/481). The `libvultra_with_openxr` option remains enabled by default.

## Application Lifecycle

BaseApp supplies `run()`, `close()`, `frameCount()` and update callbacks. DesktopApp delegates Window, Device, Swapchain, Frame and RenderingServer ownership to RuntimeContext in dependency order. Constructors and RAII handle initialization and cleanup; base constructors and destructors do not call derived virtual functions.

A desktop tick follows this order:

```text
poll -> onPreUpdate(dt) -> onUpdate(dt) -> onPostUpdate(dt)
     -> acquire -> onResize (first frame or new extent) -> onPreRender()
     -> onRender(cmd, target) -> Present layout -> submitAndWait
     -> onPostRender(target) -> present -> onPostPresent() -> frameCount++
```

`dt` uses a steady clock. When the main window is minimized or acquisition is temporarily unavailable, logic continues, `onRenderSkipped()` runs and the loop briefly waits for events. The main render callbacks are skipped and `frameCount()` does not advance. ImGuiApp continues updating existing detached viewports through this path.

Calling `close()` during updates finishes the update phases and exits before acquisition. After acquisition, a close request still lets that frame complete and present. `--frames N` limits presented desktop frames; zero runs until closed. In render callbacks, `frameCount()` is the current frame's zero-based index.

`onRender()` records commands. `onPostRender()` runs after GPU completion and before presentation, making it suitable for readback and profiler collection. Access the owned resources through `getWindow()`, `getDevice()` and `getSwapchain()`.

All example window titles retain their example/model name and append FPS, CPU milliseconds and GPU milliseconds. Statistics update after the first completed frame, then average over half-second intervals. FPS measures complete application frames, including waits and capture work. CPU measures event/update work, render preparation and command recording, excluding acquisition, the explicit GPU wait, presentation and post-render capture. GPU timestamps cover the main frame command buffer; XR includes both eyes, mirror and UI, excluding the runtime compositor. Detached ImGui windows are not included in the main GPU interval. Unsupported timestamp queries display `GPU: N/A`.

ImGuiApp calls `EditorGui::begin()`, `onImGui()` and `EditorGui::upload()` from `onPreRender()`. An override calls the base first to finalize the UI capture decision, then reads `Window::input()` for camera controls. ImGui's transient input may already be cleared by `EditorGui::upload()`. The application chooses where to draw the overlay: call `drawGui()` outside a rendering pass, or use `getEditorGui()` in an explicit RenderGraph pass.

The XR examples share `examples/common/xr_sample.*`, derived from BaseApp, with separate update, GUI and eye-render phases: the runtime participates in device creation and controls XR frame timing.

## ImGui and Layouts

Vultra uses the ImGui docking branch. Docking is enabled by default; multiple native viewports are enabled when the platform backend supports them. `EditorGui::begin()` creates a transparent root dockspace; `dockspaceId()` can be used to arrange an initial layout.

Each detached window owns a GLFW or SDL3 window, swapchain, frame and VRI GUI geometry buffers. ImGuiApp renders these from `onPostPresent()`. Applications using EditorGui directly must call `renderPlatformWindows()` after the main GPU frame completes, while referenced textures remain alive. Position and clipping data are converted to framebuffer pixels per viewport.

The default theme is `EditorGuiTheme::eUnreal`. `EditorGuiConfig::theme` selects Dark, Light, Unity, Unreal or Godot at construction; `EditorGui::setTheme()` switches it at runtime. These editor-inspired palettes are approximations. Theme changes preserve layout spacing and the constraints needed by detached viewports.

`EditorGuiInspector` draws property rows in an ImGui window: labels on the left, controls on the right. Its explicit property/drawer split takes inspiration from [Godot's inspector plugins](https://docs.godotengine.org/en/stable/tutorials/plugins/editor/inspector_plugins.html), adapted to immediate-mode drawing. Give each property an ID independent of its display label so labels can change without losing widget state:

```cpp
if (vultra::EditorGuiInspector inspector(getEditorGui(), "Settings"); inspector)
{
    inspector.boolField({"skybox", "Skybox"}, &settings.skybox);
    inspector.floatSlider({"exposure", "Exposure (EV)"}, &settings.exposure, -4.0f, 4.0f);
}
```

`EditorGuiInspector::property()` accepts a `EditorGuiPropertyDrawer` callback for custom value widgets. The callback receives the property's ID and label and runs immediately on the UI thread in the value column; all pointers are borrowed for that call. A single-widget drawer can use `##value` as its widget ID; multiple widgets need distinct IDs within the property scope. The Research example uses one for its render-path enum.

The generated native-plugin C ABI keeps ImGui types and C++ class layouts behind opaque handles. The managed, Lua and native adapters consume the same versioned tables; script authors use their node-oriented wrappers.

Layout persistence is enabled by default. Modes in one category share that category's layout:

```text
.vultra/example-ui/imgui.ini
.vultra/example-scene/imgui.ini
.vultra/example-ray/imgui.ini
.vultra/example-xr/imgui.ini
.vultra/example-research/imgui.ini
```

An empty `appName` uses the executable stem, independent of the window title or loaded model. An empty `iniFile` selects `.vultra/<AppName>/imgui.ini` under the working directory. The directory is created automatically and the resolved path stays fixed for the context's lifetime.

```cpp
vultra::EditorGuiConfig guiConfig;
guiConfig.appName = "my-research-app";
// Optional explicit path; takes precedence over appName:
// guiConfig.iniFile = "layouts/experiment.ini";
// Disable both loading and saving for tests or temporary windows:
// guiConfig.persistLayout = false;
```

Pass this as ImGuiApp's second constructor argument or EditorGui's configuration argument. AppName must be a valid single directory name and may contain Unicode. ImGui loads on the first frame, periodically saves changed layouts and saves on normal shutdown. `.vultra/` is ignored by Git. The old shared `imgui.ini` is not imported; delete an application's file to reset its layout.

Use `EditorGui::textureId(texture)` with `ImGui::Image`. Keep the texture in ShaderResource state until all viewport draws finish. Call `EditorGui::forgetTexture(texture)` before destroying or recreating it, after the previous GPU frame completes, to release cached VRI descriptors. Platform windows use the desktop BGRA8_UNORM format; disable `multiViewport` for an offscreen GUI targeting another format.

## Asset sources

`AssetSource` is an explicit filesystem directory or VPK owner. It does not register a global mount or own GPU
resources. `resolve()` returns a stable source path; package paths identify entries and are not real filesystem
files. `read()` returns owned bytes and verifies entry checksums, while `contains()` and `size()` query the index
without loading payloads. Relative dependencies resolve within the same source; escapes, missing entries and
corrupt payloads fail without falling back to local files.

```cpp
vultra::AssetSource assets(vultra::VpkArchive("research.vpk"));
auto project = vultra::ProjectManifest::load(assets.resolve("project.vproject"), &assets);
auto scene = vultra::SceneTree::load(assets.resolve(project.mainScene), &assets);
auto imported = vultra::importScene(scene, project, assets.root(), {}, nullptr, &assets);
```

The same glTF/GLB, OBJ/MTL, FBX, image/DDS and cooked game-shader implementations accept a borrowed source.
Derived cache recipes and dependency checks retain consumed-byte provenance; package data need not be extracted
for cache validation or restored DDS mips. Sources outlive borrowing environments and shader-material consumers.
Reads may run on import workers; selective `materialize()` calls belong to the owning thread. A materialized path
expires when its source is destroyed.

`ExperimentSession` accepts `.vpk` directly and owns that source. `scriptPath()` prepares only the requested script
and declared native/managed dependencies, including .NET host/API assemblies and descriptions. It uses the
manifest's language, not a DLL suffix, to select managed dependencies. Stop script hosts before destroying the
session. `ProjectManifest::materializeModule()` owns the shared module/sidecar selection used by both the session
and project runtime. Lua currently uses its existing file-loading adapter. The batch tool, generated-ABI Python
host and project runtime share direct package reads; embedded engine shaders retain their file-based bootstrap.

## Built-in Renderer

`RenderSettings::path` defaults to `RenderPath::eNaiveDeferred`; `eNaiveForward` remains available. Both paths share OpenPBR material sampling and lighting. The default graph contains four shadow passes, skybox, two G-buffer geometry passes, fullscreen deferred lighting and tone mapping, followed by the application's display copy, GUI and presentation passes. Each G-buffer pass uses at most four color attachments; this deliberately draws indexed geometry twice. The existing scene viewers and XR Sponza example explicitly select `eNaiveForward` to preserve their established rendering and mesh-shader paths. Deferred currently requires indexed geometry.

| Component | Current implementation |
| --- | --- |
| Skybox | HDR equirectangular environment with no positional parallax |
| Materials | Base color, metalness, roughness, normal, AO, emission, vertex color and alpha mask |
| OpenPBR | Public Adobe Slang BRDF; opaque base/specular/coat, with separate glTF emission |
| IBL | Diffuse convolution, GGX prefiltered mip chain and BRDF LUT with split-sum integration |
| Shadows | Four cascades, practical splits, texel snapping, caster bounds and cascade blending |
| Filtering | Hard, PCF and PCSS; depth/normal bias, receiver-plane correction and sun angular radius |
| Output | Linear RGBA16F HDR, D32 depth, fitted ACES tone mapping and sRGB display values |

The [OpenPBR integration notes](../external/openpbr/README.vultra.md) record the public source, fixed version, license and local Slang initialization patch. The adapter calls upstream `openpbr_prepare` and `openpbr_eval`; the latter already includes the incident cosine. glTF emission is added separately, without OpenPBR coat attenuation or shading-normal backface suppression. Upstream energy-compensation LUTs are uploaded as an RGBA32F atlas rather than embedded as large shader constants.

SurfaceMaterial currently exposes only the opaque subset. Transmission, subsurface, fuzz and thin-film transport are not wired into the renderer. IBL remains a separate GGX split-sum approximation, not full OpenPBR environment integration. CPU/GPU reference comparisons do not constitute complete OpenPBR conformance.

`SceneData` is CPU-owned mesh and material data loaded from glTF, OBJ or FBX, or generated by an experiment. `GpuScene` uploads it, `Environment` prepares lighting textures, and `BuiltinRenderer` adds passes to a graph:

```cpp
vultra::GpuScene gpuScene(device, scene);
vultra::Environment environment(device); // An optional .hdr path can be supplied.
vultra::BuiltinRenderer renderer(device, gpuScene, environment);
vultra::RenderGraph graph(device);
const auto outputs = renderer.addPasses(graph, {1280, 720});
graph.exportResource(outputs.color);
graph.compile();

// Each frame, after the previous GPU frame completes:
renderer.pollShaders();
renderer.prepare(camera, graph, outputs);
graph.execute(frame.begin(), &profiler);
frame.submitAndWait();
profiler.collect();
```

Outputs exposes `shadows`, `gbuffer`, `hdr`, `depth` and `color` for experiments to connect or export. Individual shadow, skybox, forward and tone-mapping pass builders are also available. The renderer, scene and environment must outlive the graph. Each renderer uses one set of per-frame descriptors and assumes one frame in flight. Rebuild the graph when changing render path or shadow resolution, and rebuild GpuScene/renderer when replacing geometry or the texture collection. Uploaded material scalar parameters can be edited through `GpuScene::materials`.

IBL precomputation settings are local to `environment.cpp`: diffuse 64x32, specular 256x128 with nine mips, a 128x128 BRDF LUT and 256 samples. The default environment is procedural; supply a Radiance HDR image for measured lighting. Cubemaps, EXR and runtime reflection probes are not implemented.

## Scene Cameras, Lights and Environments

`CameraNode` and `LightNode` are optional scene objects. `SceneRenderState::update()` resolves parent/local transforms into a reusable CPU camera/light snapshot when relevant revisions or the render extent change; its return value reports a changed snapshot. Environment-only edits reuse the camera/light data, and material/name edits do not rebuild it. `BuiltinRenderer::prepare()` copies the snapshot into existing per-frame GPU data. Camera and light edits do not reimport geometry, recreate graph textures or compile pipelines. Research `--project`, the workbench, batch and the packaged player use this same snapshot. The player updates it after script callbacks.

```cpp
auto& camera = static_cast<vultra::CameraNode&>(
    tree.addChild(tree.root(), std::make_unique<vultra::CameraNode>("Camera")));
camera.setLocalTransform(glm::translate(glm::mat4(1), glm::vec3(0, 0, 3)));
tree.setCurrentCamera(camera.id());
auto& light = static_cast<vultra::LightNode&>(tree.addChild(
    tree.root(), std::make_unique<vultra::LightNode>("Point", vultra::RenderLightKind::ePoint)));
light.setLocalTransform(glm::translate(glm::mat4(1), glm::vec3(0, 0, 2)));
auto settings = light.settings();
settings.intensity = 10;
light.setSettings(settings);

vultra::SceneRenderState state;
// Each frame, after the previous GPU frame has completed:
state.update(tree, extent);
renderer.prepare(*state.camera, graph, outputs, state.lighting());
```

Scene cameras look along local `-Z` with `+Y` up, using VRI's right-handed, zero-to-one clip depth convention. Parent transforms determine position/orientation; camera scale is removed when building its orthonormal view. FOV and spotlight cone half-angles are radians. Projection requires `0 < FOV < pi` and `0 < near < far`. Camera/light world transforms must be finite and affine with nonsingular orientation axes. Removing the current camera or its ancestor clears selection; selecting ID zero restores the caller's code-driven camera.

Directional and spot lights emit along local `-Z`; their incoming-light direction is local `+Z`. Point/spot lights use inverse-square attenuation with a smooth cutoff at positive world-space `range`. Light color/intensity must be finite and nonnegative; spot cones require `0 <= inner < outer < pi/2`. Intensity is a renderer scale, not a calibrated photometric unit. Up to 64 lights are supported; excess lights fail rather than being truncated. The first directional light uses the existing cascade shadows. Other directional, point and spot lights are currently unshadowed. Parent scale moves child lights but does not rescale their range or intensity.

Direct C++ calls may omit `prepare()`'s light span to keep `RenderSettings`' sun preset. A tree adopts explicit scene lighting when a light is added; removing its last light leaves direct lighting disabled. Version-1 `.vscene` saves this choice as `scene_lighting`, the selected camera's persistent ID as `current_camera`, and `Camera`/`DirectionalLight`/`PointLight`/`SpotLight` settings beside their transforms. Both selection fields are required; this is a pre-release breaking format change with version still `1`. Mesh-only scenes use `scene_lighting: false` and `current_camera: null` to retain the code-driven sun and camera; RID and runtime ObjectId values are never serialized.

Native scripts use `createCameraChild`/`createLightChild`, `cameraSettings`/`lightSettings`, setters and `makeCurrent`. Lua uses `create_camera_child`, `create_light_child(name, "point")`, settings tables and `make_current`. Safe C# uses `Camera3D`, `Light3D`, `CameraSettings` and `LightSettings`:

```csharp
private Light3D _light = null!;
public override void _Ready()
{
    _light = CreateLightChild("Point", LightKind.Point);
    _light.Position = new Vector3(0, 0, 2);
    var camera = CreateCameraChild("Camera");
    camera.Position = new Vector3(0, 0, 3);
    camera.MakeCurrent();
}
public override void _Process(double delta)
{
    _light.Settings = _light.Settings with { Intensity = 2 + (float)delta };
}
```

`EnvironmentNode` is non-spatial: parent and local transforms do not rotate or translate its lighting. `SceneTree::setCurrentEnvironment()` selects one node explicitly; clearing the selection or removing its ancestor restores the manifest's environment preset. The required version-1 `current_environment` field stores a stable node ID or `null`. A selected node stores a Radiance HDR `AssetId` or `null` for the procedural studio, plus nonnegative finite intensity. Unselected nodes never implicitly override the project. Declared assets must be `.hdr` images; the existing loader reports file/decode failures.

`sceneEnvironmentPath()` resolves the selected asset or project preset. `SceneRenderState` supplies the authored intensity, which `BuiltinRenderer::prepare()` multiplies by `RenderSettings::environmentIntensity`. Skybox and IBL toggles remain experiment settings. Intensity edits retain textures, geometry and the graph. Replacing an HDR source runs the existing preprocessing once through `Environment::setSource()` after previous GPU use completes; unchanged paths are a no-op. Decode/preprocessing failure retains the previous textures and reports an error. The workbench stages asset/selection edits at the completed-frame boundary and updates scene state only after lighting is ready. Direct C++ renderers can still construct `Environment(device, path)` and omit the optional intensity argument.

The common generated ABI supplies environment creation, selection, HDR asset assignment and value settings. Native C++ uses `createEnvironmentChild()`, `makeEnvironmentCurrent()` and `setEnvironmentSettings()`; Lua uses `create_environment_child()`, `make_environment_current()` and `set_environment_settings()`; safe C# uses `WorldEnvironment`:

```csharp
var sky = CreateEnvironmentChild("Sky");
sky.SetRadianceAsset("6ddad025-9e72-41d4-9d0e-e2fdc116ca27"); // Declared project HDR asset; null selects procedural lighting.
sky.Settings = new EnvironmentSettings(0.5f);
sky.MakeCurrent();
```

Research and batch `--environment` override the source image while retaining the scene intensity. The player and workbench follow the scene selection. Saved scenes and embedded workspace snapshots preserve the same stable selection, source and intensity; no runtime ObjectId is serialized.

Try `resources/research_lighting.vproject` with any project-loading frontend. `test-scene-rendering` checks both renderer paths, independent notification consumers, reparenting, topology rejection/retry, camera/light/material/environment updates, persistence, resource reuse and identical native/Lua/C# GPU outputs. The workbench Inspector consumes these same scene setters and synchronization helpers.

## Asset Import

Model examples use `importAsset()` and construct `GpuScene` from its prepared result. The [asset pipeline guide](asset_pipeline.md) explains cache identity, dependency tracking, texture color/compression policy, reimport, CLI options and limits. `loadGltf()`, `loadObj()` and `loadFbx()` remain available for uncached CPU experiments.

## Color Conventions

The old `dev` branch used two defaults: raw swapchain creation selected sRGB, while AppConfig selected linear/UNORM. This branch follows that distinction. `Swapchain` defaults to BGRA8 sRGB; `DesktopAppConfig` defaults to BGRA8 UNORM. Window and RHI triangle explicitly select sRGB. Clearing with linear `(0.2, 0.3, 0.3)` then stores approximately `(124, 149, 149)` in 8-bit sRGB.

ImGui, detached viewports and desktop tone-mapped output use UNORM with display-encoded colors. The built-in renderer's UNORM output applies ACES and one sRGB transfer. Its RGBA16F output applies ACES but stays linear for XR; raw HDR remains available in `Outputs::hdr`. PNG readback performs no extra conversion.

Vendored build dependencies are targets declared under `external`: `filewatch` and `openpbr` are header-only, and `bc7enc` owns its ISPC static library and generated header. Consumers use `add_deps()`; include paths and encoder build rules stay with their owning targets. Package dependencies remain centrally pinned in `external/xmake.lua` and are consumed with `add_packages()`.

## glTF Support

The Viewer defaults to the included Damaged Helmet GLB. **Open model...** selects a glTF/GLB file, **Reload** validates/reuses the cache, **Reimport** rebuilds it, and **Material spheres** selects the procedural scene (`--materials` on the CLI). Failed loads keep the previous model; successful loads reset the camera and update the window title. `--environment` selects a Radiance HDR image.

The loader supports static triangle primitives, node transforms, indexed and non-indexed geometry, POSITION/NORMAL/TANGENT/TEXCOORD_0/COLOR_0, and generated normals when absent. PNG/JPEG textures have mip chains; base color, emission and specular color use sRGB. Specular weight textures use linear alpha. DDS textures can be selected through `MSFT_texture_dds`; see the [asset format limits](asset_pipeline.md#fbx-and-dds-scope). It handles OPAQUE/MASK, numerical IOR/emissive-strength/clearcoat parameters and specular factors/textures.

Each material texture keeps separate image and sampler indices (`MaterialTexture`); `SceneData::samplers` contains CPU-side `SceneSampler` values, translated to VRI descriptors during upload. Shared images retain independent sampling in all seven material slots, including alpha masks and specular textures. glTF repeat, mirrored repeat, clamp-to-edge, nearest/linear filtering and all mipmap filters are supported. Missing filters select linear filtering with linear mip interpolation; missing samplers also use repeat. Non-mipmapped filters use mip zero while preserving separate minification/magnification filters, according to the current VRI sampler implementation. Image preparation/cache reuse is independent of sampler state. Texture and sampler bindings are fixed when `BuiltinRenderer` is constructed; rebuild it after rebinding them.

Following [glTF material sidedness](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#double-sided), imported materials default to single-sided, including the implicit material. Indexed, meshlet and shadow draws cull back faces unless `doubleSided` is true. Double-sided back faces reverse the complete perturbed normal using rasterized face orientation. Mirrored node transforms preserve front-face winding. Procedural, OBJ and FBX materials retain their existing double-sided default; set `SurfaceMaterial::doubleSided` explicitly when needed.

Normal mapping uses vertex tangents and handedness. Authored glTF tangents are imported directly, transformed with the model's linear matrix and normalized; reflected transforms correct handedness and triangle winding. Only missing tangents are generated from triangle UV derivatives in the geometry jobs. This is deterministic vertex accumulation, not MikkTSpace. The shader orthogonalizes the interpolated tangent against the normal; a degenerate TBN retains the geometric normal rather than normalizing a zero vector. Nonfinite or zero source directions and invalid handedness remain import errors with source, primitive job and vertex context. Procedural `SceneData` callers can use `generateTangents()` explicitly; `GpuScene` generates missing frames when normal-mapped materials need them. Tangent-space XY is scaled by the material's `normalScale`, and BC5/RG8 maps reconstruct Z. Screen derivatives are not used to reconstruct the frame.

Emission samples an sRGB texture into linear RGB, then multiplies `emissiveFactor` and `KHR_materials_emissive_strength`. Following glTF, it is added to HDR scattering independently of the perturbed normal and OpenPBR coat lobes, before exposure and tone mapping. It does not create lights on nearby surfaces, bloom or global illumination. Use the viewer's **Emission** view (`--debug 5`) to inspect this contribution separately from direct lighting and IBL. Different lighting, environment maps and tone mapping can change a scene's appearance even when its material inputs match.

The renderer has only opaque and alpha-mask passes. For static scenes authored with BLEND materials, import logs a warning and maps coverage to an alpha cutoff of 0.5 in both forward and shadow passes. Fractional transparency is not rendered; low-alpha decals can therefore disappear. Source files remain unchanged. This policy is intentionally not glTF alpha-blending conformance.

Skinning, morph targets, sparse accessors, additional UV sets, texture transforms and clearcoat extension textures are unsupported; detected uses are rejected. Animations are ignored with a warning and static node transforms are rendered. Unknown required extensions are rejected; unknown optional extensions may be ignored.

Model viewers accept `--eye X Y Z` and optional `--look-at X Y Z` for reproducible interior views and research captures. An explicit eye selects first-person controls; without a target, it faces the scene center. The Debug Draw example loads Damaged Helmet with the original HDR and draws its actual world-space AABB, grid, axes and sphere wireframes. The shared viewer UI can toggle this overlay for other models. Lines test scene depth without writing it; the shared camera projection includes their bounds.

## Input and Camera Controls

`Window::poll()` publishes `Window::input()` before application updates. Each window owns its `Input` state: held, pressed, released and repeated keys, mouse buttons, logical cursor position/delta and accumulated scroll. Events received during an event wait are retained until the next poll. Press/release edges and deltas last one poll; held state persists. Losing focus releases held keys/buttons and discards motion/scroll to prevent stuck controls.

The core input types contain no GLFW or SDL constants. `platform/glfw` translates native callbacks before ImGui's chained callbacks. `platform/sdl3` routes queued events by window ID to the owning Input and ImGui context, including borrowed detached GUI windows; global events reach each main GUI context once. Applications should use `Window::poll()` and `Window::waitEvents()` so queued SDL events are not discarded outside this dispatch path.

`scene/camera` provides optional Orbit/FPS controllers over the core `RenderCamera` value; these controllers have no ImGui dependency:

- `OrbitCamera`: left-drag orbit, middle/right-drag pan, wheel dolly; radius-relative zoom limits and clip planes. Supply a positive scene radius, positive distance and a valid vertical FOV in radians.
- `FpsCamera`: right-drag look, WASD translation, QE world-Y movement and Shift acceleration. Movement uses elapsed seconds with normalized diagonal speed; angles and vertical FOV are radians.
- Both produce a right-handed `RenderCamera` with depth in `[0,1]`, for raster or ray rendering. Call `camera()` with a nonempty framebuffer extent. An optional minimum far plane includes debug geometry without changing framing.

Controllers read the window input directly. `EditorGui::inputCapture()` supplies only UI ownership flags; it is not the input source. For an ImGuiApp, update the controller in `onPreRender()` after the base call so the current UI capture decision is available:

```cpp
void Viewer::onPreRender()
{
    ImGuiApp::onPreRender();
    m_Camera.update(getWindow().input(), getWindow().size(), getEditorGui().inputCapture()); // Orbit
}
```

FPS uses `update(input, deltaSeconds, capture)` instead. A DesktopApp without UI can update either controller during `onUpdate()` and omit capture. Mouse coordinates and orbit pan extents use logical window units; projection extents use framebuffer pixels. Window scroll survives `ImGui::Render()`, which clears ImGui's own wheel value.

The glTF/Debug Draw viewers share OrbitCamera. Sponza, meshlet Sponza, ray query, ray tracing and the XR desktop rig reuse FpsCamera. XR eye projection and head pose still come from OpenXR. `test-camera` covers event accumulation, focus loss, UI ownership, orbit/pan/FPS behavior and GPU-observed wheel zoom through native GLFW callbacks or the SDL event queue and ImGui forwarding. Initial frames are presented before exact pixel comparisons to allow Wayland surface-size/scale negotiation.

## RenderGraph

`addPass(name, uses, execute)` declares resource accesses and records VRI commands in a callback. Assemble passes with producers before consumers. `exportResource()` and `sideEffect=true` identify observable outputs.

The graph tracks RAW/WAR/WAW dependencies, culls passes without observable effects and inserts barriers before each pass. It rejects reads of uninitialized resources, duplicate resource declarations within one pass, and resource type/usage mismatches. Overwrite dependencies are conservative; resources are not versioned. For debugging an overwritten texture, `captureAfterPass(passName, source, name)` inserts and exports a copy at that pass boundary. It validates the named writer and source transfer usage, then the usual graph dependencies order the copy before later writes. Construct this capture only when requested; it costs an additional texture and GPU copy each frame while present in the graph.

The supported scope is one graphics queue, single-mip/single-layer 2D color or D32 textures, and storage/copy buffers. Depth access can be read, write or read/write. Graph-owned resources live until graph destruction; examples rebuild on resize. Opt-in transient reuse shares exact-descriptor allocations across non-overlapping lifetimes; imported, exported and history resources are excluded. Subresource state tracking and cross-queue scheduling are not supported. A pass handles its own internal synchronization.

Callers own imported resources and guarantee their initial contents and lifetime. Per-frame `bind()` replaces an imported texture only with an equivalent descriptor. Environment precomputes and synchronizes its multi-mip IBL textures separately, then uses them as read-only external resources.

`RenderGraph::snapshot()` returns a read-only copy of the compiled pass/resource plan: active and culled passes, dependencies, declared uses, and resource descriptors. The Research example displays this snapshot alongside a scene-texture preview. It does not edit graph execution. GPU tools see the graph's pass names as Vulkan debug groups and resource/pipeline names as debug labels.

`PassCatalog` holds explicitly installed `PassDefinition`s: stable type names, texture/buffer ports, optional exact texture formats, extent constraints, bounded numeric scalar parameters, required VRI features and instance factories. `RuntimeContext::passes()` owns the desktop catalog; `DesktopApp::getPassCatalog()` exposes it. A direct C++ program can create its own catalog beside its device. No global registration is performed. The catalog checks graph/device ownership and port/parameter requirements before constructing a pass. The existing graph compiler still checks usage, initialization and synchronization.

Pass metadata is currently handwritten: `colorGainDefinition()` declares the `gain` label, default and bounds, then the application installs it with `catalog.add()`. Separately, annotated `RenderSettings`, `CameraSettings`, `LightSettings`, `EnvironmentSettings` and `MaterialParameters` fields produce checked-in Inspector descriptors through the existing libclang code generator. Scene containers, transforms and asset references have explicit editor controls. Neither path uses C++26 static reflection. The name graph definition describes the JSON `.vgraph` data; Falcor's [graph tutorial](https://github.com/NVIDIAGameWorks/Falcor/blob/master/docs/tutorials/03-creating-and-editing-render-graphs.md) uses Render Graph, Render Pass, Graph Editor and graph scripts.

`GraphDefinition` saves and loads UTF-8 JSON `.vgraph` files with `format: "vultra.graph"` and `version: 1`. Instances contain `id`, `type` and `parameters`; edges connect `instance.port` names; `outputs` mark exported resources. Imports are supplied by the caller, such as the built-in renderer's `scene.hdr`. Connections determine construction order, independently of file order. Unknown or duplicate instances/ports, missing or multiply connected inputs, cycles and incompatible resources fail before command recording. Graph definitions compile into the same RenderGraph. Parameters currently support finite numeric scalars. The generated experiment API exposes graph configuration to the optional Python host; graph-owned texture history and opt-in transient reuse are available through the C++ RenderGraph. History handles remain local to the graph. The [research workbench](#research-workbench) edits graph definitions and stages candidate replacements at completed-frame boundaries.

Keep `BuiltPass`/`GraphBuild` instances alive until their graph callbacks are discarded and the final GPU submission completes. Declare instance state before the graph so reverse destruction removes callbacks first. Build a graph definition into a fresh candidate graph and discard that candidate if construction fails. `resourceInfo()` exposes descriptors before compilation and compiled liveness afterward. The [project compute pass](../examples/research/color_gain.cpp) demonstrates this contract without changing the VRI boundary.

`BuiltPass` owns stable numeric parameter storage. Its `GraphPass::addPasses()` parameter span can be captured by value and read when recording each frame. Use `PassCatalog::setParameters()` after previous GPU use completes, before recording; it validates the entire parameter set before changing values and does not rebuild the graph, pass, pipeline or textures. Do not resize `parameterValues` after construction. Numeric parameters must not change resource descriptors or graph topology; those changes require a candidate graph.

## Debug Drawing

Debug bounds, grid, axes and sphere lines load the built-in renderer's scene depth. They use `LessOrEqual` testing without depth writes, so geometry occludes rear lines while coplanar lines remain visible. The graph declares this as `eDepthRead`; resizing rebuilds the depth attachment with the scene. When debug drawing is enabled, orbit and first-person cameras extend their far plane to include the generated line geometry's bounding sphere, with a small margin. The model and lines share that projection; model framing, zoom and the near plane are unchanged. `test-debugdraw` checks occlusion, depth preservation and far clipping through GPU readback.

## Meshlet Rendering

```powershell
xmake run example-scene sponza-mesh-shading
xmake run example-scene sponza-mesh-shading --meshlet-colors
xmake run example-scene sponza-mesh-shading --indexed
xmake run example-scene sponza --meshlets --no-meshlet-culling
```

The dedicated example requests `VriFeature_MeshShader`, builds meshlets from the original Sponza assets and renders them through Slang task/mesh stages. Its UI exposes **Mesh shading**, **Meshlet frustum culling** and **Meshlet colors**. The indexed switch uses the same material/lighting passes for comparison. Other scene viewers can opt in with `--meshlets`; ordinary runs retain the indexed path and do not build meshlet data.

`GpuScene(device, asset, true, workers)` enables the geometry buffers. Set `BuiltinRenderer::settings.meshShading` to select the mesh path; `meshletCulling` and `meshletColors` control culling and visualization. Only the forward geometry path uses meshlets currently; shadow maps retain indexed draws. The sphere test uses VRI's zero-to-one depth clip volume. Every task invocation reaches the group barriers, including partially filled final groups. Source tangents are consumed unchanged after their import transform.

Meshlet colors use the original `dev` hash and submesh-local IDs. This display palette bypasses exposure and tone mapping, with a black background. The HDR attachment stays linear; desktop output encodes the palette once, while float output retains linear values for XR.

`test-meshlets` requires mesh-shader hardware. It checks triangle winding/material preservation, bounds, deterministic parallel construction, partial/fully culled task groups, GPU HDR/normal parity against indexed draws at several camera positions, and display-palette consistency across submeshes, exposure levels and output formats. See [example coverage](example_parity.md) for the remaining differences from `dev`.

## Shaders and Hot Reload

Shader entry points live in `builtin/shaders/passes`. Shared `.slangh` includes use `#pragma once` and are grouped by purpose:

- `lib`: math, color, space, depth, texture coordinates, Hammersley/GGX sampling, PBR/OpenPBR, lighting, IBL and shadows.
- `resources`: frame bindings, GPU materials, OpenPBR LUT sampling, mesh vertex data and the fullscreen vertex entry point.
- `passes`: shadow, forward, skybox, tone mapping, environment precomputation and texture blit.

The built-in renderer supplies `builtin/shaders` and `external` as include roots:

```cpp
#include "lib/math.slangh"
#include "resources/openpbr_luts.slangh"

#include <openpbr/openpbr.h>
```

ShaderPipeline's `includeDirectories` adds search roots. The entry's directory is searched first, then the supplied directories in order. Relative paths become absolute at pipeline creation and are reused on reload. Same-directory experiment shaders can still include `"color.slangh"`. Upstream OpenPBR includes stay unchanged.

FileWatch events are debounced for 150 ms. Each compilation uses a new Slang session. A failed reload preserves the previous pipeline and reports diagnostics; a later successful save replaces it. Source-based built-in pipelines watch `builtin/shaders`, covering sibling `lib` and `resources` directories. Packaged built-ins load cooked programs and do not create watchers. Other pipelines can set a shared `watchDirectory`; the default is the entry directory's tree. Include dependencies and search-precedence parents are also watched, so creating a nearer override triggers reload. Linux recursive watches exclude build and repository metadata directories.

Environment shaders execute when constructing Environment; use **Rebuild IBL** after changing them. Binding or shared-layout changes still require corresponding C++ changes and a rebuild. Vendored OpenPBR is outside the watched tree and requires a rebuild/restart after updates. Vultra's own adapter remains hot-reloadable.

FileWatch is vendored at commit `a59891baf375b73ff28144973a6fafd3fe40aa21`, with its MIT license and attribution preserved in `external/FileWatch`. The [local Linux patch](../external/FileWatch/README.vultra.md) covers rename notifications and shutdown after directory removal. Linux uses one retained watcher per directory, reconciled after debounced changes.

### Shader cooking

`ShaderProgram::compile()` is shared by source hot reload and the CPU-only `vultra-shader` tool. Cooking discovers
all annotated entries in the module, including included fullscreen vertices, and records their reflected VRI
stages. A version-1 `.vshaderc` contains aligned SPIR-V bytecode compiled with the `spirv_1_5` profile, entry
names/stages and an explicit ray-query requirement. Its CBOR payload has an XXH3 checksum; unsupported
versions/targets, malformed entries and corrupt files fail before pipeline creation. This format currently
supports raster, compute and mesh/task shaders on little-endian hosts. DXIL cooking and D3D12 remain unimplemented.

```sh
xmake build -y vultra-shader
./build/linux/x86_64/release/vultra-shader examples/research/shaders/triangle.slang \
    --include builtin/shaders --include examples/common --output build/.tmp/triangle.vshaderc
```

Use the Windows x64 build directory and `.exe` suffix on Windows. Repeat `--include` to add search roots; use
`--ray-query` for a shader that requires ray query. Cooking needs no graphics device or display. A successful cook
atomically replaces the output; compilation or write failure retains the preceding artifact.

Pass a `.vshaderc` path to the ordinary `ShaderPipeline` constructor with the entries needed by that pipeline.
The builder still receives VRI descriptors, whose bytecode/name views belong to the loaded program during the
builder call. If a requested `.slang` source is absent, the constructor loads its `.vshaderc` sibling. A present
source always compiles and watches; an invalid cooked program never switches to source compilation. Cooked
pipelines have no watcher and `poll()` returns false. Explicit `reload()` can replace a cooked file and retains
the preceding GPU pipeline on failure.

`vultra-pack --builtins` cooks all files under `builtin/shaders/passes` before publishing the archive. The archive
contains `.vshaderc` files and OpenPBR license/integration notices, without Slang sources, includes or OpenPBR
headers. The runtime, batch tool, workbench and optional research library embed this archive. Their xmake build
tracks shader/include files, the packer and build configuration, skipping unchanged cooking; packaged shader
changes require rebuilding the archive. Direct source-based examples retain FileWatch hot reload. The research
sample's embedded color-gain source still compiles at runtime. Explicit game and research shader assets in a
project manifest are cooked automatically when packing its VPK. Other project passes and native extensions must
explicitly deliver sources or cooked programs; see [Vultra Shader](shader_system.md#cooking-caching-and-packaging).
Slang remains statically linked for source editing and native project compilation.

## Research Utilities

```powershell
xmake run example-research --frames 60 --dump captures/run01 --capture captures/reference.png
xmake run example-research --path forward --model resources/models/Sponza/Sponza.gltf --frames 60
xmake run example-research --frames 3 --preview-intermediates --dump-intermediates build/.tmp/experiment-stages
xmake run example-research --frames 1 --compare captures/reference.png
xmake run example-research --benchmark build/.tmp/experiment-01 --warmup 60 --samples 120 --revision YOUR_COMMIT
xmake run example-research --fixed-camera --color-gain 0.5 --frames 3 --capture build/.tmp/gain-cpp.png
xmake run example-research --fixed-camera --graph examples/research/color_gain.vgraph --frames 3 --capture build/.tmp/gain-definition.png
```

Research defaults to Damaged Helmet, the built-in renderer's `NaiveDeferred` path, and an HDR environment. The **Path** control or `--path forward` selects `NaiveForward`; the compiled graph observer updates when the path changes. Orbit with left drag, pan with middle/right drag and zoom with the wheel. `--model` and `--environment` select other assets. Both paths expose shadow, lighting, tone-mapping and presentation passes, while deferred also exposes seven G-buffer textures and two geometry passes.

`--color-gain` builds an example-owned HDR compute pass directly in C++; `--graph` builds project passes from a version-1 graph definition. They are mutually exclusive. The sample graph definition takes `scene.hdr`, scales RGB by 0.5 without clipping linear HDR or changing alpha, and returns `gain.color` before tone mapping. Research currently requires exactly one RGBA16F output with the scene extent. `BuiltinRenderer::addScenePasses()` exposes the scene before tone mapping for this purpose; `addPasses()` retains its complete-renderer behavior. `--fixed-camera` disables interactive camera changes for repeatable captures. `test-graph-definition` checks dependency sorting, invalid inputs, save/load and exact float GPU readback parity, including non-multiple-of-eight dispatch extents.

Enable **Preview color attachments** in the observer (or `--preview-intermediates`) to inspect HDR and G-buffer RGB while running. The live view shows raw channels; signed normals and HDR values may clip. **Dump intermediate textures** or `--dump-intermediates DIR` saves one frame of visualized G-buffer, depth, shadow, Skybox-only HDR, final HDR and display PNGs to a fresh directory. The Skybox copy is inserted immediately after its pass and before lighting overwrites HDR; the debug copy runs only for a requested dump frame. Position is scaled by scene bounds, normals map from [-1,1] to [0,1], HDR uses a simple preview curve, and depth uses the observed range. These PNGs are visual aids, not raw numeric or colorimetric references. Capture is disabled during benchmarks.

Frame dumping writes `frame_000000.png`-style images and `timings.csv` into a new directory. Research captures and comparisons use the scene texture without GUI. Other desktop examples capture the displayed image, including GUI where present: the last frame with `--frames`, or the first frame otherwise.

`readback(device, texture, mip=0)` returns top-left-origin RGBA float pixels. It supports RGBA8/BGRA8, RGBA16F, RGBA32F and D32F for a selected mip of a single-layer, single-sample texture. D32F is replicated to RGB with opaque alpha; color alpha is preserved and floating-point values are not clipped. sRGB readback preserves stored values. `savePng()` clamps to [0,1] and quantizes to 8 bits without gamma, exposure or tone mapping.

`compare(reference, test, peak=1)` computes RGB MSE/PSNR without alpha; identical images yield infinite PSNR. SSIM uses luminance weights 0.2126/0.7152/0.0722, an 11x11 Gaussian window with sigma 1.5, K1=0.01 and K2=0.03, and only complete windows. Inputs must match and be at least 11x11. Experiments must choose a consistent color space and peak value. See the [SSIM paper](https://ece.uwaterloo.ca/~z70wang/publications/ssim.pdf) for the method.

The benchmark keeps measured samples in memory and writes `manifest.json`, `frames.csv`, `passes.csv`, and `summary.csv` after the run. Use a fresh output directory and pass the exact source revision with `--revision`; the version-1 manifest also records shader, imported-asset-cache and HDR content hashes, actual framebuffer size, window system, adapter, enabled VRI features, warmup/sample counts, capture path and UI parameters. When a project pass is present, it records the canonical graph definition and whether it was built by C++ or the graph definition frontend. The shader hash covers the built-in and Research Slang shader trees and vendored OpenPBR headers. Shader hot reload and UI edits are disabled during measurement. The run rejects a framebuffer-size change. `--dump`, `--dump-intermediates`, `--preview-intermediates`, `--renderdoc-frame`, and `--frames` cannot be combined with `--benchmark`. `--capture` saves the final scene after measurement. For an isolated interactive layout, use `--layout-file build/.tmp/research.ini`.

`frames.csv` separates update, swapchain acquire, command preparation/recording, submission/wait, post-render, and present phases. `passes.csv` separates each live graph pass's barrier and command recording/execution; `summary.csv` gives mean, median, nearest-rank P95, minimum, and maximum. GPU columns are empty when timestamps are unavailable. GPU time is nested in CPU frame phases, so do not sum it with them. The current single-frame-in-flight loop can spend most of its wall time in swapchain acquire or GPU wait. Pass timing covers up to 64 events with nested scopes; its CPU total is recording time, while GPU timestamps measure execution. PNG encoding is outside measured benchmark frames.

For a GPU capture, start the process through RenderDoc and use `--renderdoc-frame 0` (zero-based) to capture the first frame. This requires RenderDoc to be injected; a missing injection produces an error. Use X11/XWayland if the installed RenderDoc build lacks Wayland surface support. Nsight Graphics can use the Vulkan debug labels and debug Slang information from a debug build. Captures are separate from benchmark sampling because capture overhead invalidates timing comparisons.

## Offline Rendering for AI and QA

`vultra-batch` renders without constructing a Window, Swapchain, desktop RuntimeContext or GUI. It uses a VRI Device, the existing RenderGraph, a selected scene camera or fixed orbit camera and the same built-in renderer and project compute pass as Research. It never connects an application window to the desktop. Prefer this path for rendering QA while the desktop is in use; window/input tests are separate.

```sh
xmake build -y vultra-batch
./build/linux/x86_64/release/vultra-batch --project resources/research.vproject \
    --graph examples/research/color_gain.vgraph --width 640 --height 360 \
    --frames 3 --warmup 1 --revision YOUR_BUILD_ID --output build/.tmp/offline-run
```

Choose exactly one of `--model MODEL` and `--project PROJECT`. The project can be a `.vproject` or an external `.vpk`. `--environment` overrides the project environment map; without a project environment or explicit HDR, Environment uses its existing procedural sky. `--path forward` selects the forward renderer. `--color-gain` constructs the sample pass directly instead of `--graph`. Frame count and dimensions must be positive, and `--revision` and a new `--output` directory are required. Native/Lua/C# project scripts update at the fixed `--time-step` before each render, including warmup. GUI callbacks and hot reload are disabled. `--path reference --seed N` enables progressive reference transport. [Python sessions and experiment descriptions](python_research.md) use the same renderer, generated API and fixed-step lifecycle.

The output directory contains the effective `experiment.vexperiment`, `graph_report.json`, `final.png`, linear `scene_hdr.pfm`, `output_000.pfm`-style marked graph definition outputs and a `report/` directory with the existing manifest and frame/pass/summary CSV files. The first marked graph output must match the requested extent: RGBA16F feeds tone mapping; display-encoded RGBA8 is shown directly. If the first output is RGBA8, `hdr` retains scene HDR and processed HDR should be marked/captured by its own port. The manifest maps each numbered output file back to its graph definition port and records `window_system: "offscreen"`, `present_mode: "none"`, build/input/shader identifiers and camera position. Acquisition and presentation timing columns are zero. Readback and encoding happen after measured frames. `--compare REFERENCE.png` records RGB MSE, PSNR and SSIM; it requires the same dimensions and color space and does not impose a hidden acceptance threshold.

`savePfm()` exports IEEE float RGB without clamping, gamma or tone mapping; PFM does not carry alpha. It retains signed channels and HDR values, unlike the PNG preview. The writer follows the [PFM format's byte-order and row-order contract](https://www.pauldebevec.com/Research/HDR/PFM/). Marked outputs are raw channels, so normals or HDR may require visualization before displaying them as ordinary images.

The executable embeds the built-in shader VPK and the sample compute shader. It extracts them into a temporary directory, shared with the packaged runtime's resource bootstrap, and removes that directory after GPU objects are released. A built `vultra-batch` can be copied beside a project VPK and graph definition and launched without xmake or the repository. System Vulkan/driver and the currently linked Linux platform libraries are still required. Copied Windows batch/VPK startup passes on the development machine; separate clean-machine delivery still needs validation.

The standard-library-only integration regression exercises no-display/no-build-tool runs, C++/graph definition pixel parity, signed HDR output, invalid input and recovery, and a copied standalone executable with VPK input:

```sh
python3 tests/offline_render.py build/linux/x86_64/release/vultra-batch
```

Reference AOVs, graph memory/producer reports, nested timing scopes and actual offscreen RenderDoc captures are described in [reference rendering and diagnostics](reference_renderer.md). Browser streaming is not implemented. Offline PNG/PFM files and reports can be inspected without opening a native rendering window; a future stream frontend should consume this rendering path rather than add a second executor.

## Research Workbench

`vultra-app` is a separate research editor target; pure C++ applications and the player do not depend on its code or node-canvas dependency. It uses the same scene renderer, explicit context catalog and RenderGraph as the research examples. The node canvas follows [Falcor's graph editing workflow](https://github.com/NVIDIAGameWorks/Falcor/blob/master/docs/tutorials/03-creating-and-editing-render-graphs.md), using [imgui-node-editor](https://github.com/thedmd/imgui-node-editor) for navigation and pointer interactions, not another graph executor.

```sh
xmake build -y vultra-app
xmake run vultra-app --project resources/research.vproject --graph examples/research/color_gain.vgraph
```

The **Research document** panel selects a static `.vproject`, render extent and renderer settings. **Apply scene / retry graph** compiles those changes. **Discard draft** restores active values.

The **RenderGraph editor** canvas shows the built-in scene renderer's ports, catalog passes (including shared `vultra.tone_mapping`), and the display output input. The built-in renderer is currently one source node; its internal shadow/G-buffer/lighting passes remain visible in the compiled diagnostics, not individually rewritable graph nodes.

- Drag an output dot to an input dot to connect; dropping onto an already connected input replaces its wire. Texture/buffer and known format mismatches are rejected immediately. The graph compiler validates dimensions, cycles and other graph invariants.
- Drag an RGBA16F or display-encoded RGBA8 output to **Display output** to make it the primary output. HDR is tone mapped; RGBA8 is shown directly. It must match the scene extent. Blue dots are texture ports; amber dots are buffer ports; red inputs need connections. A gold ring marks an exported output.
- Use **Add pass** or right-click the canvas background. Numeric parameters inside a node update the active pass and preview in the same frame while dragging; mouse release does not rebuild the graph. Drag node bodies to reposition them; middle-mouse drag pans and the wheel zooms. **Fit** frames the graph; **Arrange** lays out the graph nodes in their listed order.
- Select nodes/wires and press Delete. Right-click a pass for rename/delete; renaming rewrites its connections and marked outputs. Right-click a wire to disconnect. The scene/display nodes and required display wire are protected.
- Right-click an output to mark/unmark it, preview it, or use it as the display input. Double-click an output dot to mark and preview it. Deferred imports include HDR, depth, position/metallic, normal/roughness, albedo/weight, emission/occlusion, specular, geometric normal/IOR and coat.

Connections, nodes and marked-output edits compile automatically at the next completed-frame boundary after the pointer edit. Unconnected passes, invalid cycles or other invalid edits display an error and retain the last valid image, graph, scene and settings. Preview descriptors are retired only after compilation succeeds. Numeric edits change only parameters of an existing active pass, even when a topology draft is invalid; unknown active pass IDs and invalid values are rejected without changing active state. Saved active graph definitions/workspaces include live parameter values. Scripted projects currently fail; the tool does not execute scripts.

The **Scene inspector** lists the node hierarchy and shared material resources. Click a name to inspect it; arrow buttons expand branches. Scroll the tree to reach additional nodes/resources. Names, local position/rotation/scale and camera/light/environment/material settings update through scene setters while dragging, without replacing geometry or the graph. Material slot selectors assign an authored resource or restore imported values. Model paths are read-only; node creation/removal/reparenting and model changes remain available through scene files and the C++/script APIs, not Inspector controls.

Local rotation is displayed in degrees; camera FOV and light cone fields retain their API's radian units. Transform edits preserve existing shear and signed scale. The editing view handles small affine scales; edited scale magnitudes must be at least `1e-5` on each axis. Invalid field values retain their previous values and report the cause. The current camera checkbox switches between a scene camera and the orbit camera. The current environment checkbox selects that node or restores the project preset. HDR edits/selection stage replacement after the previous frame completes; a failure retains the prior lighting and selection. Replacing a scene cancels pending operations referring to its old object IDs. **Save scene copy** exports the current version-1 `.vscene` independently of workspace saving.

`VULTRA_REFLECT` and `VULTRA_PROPERTY` annotate the current property structs. `xmake codegen` generates typed
`PropertyInfo` accessors, defaults and JSON paths in each type's module. EditorGuiInspector and scene persistence
consume these same descriptors; generated drawing functions call the generic Inspector. The same IR generates
safe C# value records and their C ABI conversion. Normal builds use checked-in output without Python/libclang.
`xmake codegen --check` verifies consistency; `python3 tests/codegen.py` covers declarations, defaults and rejected
types/metadata. The generator rejects unknown or repeated metadata, conflicting JSON paths and invalid enum choices.
Reflected settings use public fields and field initializers. Float defaults must be literals; constructor-defined
or expression defaults fail generation rather than producing guessed managed defaults.

For example, `cameraSettingsType().property("nearPlane").write(&settings, 0.05f)` writes a typed value;
`serializeProperties(cameraSettingsType(), &settings)` writes its JSON. Use
`deserializeProperties(cameraSettingsType(), json, &settings)` to validate all input before mutation and restore
missing fields from C++ defaults. Apply the edited snapshot with `CameraNode::setSettings()` so semantic validation
and scene revisions run. Unknown keys, wrong value types, integer overflow, non-finite values and unknown enum values
fail explicitly. The catalog is context-owned (`RuntimeContext::types()`); a direct C++ program can own one without a
desktop context. Descriptors and returned catalog spans are borrowed; catalog mutation invalidates spans, and a
module must unregister its descriptors before unloading.
DesktopApp exposes the same catalog through `getObjectTypeCatalog()`; Research draws its renderer settings from
that context catalog. Typed generated drawing helpers remain available to direct C++ programs.

Annotation `json=/color/0` maps a component into a serialized array without a second hand-written codec.
`flags=serialize|inspect|bind` is the default; `flags=serialize|bind` keeps a field outside the Inspector while
persisting it. `api=NearPlane` supplies an explicit public managed name when it differs from the JSON key.
Scalar ranges and slider/drag hints remain editor metadata; scene setters own semantic validation. Scene drawer
keys use `Type.field` (for example `MaterialParameters.baseRed`); RenderSettings uses `path`, `exposure`, etc.
Register drawers on the owning EditorGui context; values are borrowed only during drawing. The reload flag is
metadata for the upcoming explicit script-state contract; current hosts do not yet consume it.

**Outputs** selects the final display image or any marked output. Texture previews show raw sampled channels; HDR and signed values can clip. Buffers show their descriptor size rather than an image. The graph panel lists compiled passes, resources and previous-frame CPU/GPU timings. Orbit/pan/zoom use the same controls as Research when GUI input is not captured.

**Save active graph** writes the portable version-1 `.vgraph`. **Save active workspace** writes a version-1 `.vworkspace` (`format: "vultra.research"`) containing a project path relative to that document, a SceneTree snapshot, the graph definition, camera, extent, node positions and all annotated renderer settings (including lights, mesh shading and shadows), plus the seed. Open the workspace to restore the same experiment. These operations save active state, not an invalid draft. A selected scene camera takes precedence over the saved orbit camera. Authored lights disable the sun-preset slider. Saving a workspace captures current node/material state without changing the original project scene. The required `scene` member contains the version-1 scene document; `null` selects the project entry scene. Saving a scene copy writes a standalone `.vscene`. Workspaces still reference project model/texture assets rather than bundling them. `node_positions` is required (use `{}` for automatic initial placement); the file version remains `1` as a deliberate pre-release breaking change. The renderer object now uses generated JSON paths and numeric enum values; `seed` is a separate top-level member. Earlier partial renderer objects are not read through a compatibility path. Node positions are keyed by `scene`, `display` and `pass:<instance ID>` in `node_positions`; view pan/zoom is session state and **Fit** restores a useful view on open. ImGui window layouts remain separate and ignored under `.vultra/vultra-app/`; finite runs disable layout persistence.

**Export PNG + linear PFM** requires a new directory. It saves the final image and every marked texture, `graph.vgraph`, `workspace.vworkspace` and a version-1 capture manifest mapping numbered files to ports. PNG quantizes raw channels; PFM retains linear signed/HDR values without alpha. Buffer image export fails explicitly. `--frames N --export DIR` performs the same capture after a finite run.

AI/QA can render the **same panel and widgets** offscreen:

```sh
./build/linux/x86_64/release/vultra-app --offline --frames 3 --export build/.tmp/workbench-run
./build/linux/x86_64/release/vultra-app --offline --frames 3 \
    --workspace build/.tmp/workbench-run/workspace.vworkspace --export build/.tmp/workbench-reopened
```

This path constructs no Window or Swapchain and does not initialize a GLFW/SDL platform backend. It uses a 1600x1000 GUI target with a fixed 1/60-second GUI time step and exports `workbench.png` in addition to scene/port images. Timings shown in the UI may vary; the scene, graph and camera outputs are reproducible. `EditorGui(Device&, format, config)` supplies this explicit offscreen context (`multiViewport = false`), followed by `begin(extent, deltaSeconds)`, widget construction, `upload`, command recording and GPU completion. Native window contexts keep the existing `begin()` lifecycle.

`test-graph-editor` injects mouse events into the offscreen GUI to verify wiring, format rejection, rewiring, display selection, navigation, preview, node deletion/recreation, rename, add and saved node positions. It holds and moves a parameter slider, reads changed scene pixels before mouse release, and verifies graph/pass/texture reuse. `test-research-workspace` covers scene/workspace image round trips, invalid snapshot recovery, shared-resource reuse and graph/project/parameter failure recovery. `test-scene-inspector` checks tree/resource selection, held-drag GPU output, generated/custom drawers, small/signed scales and shear, rejected values, HDR failure/recovery, stale pending operations and saved-image parity. `test-gui-offscreen` covers default scalar/vector/text controls, checkbox and dropdown interaction plus GPU readback. These tests do not send input to the desktop. Windows GLFW native startup and the offscreen workbench regressions pass; manual desktop UI interaction and current SDL3 acceptance remain separate checks.

The standalone integration regression checks workbench/reopened/batch pixel parity, marked depth/normal output, the real UI screenshot and invalid CLI/graph definition inputs with no display connection or build tools:

```sh
python3 tests/research_workbench.py build/linux/x86_64/release/vultra-app
```

## OpenXR

`OpenXRSystem -> Device(creationHooks) -> OpenXRSession` uses `XR_KHR_vulkan_enable2` for instance/device creation and GPU selection. Each eye has its own swapchain. Views use predicted display time pose/FOV, with session state, invalid tracking, `shouldRender` and acquire/wait/release handled explicitly.

The desktop mirror shows both eyes side by side with preserved aspect ratios. Mirror reads complete before releasing acquired eye images, which return to color-attachment layout. When no valid views are available, the desktop UI can still update. In this example, `--frames` counts application loop iterations; the log reports actual eye-rendered frames separately. `--capture` saves the desktop mirror.

sRGB RGBA/BGRA formats are preferred over UNORM. Following the [OpenXR linear-composition contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrSwapchain.html), eye shaders output linear color: sRGB attachments encode in hardware, while UNORM attachments store linear values. The sample triangle interpolates the same linear RGB vertex colors as the desktop examples, with no shader encoding for XR. Mirror sampling returns linear values, which are encoded once for the desktop UNORM output.

Controllers, hand tracking, depth composition and in-headset ImGui are not implemented. Desktop ImGui is available. Four RGBA/BGRA sRGB/UNORM formats and mirror color parity are covered by offscreen tests; they do not validate the real headset/runtime path. Monado simulated-device eye and mirror execution is also covered; see [runtime setup and limitations](monado.md). No physical headset has been validated.

## Editor Setup

Open Vultra as the VS Code workspace root and run one of these scripts with xmake on PATH:

```powershell
.\scripts\setup_vscode.ps1
```

```sh
sh scripts/setup_vscode.sh
```

Both call `scripts/setup_vscode.lua`. They update only `clangd.arguments`, `slang.additionalSearchPaths` and `slang.searchInAllWorkspaceDirectories`. clangd uses `.vscode/compile_commands.json`; Slang searches `builtin/shaders`, `external` and `examples/common` with whole-workspace scanning disabled. Other settings, including personal colors, are preserved. `.vscode/` is ignored; only the setup scripts are shared.

`${workspaceFolder}` is expanded by the Slang VS Code extension. JSON `\/` and `/` mean the same slash; the generator emits plain `/` for readability.

JSONC comments and trailing commas are accepted. Updates write formatted JSON and preserve the original commented file once as `.vscode/settings.json.bak`; existing backups are not replaced. Matching settings cause no write, and invalid input leaves the original file intact. Scripts may run from another directory or accept the project directory as their single argument. Runtime include roots are not automatically sent to the language server; add experimental roots to the setup script too. Reload the editor window if diagnostics are stale.

## Shell Completion

Xmake's installed completion scripts provide project target names and command options. Load the matching Vultra helper in the current interactive shell, with xmake on PATH:

```zsh
source scripts/setup_xmake_completion.zsh
```

```powershell
. ./scripts/setup_xmake_completion.ps1
```

The zsh helper initializes `compinit` if necessary and explicitly registers `xmake` and `xrepo`. With Oh My Zsh, source it after `oh-my-zsh.sh` and before syntax-highlighting plugins. The PowerShell helper uses xmake's native argument completer; PowerShell 7 is recommended. Both query the active xmake installation directory instead of assuming a package-manager or Windows install path. They load the existing upstream scripts without copying or replacing their completion logic. Repeated loading refreshes the registrations without adding startup-file entries.

For persistent loading, add one source line with the helper's **absolute path** to `.zshrc` (under `$ZDOTDIR` if configured), or to the PowerShell profile displayed by `$PROFILE.CurrentUserAllHosts`. For example:

```zsh
source "/path/to/libvultra/scripts/setup_xmake_completion.zsh"
```

```powershell
. 'C:/path/to/libvultra/scripts/setup_xmake_completion.ps1'
```

Create the PowerShell profile directory/file if absent, preserving existing content. These helpers do not edit profiles, install packages or change execution policy. New terminal sessions load the configured profile; an existing terminal needs the source command again. Update the profile's source path if the checkout moves. Run the zsh helper with `source`, not as a child `zsh` process, so registration remains in the current shell.

Inside this project, `xmake run example-sc` followed by Tab completes `example-scene`. Category mode names are listed by `xmake run example-scene --help`; pass a mode before its own options. The shell scripts provide xmake target completion, while the example launcher validates mode names. `xrepo` completion is registered as well.

## Verification

The test targets cover independent window ownership/input and native GUI readback, executable/memory queries and atomic file publication with failure/recovery, image metrics/PNG, CLI validation/logging, RenderGraph/GPU behavior, FileWatch failure and recovery, application lifecycle, GUI themes/viewports/layout persistence, asset cache invalidation/recovery and compressed uploads, IBL, OpenPBR reference evaluation and shadow filtering. Run them with `xmake test -v`; they are not part of default `xmake run`.

`test-gltf-materials` checks GPU pixels for sampler addressing/filtering/mips, independent slots sharing an image, cold/warm imports, front/back faces, mirrored transforms, authored/generated tangent frames and alpha-mask shadows. It exercises both indexed and meshlet drawing and requires mesh-shader support, like `test-meshlets`.

Shader failure/recovery tests intentionally compile invalid input and label the expected error. A successful process exit alone does not establish correct rendering; inspect GPU diagnostics and relevant readback results. The OpenPBR test compares 49 Slang/C++ cases, but does not certify full material or glTF conformance. Hardware-independent checks cannot replace headset validation.

As of 2026-10-05, the Windows x64 GLFW/release/Vulkan all-target build and 32-test suite pass on an RTX 4080 SUPER. Standalone batch/VPK, workbench persistence and Python/CLI regressions also pass, including exact same-device HDR readbacks and reference AOVs. A 30-frame native workbench run exports the same PNG/PFM pixels as its offscreen baseline. These checks cover the development machine, not manual editor interaction or a separate clean installation. Current SDL3, D3D12, physical XR and RenderDoc/Nsight acceptance remain separate.

On Linux, run the complete suite with `VULTRA_WINDOW_SYSTEM=x11 xmake test -v`. Its detached-viewport and iconify tests require a window manager that honors application resize and iconify requests. An X11 stacking window manager such as Openbox provides that test environment; an isolated Xvfb display with Openbox can also be used. Choose an unused display number separate from the active desktop, and limit its `DISPLAY` environment variable to the test processes. Hyprland's tiling/minimize policy can invalidate exact-size and minimize assertions even though XWayland rendering works. Do not suppress these assertions or treat a bare Xvfb server without a running window manager as equivalent. Virtual-display results validate the code paths and GPU readbacks, not physical display or headset behavior.

For native Wayland, run `VULTRA_WINDOW_SYSTEM=wayland xmake test -v 'test-window/*' 'test-camera/*'` and finite-frame examples with captures. `test-window` requires docking and correct GPU pixels while checking that unsupported detached viewports stay disabled. The full X11 suite retains its viewport/minimize assertions; running it under Wayland is not an equivalent acceptance environment. Use an isolated run directory for example layouts when comparing backends.

After configuring and building, xmake updates `.vscode/compile_commands.json` for the selected backend. Run the editor setup script and open a project `.cpp` file to check clangd diagnostics. Use the same database for standalone clang-tidy:

```sh
clang-tidy -p .vscode source/platform/src/os/window.cpp
```

Regenerate the database after changing backends; a GLFW database cannot describe SDL3 platform sources. For code changes, use `.clang-format` and `.clang-tidy` and finite-frame runs of affected examples. Temporary verification data belongs under `build/.tmp/`. Keep personal layouts and experiment outputs out of source control.

Use a complete LLVM installation for clang-tidy, including its matching `lib/clang/<version>/include` resource headers. A tidy-only installation can incorrectly pick up MSVC's SIMD headers and report narrowing errors inside `stb_image_resize2`. Run examples through `xmake run` for consistent working directories and prepared assets. Slang and the Linux OpenXR loader are linked statically.

## Game shaders and native research shaders

See [Vultra Shader](shader_system.md) for the two source paths, material properties, standard Surface passes, explicit VRI Pass contracts, cooking, packaging and editor services. The game example depends on build-time shader cooking; research code remains able to use native Slang and VRI directly. Regenerating the private ANTLR parser requires Java, but normal builds do not.
