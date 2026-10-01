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

Annotated declarations in `editor_gui_frame.hpp`, `scene_api.hpp` and `builtin_renderer.hpp` are parsed by `scripts/codegen.py` using the current `.vscode/compile_commands.json`. The generator writes a deterministic IR, `RenderSettings` Inspector descriptors, C UI/scene function tables and matching C# ABI layouts. Run `xmake codegen` after changing an annotation, then `xmake codegen --check`; ordinary builds do not load Python or libclang. Unsupported annotated signatures fail generation.

The GUI layer exposes `EditorGuiFrame` widgets, `EditorGuiWindow` and `EditorGuiLayout` regions, and `EditorGuiInspector` property rows. An `EditorGui` context owns the property-drawer table: register by property ID with `setPropertyDrawer`, keep callback `userData` alive until removal, and draw only during the active GUI frame. Research customizes its generated render-path field through this table; the generated descriptors also work with the default drawer.

The C tables have a version and `struct_size` (ABI version 3). Strings use UTF-8 pointer/length pairs. `VultraUiFrame` is valid only during `on_gui`; `VultraSceneFrame` is valid only during `update`. The scene table exposes root/child/name queries and local node translation reads/writes through process-local `ObjectId` values. An invalid or foreign ID, or a non-finite translation, returns `VULTRA_STATUS_INVALID_ARGUMENT`; a frame used after its callback returns `VULTRA_STATUS_INVALID_FRAME`. Returned name bytes are borrowed and must be copied if retained. A native plugin includes `<vultra/api/native_plugin.h>` and exports `vultra_plugin_init`; it does not link a second copy of `vultra`. Rebuild native plugins for ABI version 3. To try the C-only example:

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

The version-1 project manifest has a separate `extensions` list of native library paths. The runtime starts these modules before the `scripts` list; an extension may update the scene and draw through EditorGui without owning a scene node. A project manifest script entry names `language` and `path`. C# entries additionally require `type` (the fully qualified class name) and `node` (a persistent scene node UUID). A C++ script entry sets `type` to a class registered by its native module, and may set `node`; the C-only plugin needs no class name. Lua may also set `node`. Native and Lua entries without a node attach to the root. One native library can register several C++ script classes with `VULTRA_NATIVE_MODULE(VULTRA_NATIVE_CLASS(First), VULTRA_NATIVE_CLASS(Second))`; the one-class shorthand is `VULTRA_NATIVE_SCRIPT(First)`. Declaring that same C++ library in `extensions` makes it the class provider for matching native script entries, so the host loads it once and creates separate node instances. On hot reload, the host validates replacement classes and instances before stopping the old module; missing classes keep the old code active. The extension ABI also supplies scene/UI callbacks, but resource/server/editor type registration is not implemented. These are pre-release format-1 contracts, so changes deliberately break older local packages. Native and C# modules are loaded from extracted files; Lua source is loaded into a per-module VM. The source-backed ABI generator currently emits the C UI/scene tables and C# low-level layouts. Native/Lua high-level adapters and the safe managed `Node`/`Node3D` API are still handwritten. `VultraBindings.g.cs` contains internal ABI layouts, not a public C# engine API; generating typed public classes and properties from the IR remains open work.

For live reload, run without `--frames`, edit `examples/scripting/lua/scene_probe.lua`, rebuild `example-native-cpp-plugin` with xmake, or run `dotnet build examples/scripting/csharp/VultraScript.csproj -c Release -o build/.tmp/scripting-managed` in another terminal. The host checks the source files during `onUpdate`. Native modules load from unique copies so their original DLL/SO can be rebuilt. C# module code is loaded from bytes into a collectible `AssemblyLoadContext`; the process loads one compatible CoreCLR runtime. The replacement loads before the old module stops; a load or initialization failure leaves the old callbacks running. A later exception inside the new process callback is a runtime error, not a rollback. Reload resets script-local state; scene node translations persist because the scene belongs to the host. The GUI button retries failed replacements on the next update, even if their files have not changed again. Windows live-rebuild behavior remains unverified.

The managed UI-phase control can be checked without a GPU using `dotnet run --project tests/managed_control/ControlTest.csproj -c Release`; it checks that a button click changes the scene on the next update and that prewarmed process/UI callbacks do not allocate managed memory. The `ScriptHost` entry point is in `<vultra/scripting/script_host.hpp>` and requires `add_deps("vultra-scripting")`. It is optional for a pure C++ program. The generated C# layouts live in `source/api/csharp/VultraBindings.g.cs`; normal C++ builds do not run codegen or require the .NET SDK. Python is a future optional adapter, with batch access needed for large simulation data. The [architecture guide](architecture.md) records the ownership boundaries and references, including [Infernux](https://infernux-engine.com/) for Python-oriented simulation design.

## Project Package and Runtime

A `.vproject` manifest assigns persistent `AssetId` values to project-relative files and names a `.vscene` entry scene. A scene node has its own persistent node ID and a separate runtime `ObjectId`; neither is a `RenderingServer` RID. Renaming an asset path preserves its asset ID. The initial `SceneTree` format and project manifest are versioned JSON and reject missing assets, duplicate identities and paths that leave the project root. The runtime and Research `--project` import every static mesh node, apply parent and local transforms, and combine them into one GPU scene. Repeated references to an asset share its imported materials and textures, though their transformed geometry is duplicated. This is a startup bake; changing node transforms after upload does not update the GPU scene. `example-scripting` uses a small direct colored-mesh renderer that reads node translations every frame, so its scripted movement is visible without changing the packaged runtime's static-mesh path.

`vultra-pack` writes an uncompressed VPK with the manifest, scene, declared assets and script modules. A C# module also packs the sibling safe API `Vultra.Scripting.dll`, internal `Vultra.ManagedHost.dll`, its required `.runtimeconfig.json`, and any `.deps.json` files. Declare other managed dependency DLLs as project assets. The project VPK contains no engine shaders. The `vultra-runtime` binary embeds a separate checked VPK of built-in Slang shader sources and OpenPBR includes at build time. A project VPK can stay external or be appended to a copy of the runtime. The appended archive has a versioned, checksummed footer; the runtime reads it from its own executable when no package path is supplied. An explicit package path selects an external VPK. Both forms extract the project and built-ins to a temporary directory for the current file-based importer and shader compiler, then remove it after shutdown. VPK entries have XXH3 checksums and reject corrupt data and traversal paths.

Build the tools once, then use the standalone packer directly. Exporting and running do not invoke xmake:

```sh
xmake build vultra-pack vultra-runtime
./build/linux/x86_64/release/vultra-pack resources/research.vproject build/.tmp/research.vpk
VULTRA_WINDOW_SYSTEM=wayland ./build/linux/x86_64/release/vultra-runtime build/.tmp/research.vpk --frames 60
./build/linux/x86_64/release/vultra-pack --embed ./build/linux/x86_64/release/vultra-runtime build/.tmp/research.vpk build/.tmp/research-game
VULTRA_WINDOW_SYSTEM=wayland ./build/.tmp/research-game --frames 60 --capture build/.tmp/research-game.png
```

An editor can call `VpkArchive::packProject()` and `VpkArchive::embedProject()` directly; these operations require neither xmake nor a build tool on the target machine. The output path must be new, the source runtime must not already contain a project, and Unix execute permissions are preserved. On Windows, use a `.exe` output path; the Windows build remains unverified. The footer must remain at the end of the executable, so code signing is not yet supported for this export mode.

The runtime includes ImGui today because native C ABI plugins may draw through its generated UI bridge. The built-in debugger is opt-in: launch either package form with `--debug-ui` to show render settings, previous-frame CPU/GPU time and the compiled RenderGraph pass list; press F1 to hide or restore it. Project plugin GUI callbacks still run when the built-in debugger is hidden. This is a visibility option, not yet a build variant that removes ImGui from the executable. The current runtime debugger is smaller than the interactive Research example and does not show intermediate attachment previews.

Game-facing UI uses [RmlUi 6.2](https://github.com/mikke89/RmlUi/tree/6.2) (MIT) through the optional `vultra-vgui` static target. `VGui` owns a RmlUi context and converts compiled geometry, premultiplied RGBA textures, scissor regions and input into VRI operations. The runtime links it statically; direct C++ rendering projects that only depend on `vultra` do not link RmlUi. The research project declares `ui_document` and `ui_font` asset IDs for `ui/hud.rml` and the Lato Latin font; its RCSS and font license are also listed as project assets. The packer includes those assets in external or embedded VPKs. At startup the current runtime extracts the VPK, then RmlUi loads the document and referenced CSS from the extracted project directory. A styled checkbox toggles the skybox and updates a status label. The sample font is distributed under the SIL Open Font License in `resources/ui/LICENSE.txt`.

`EditorGui*` names identify the existing ImGui editor/debug layer. It remains available to research examples and native UI plugins, with `--debug-ui` opt-in for the runtime's built-in panel. `VGui` is the authored in-game layer. It automatically applies a neutral style with an embedded PNG sprite atlas when loading a document; document RCSS can override it. Xmake embeds the atlas into `vultra-vgui`, so the runtime needs no UI texture sidecar. The style covers panels, rows, buttons, checkboxes, radio buttons, text/password fields, text areas, dropdowns, range sliders, progress bars and scrollbars. `example-ui` shows these controls beside raw ImGui and EditorGui; its Kenney UI Pack skin demonstrates texture-backed button states, icons, toggles, radio buttons and a slider alongside text, dropdown and progress controls. The Kenney PNGs are example-only CC0 assets; the packaged runtime uses the built-in skin for its settings panel. RmlUi creates internal parts for sliders and dropdowns, which the built-in style also covers; see the [RmlUi control style guide](https://mikke89.github.io/RmlUiDoc/pages/style_guide.html). A font still needs to be loaded by the application. `bindChange()` reports control changes; `value()`/`setValue()` handle text, sliders and dropdowns, while `isChecked()`/`setChecked()` handle toggles. `setText()` inserts literal text and escapes RML markup. Call these methods on the owning VGui context during its active application lifetime. The renderer supports 2D geometry, PNG image sources, generated font textures and rectangular scissoring; RmlUi's optional transform, mask, layer, shadow and filter hooks are not implemented. Project skins can use `<img src>` and RCSS `decorator: image(...)` with PNG files; declare those files as project assets before packing a VPK. See RmlUi's [image](https://mikke89.github.io/RmlUiDoc/pages/rml/images.html), [sprite sheet](https://mikke89.github.io/RmlUiDoc/pages/rcss/sprite_sheets.html) and [image decorator](https://mikke89.github.io/RmlUiDoc/pages/rcss/decorators/image.html) references. Both UI layers use the same VRI device and frame timing. VGui documents are currently authored as RML/RCSS and loaded from files; a stream-backed VPK file interface is future work.

This runtime slice renders static mesh instances from the entry scene. The optional `vultra-scripting` static library hosts declared native, Lua 5.4 and C# modules. Native and C# modules are project payloads in the VPK and are extracted to real files for loading; Lua 5.4 and engine dependencies are linked into the executable. A C# project needs the installed .NET 10 runtime; `hostfxr` is resolved when its first module starts. Build scripts with the .NET 10 SDK. The safe `Vultra.Scripting` API assembly holds internal generated ABI layouts; `Vultra.ManagedHost` loads user `Node` subclasses into collectible assembly contexts. User scripts compile without unsafe code. This reload model does not use Native AOT, whose shared libraries cannot be unloaded. The current importer extracts the entire project before startup, so package size and startup time grow with source assets; stream-backed imports remain future work. Before the first public release, all serialized and file-format versions stay at `1`. Format changes deliberately break old files and caches; there are no migration or compatibility readers.

A Linux release executable still relies on operating-system facilities such as the ELF loader, libc, Vulkan loader/GPU driver and the chosen X11/Wayland stack. “Single executable” means no engine or third-party `.so` sidecars are shipped alongside `vultra-runtime`; it does not mean a fully static Linux ELF. Inspect `readelf -d` and `ldd` before distributing a build, and verify on a clean target system.

## Desktop Platforms

Windows x64 and Linux x86_64 share the VRI Vulkan renderer and application lifecycle. `libvultra_window_backend` selects `glfw` (default, 3.4) or `sdl3` (3.4.0) at build time. Both Linux backends include X11 and native Wayland. Set `VULTRA_WINDOW_SYSTEM=x11` or `VULTRA_WINDOW_SYSTEM=wayland` before launch to select explicitly; an unset or empty value leaves selection to the backend, while an invalid value fails initialization. Other Linux architectures and headless desktop execution are outside this build's scope. GPU tests still require their documented Vulkan features.

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

The supported scope is one graphics queue, single-mip/single-layer 2D color or D32 textures, and storage/copy buffers. Depth access can be read, write or read/write. Graph-owned resources live until graph destruction; examples rebuild on resize. There is no transient aliasing, subresource state tracking or cross-queue scheduling. A pass handles its own internal synchronization.

Callers own imported resources and guarantee their initial contents and lifetime. Per-frame `bind()` replaces an imported texture only with an equivalent descriptor. Environment precomputes and synchronizes its multi-mip IBL textures separately, then uses them as read-only external resources.

`RenderGraph::snapshot()` returns a read-only copy of the compiled pass/resource plan: active and culled passes, dependencies, declared uses, and resource descriptors. The Research example displays this snapshot alongside a scene-texture preview. It does not edit graph execution. GPU tools see the graph's pass names as Vulkan debug groups and resource/pipeline names as debug labels.

Data-driven and Python/Lua construction are not implemented. A future frontend should select passes, parameters and connections while reusing the existing C++ graph compiler and executor.

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

FileWatch events are debounced for 150 ms. Each compilation uses a new Slang session. A failed reload preserves the previous pipeline and reports diagnostics; a later successful save replaces it. Built-in runtime pipelines watch `builtin/shaders`, covering sibling `lib` and `resources` directories. Other pipelines can set a shared `watchDirectory`; the default is the entry directory's tree.

Environment shaders execute when constructing Environment; use **Rebuild IBL** after changing them. Binding or shared-layout changes still require corresponding C++ changes and a rebuild. Vendored OpenPBR is outside the watched tree and requires a rebuild/restart after updates. Vultra's own adapter remains hot-reloadable.

FileWatch is vendored at commit `a59891baf375b73ff28144973a6fafd3fe40aa21`, with its MIT license and attribution preserved in `external/FileWatch`. The [local Linux patch](../external/FileWatch/README.vultra.md) covers rename notifications and shutdown after directory removal. Linux uses one retained watcher per directory, reconciled after debounced changes.

## Research Utilities

```powershell
xmake run example-research --frames 60 --dump captures/run01 --capture captures/reference.png
xmake run example-research --path forward --model resources/models/Sponza/Sponza.gltf --frames 60
xmake run example-research --frames 3 --preview-intermediates --dump-intermediates build/.tmp/experiment-stages
xmake run example-research --frames 1 --compare captures/reference.png
xmake run example-research --benchmark build/.tmp/experiment-01 --warmup 60 --samples 120 --revision YOUR_COMMIT
```

Research defaults to Damaged Helmet, the built-in renderer's `NaiveDeferred` path, and an HDR environment. The **Path** control or `--path forward` selects `NaiveForward`; the compiled graph observer updates when the path changes. Orbit with left drag, pan with middle/right drag and zoom with the wheel. `--model` and `--environment` select other assets. Both paths expose shadow, lighting, tone-mapping and presentation passes, while deferred also exposes seven G-buffer textures and two geometry passes.

Enable **Preview color attachments** in the observer (or `--preview-intermediates`) to inspect HDR and G-buffer RGB while running. The live view shows raw channels; signed normals and HDR values may clip. **Dump intermediate textures** or `--dump-intermediates DIR` saves one frame of visualized G-buffer, depth, shadow, Skybox-only HDR, final HDR and display PNGs to a fresh directory. The Skybox copy is inserted immediately after its pass and before lighting overwrites HDR; the debug copy runs only for a requested dump frame. Position is scaled by scene bounds, normals map from [-1,1] to [0,1], HDR uses a simple preview curve, and depth uses the observed range. These PNGs are visual aids, not raw numeric or colorimetric references. Capture is disabled during benchmarks.

Frame dumping writes `frame_000000.png`-style images and `timings.csv` into a new directory. Research captures and comparisons use the scene texture without GUI. Other desktop examples capture the displayed image, including GUI where present: the last frame with `--frames`, or the first frame otherwise.

`readback(device, texture, mip=0)` returns top-left-origin RGBA float pixels. It supports RGBA8/BGRA8, RGBA16F, RGBA32F and D32F for a selected mip of a single-layer, single-sample texture. D32F is replicated to RGB with opaque alpha; color alpha is preserved and floating-point values are not clipped. sRGB readback preserves stored values. `savePng()` clamps to [0,1] and quantizes to 8 bits without gamma, exposure or tone mapping.

`compare(reference, test, peak=1)` computes RGB MSE/PSNR without alpha; identical images yield infinite PSNR. SSIM uses luminance weights 0.2126/0.7152/0.0722, an 11x11 Gaussian window with sigma 1.5, K1=0.01 and K2=0.03, and only complete windows. Inputs must match and be at least 11x11. Experiments must choose a consistent color space and peak value. See the [SSIM paper](https://ece.uwaterloo.ca/~z70wang/publications/ssim.pdf) for the method.

The benchmark keeps measured samples in memory and writes `manifest.json`, `frames.csv`, `passes.csv`, and `summary.csv` after the run. Use a fresh output directory and pass the exact source revision with `--revision`; the manifest also records shader, imported-asset-cache and HDR content hashes, actual framebuffer size, window system, adapter, warmup/sample counts, and UI parameters. The shader hash covers the built-in Slang shader tree and vendored OpenPBR headers. Shader hot reload and UI edits are disabled during measurement. The run rejects a framebuffer-size change. `--dump`, `--dump-intermediates`, `--preview-intermediates`, `--renderdoc-frame`, and `--frames` cannot be combined with `--benchmark`. For an isolated interactive layout, use `--layout-file build/.tmp/research.ini`.

`frames.csv` separates update, swapchain acquire, command preparation/recording, submission/wait, post-render, and present phases. `passes.csv` separates each live graph pass's barrier and command recording/execution; `summary.csv` gives mean, median, nearest-rank P95, minimum, and maximum. GPU columns are empty when timestamps are unavailable. GPU time is nested in CPU frame phases, so do not sum it with them. The current single-frame-in-flight loop can spend most of its wall time in swapchain acquire or GPU wait. Pass timing covers up to 64 non-nested passes; its CPU total is recording time, while GPU timestamps measure execution. PNG encoding is outside measured benchmark frames.

For a GPU capture, start the process through RenderDoc and use `--renderdoc-frame 0` (zero-based) to capture the first frame. This requires RenderDoc to be injected; a missing injection produces an error. Use X11/XWayland if the installed RenderDoc build lacks Wayland surface support. Nsight Graphics can use the Vulkan debug labels and debug Slang information from a debug build. Captures are separate from benchmark sampling because capture overhead invalidates timing comparisons.

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

On Linux, run the complete suite with `VULTRA_WINDOW_SYSTEM=x11 xmake test -v`. Its detached-viewport and iconify tests require a window manager that honors application resize and iconify requests. An X11 stacking window manager such as Openbox provides that test environment; an isolated Xvfb display with Openbox can also be used. Choose an unused display number separate from the active desktop, and limit its `DISPLAY` environment variable to the test processes. Hyprland's tiling/minimize policy can invalidate exact-size and minimize assertions even though XWayland rendering works. Do not suppress these assertions or treat a bare Xvfb server without a running window manager as equivalent. Virtual-display results validate the code paths and GPU readbacks, not physical display or headset behavior.

For native Wayland, run `VULTRA_WINDOW_SYSTEM=wayland xmake test -v 'test-window/*' 'test-camera/*'` and finite-frame examples with captures. `test-window` requires docking and correct GPU pixels while checking that unsupported detached viewports stay disabled. The full X11 suite retains its viewport/minimize assertions; running it under Wayland is not an equivalent acceptance environment. Use an isolated run directory for example layouts when comparing backends.

After configuring and building, xmake updates `.vscode/compile_commands.json` for the selected backend. Run the editor setup script and open a project `.cpp` file to check clangd diagnostics. Use the same database for standalone clang-tidy:

```sh
clang-tidy -p .vscode source/platform/src/os/window.cpp
```

Regenerate the database after changing backends; a GLFW database cannot describe SDL3 platform sources. For code changes, use `.clang-format` and `.clang-tidy` and finite-frame runs of affected examples. Temporary verification data belongs under `build/.tmp/`. Keep personal layouts and experiment outputs out of source control.

Use a complete LLVM installation for clang-tidy, including its matching `lib/clang/<version>/include` resource headers. A tidy-only installation can incorrectly pick up MSVC's SIMD headers and report narrowing errors inside `stb_image_resize2`. Run examples through `xmake run` for consistent working directories and prepared assets. Slang and the Linux OpenXR loader are linked statically.
