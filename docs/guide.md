# Development Guide

This guide describes Vultra's current contracts and limitations. For setup and example commands, see the [README](../README.md). Development constraints are in [AGENTS.md](../AGENTS.md).

## Source Layout

Public headers live under `source/include/vultra`; implementations under `source/src` use the same responsibility-based hierarchy:

```text
core/
  base/           Logging and command-line options
  os/             Windows, file-dialog interface and executable path
  rhi/            VRI device/frame, resources, swapchain and shader pipeline
  image/          PNG, PSNR and SSIM
  input/          Per-window key, mouse and scroll state with framework codes
  profiling/      CPU/GPU profiler
function/
  asset/          Source import, dependencies, derived cache and texture preparation
  app/            BaseApp, DesktopApp and ImGuiApp
  camera/         RenderCamera, OrbitCamera and FpsCamera
  openxr/         XR system, session and eye images
  rendergraph/    Vultra RenderGraph
  renderer/       Scene, glTF/OBJ/FBX loaders, uploads, GUI and texture blit
    builtin/      Renderer passes, environment, shadows and OpenPBR LUTs
  research/       Readback, capture and frame dumps
platform/
  glfw/           GLFW events mapped to framework input
  windows/        Native window, process and file-dialog implementations
```

`vultra` contains the infrastructure and research utilities. `vultra-renderer` adds scene loading and built-in rendering and depends on `vultra`. Experiments that only use VRI do not need the renderer library.

Useful starting points are [DesktopApp's loop](../source/src/function/app/desktop_app.cpp), [the research example](../examples/research/main.cpp), [the glTF Viewer](../examples/gltf_viewer/main.cpp) and [the built-in renderer](../source/src/function/renderer/builtin/builtin_renderer.cpp).

## Application Lifecycle

BaseApp supplies `run()`, `close()`, `frameCount()` and update callbacks. DesktopApp owns Window, Device, Swapchain and Frame in dependency order. Constructors and RAII handle initialization and cleanup; base constructors and destructors do not call derived virtual functions.

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

ImGuiApp calls `Gui::begin()`, `onImGui()` and `Gui::upload()` from `onPreRender()`. An override calls the base first to finalize the UI capture decision, then reads `Window::input()` for camera controls. ImGui's transient input may already be cleared by `Gui::upload()`. The application chooses where to draw the overlay: call `drawGui()` outside a rendering pass, or use `getGui()` in an explicit RenderGraph pass.

The XR examples share `examples/common/xr_sample.*`, derived from BaseApp, with separate update, GUI and eye-render phases: the runtime participates in device creation and controls XR frame timing.

## ImGui and Layouts

Vultra uses the ImGui docking branch. Docking and multiple native viewports are enabled by default. `Gui::begin()` creates a transparent root dockspace; `dockspaceId()` can be used to arrange an initial layout.

Each detached window owns a GLFW window, swapchain, frame and VRI GUI geometry buffers. ImGuiApp renders these from `onPostPresent()`. Applications using Gui directly must call `renderPlatformWindows()` after the main GPU frame completes, while referenced textures remain alive. Position and clipping data are converted to framebuffer pixels per viewport.

The default theme is `GuiTheme::eUnreal`. `GuiConfig::theme` selects Dark, Light, Unity, Unreal or Godot at construction; `Gui::setTheme()` switches it at runtime. These editor-inspired palettes are approximations. Theme changes preserve layout spacing and the constraints needed by detached viewports.

Layout persistence is enabled by default:

```text
.vultra/example-imgui/imgui.ini
.vultra/example-gltf-viewer/imgui.ini
.vultra/example-research/imgui.ini
.vultra/example-debugdraw/imgui.ini
.vultra/example-openxr-triangle/imgui.ini
```

An empty `appName` uses the executable stem, independent of the window title or loaded model. An empty `iniFile` selects `.vultra/<AppName>/imgui.ini` under the working directory. The directory is created automatically and the resolved path stays fixed for the context's lifetime.

```cpp
vultra::GuiConfig guiConfig;
guiConfig.appName = "my-research-app";
// Optional explicit path; takes precedence over appName:
// guiConfig.iniFile = "layouts/experiment.ini";
// Disable both loading and saving for tests or temporary windows:
// guiConfig.persistLayout = false;
```

Pass this as ImGuiApp's second constructor argument or Gui's configuration argument. AppName must be a valid single directory name and may contain Unicode. ImGui loads on the first frame, periodically saves changed layouts and saves on normal shutdown. `.vultra/` is ignored by Git. The old shared `imgui.ini` is not imported; delete an application's file to reset its layout.

Use `Gui::textureId(texture)` with `ImGui::Image`. Keep the texture in ShaderResource state until all viewport draws finish. Call `Gui::forgetTexture(texture)` before destroying or recreating it, after the previous GPU frame completes, to release cached VRI descriptors. Platform windows use the desktop BGRA8_UNORM format; disable `multiViewport` for an offscreen GUI targeting another format.

## Built-in Renderer

The default graph contains four shadow passes, skybox, forward OpenPBR shading and tone mapping, followed by the application's display copy, GUI and presentation passes.

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

Scene is ordinary mesh and material data loaded from glTF, OBJ or FBX or generated by an experiment. GpuScene uploads it. Environment prepares lighting textures. BuiltinRenderer adds passes to a graph:

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

Outputs exposes `shadows`, `hdr`, `depth` and `color` for experiments to connect or export. Individual shadow, skybox, forward and tone-mapping pass builders are also available. The renderer, scene and environment must outlive the graph. Each renderer uses one set of per-frame descriptors and assumes one frame in flight. Rebuild the graph when changing shadow resolution, and rebuild GpuScene/renderer when replacing geometry or the texture collection. Uploaded material scalar parameters can be edited through `GpuScene::materials`.

IBL precomputation settings are local to `environment.cpp`: diffuse 64x32, specular 256x128 with nine mips, a 128x128 BRDF LUT and 256 samples. The default environment is procedural; supply a Radiance HDR image for measured lighting. Cubemaps, EXR and runtime reflection probes are not implemented.

## Asset Import

Model examples use `importAsset()` and construct `GpuScene` from its prepared result. The [asset pipeline guide](asset_pipeline.md) explains cache identity, dependency tracking, texture color/compression policy, reimport, CLI options and limits. `loadGltf()`, `loadObj()` and `loadFbx()` remain available for uncached CPU experiments.

## Color Conventions

The old `dev` branch used two defaults: raw swapchain creation selected sRGB, while AppConfig selected linear/UNORM. This branch follows that distinction. `Swapchain` defaults to BGRA8 sRGB; `DesktopAppConfig` defaults to BGRA8 UNORM. Window and RHI triangle explicitly select sRGB. Clearing with linear `(0.2, 0.3, 0.3)` then stores approximately `(124, 149, 149)` in 8-bit sRGB.

ImGui, detached viewports and desktop tone-mapped output use UNORM with display-encoded colors. The built-in renderer's UNORM output applies ACES and one sRGB transfer. Its RGBA16F output applies ACES but stays linear for XR; raw HDR remains available in `Outputs::hdr`. PNG readback performs no extra conversion.

## glTF Support

The Viewer defaults to the included Damaged Helmet GLB. **Open model...** selects a glTF/GLB file, **Reload** validates/reuses the cache, **Reimport** rebuilds it, and **Material spheres** selects the procedural scene (`--materials` on the CLI). Failed loads keep the previous model; successful loads reset the camera and update the window title. `--environment` selects a Radiance HDR image.

The loader supports static triangle primitives, node transforms, indexed and non-indexed geometry, POSITION/NORMAL/TANGENT/TEXCOORD_0/COLOR_0, and generated normals when absent. PNG/JPEG textures have mip chains; base color, emission and specular color use sRGB. Specular weight textures use linear alpha. The current renderer uses a common linear/repeat sampler and double-sided drawing rather than each model's sampler settings. DDS textures can be selected through `MSFT_texture_dds`; see the [asset format limits](asset_pipeline.md#fbx-and-dds-scope). It handles OPAQUE/MASK, numerical IOR/emissive-strength/clearcoat parameters and specular factors/textures.

Normal mapping uses vertex tangents and handedness. Authored glTF tangents are imported directly, transformed with the model's linear matrix and normalized; reflected transforms correct handedness and triangle winding. Only missing tangents are generated from triangle UV derivatives in the geometry jobs. This is deterministic vertex accumulation, not MikkTSpace. The shader orthogonalizes the interpolated tangent against the normal; a degenerate TBN retains the geometric normal rather than normalizing a zero vector. Nonfinite or zero source directions and invalid handedness remain import errors with source, primitive job and vertex context. Procedural `Scene` callers can use `generateTangents()` explicitly; `GpuScene` generates missing frames when normal-mapped materials need them. Tangent-space XY is scaled by the material's `normalScale`, and BC5/RG8 maps reconstruct Z. Screen derivatives are not used to reconstruct the frame.

Emission samples an sRGB texture into linear RGB, then multiplies `emissiveFactor` and `KHR_materials_emissive_strength`. Following glTF, it is added to HDR scattering independently of the perturbed normal and OpenPBR coat lobes, before exposure and tone mapping. It does not create lights on nearby surfaces, bloom or global illumination. Use the viewer's **Emission** view (`--debug 5`) to inspect this contribution separately from direct lighting and IBL. Different lighting, environment maps and tone mapping can change a scene's appearance even when its material inputs match.

The renderer has only opaque and alpha-mask passes. For static scenes authored with BLEND materials, import logs a warning and maps coverage to an alpha cutoff of 0.5 in both forward and shadow passes. Fractional transparency is not rendered; low-alpha decals can therefore disappear. Source files remain unchanged. This policy is intentionally not glTF alpha-blending conformance.

Skinning, morph targets, sparse accessors, additional UV sets, texture transforms and clearcoat extension textures are unsupported; detected uses are rejected. Animations are ignored with a warning and static node transforms are rendered. Unknown required extensions are rejected; unknown optional extensions may be ignored.

Model viewers accept `--eye X Y Z` and optional `--look-at X Y Z` for reproducible interior views and research captures. An explicit eye selects first-person controls; without a target, it faces the scene center. The Debug Draw example loads Damaged Helmet with the original HDR and draws its actual world-space AABB, grid, axes and sphere wireframes. The shared viewer UI can toggle this overlay for other models. Lines test scene depth without writing it; the shared camera projection includes their bounds.

## Input and Camera Controls

`Window::poll()` publishes `Window::input()` before application updates. Each window owns its `Input` state: held, pressed, released and repeated keys, mouse buttons, logical cursor position/delta and accumulated scroll. Events received during an event wait are retained until the next poll. Press/release edges and deltas last one poll; held state persists. Losing focus releases held keys/buttons and discards motion/scroll to prevent stuck controls.

The core input types contain no GLFW or SDL constants. The current GLFW event adapter is under `platform/glfw`; it runs before ImGui's chained callbacks. ImGui-owned detached windows keep their own callbacks and do not supply scene camera input. A future SDL3 window implementation can feed the same `Input` event sink; this branch currently provides only a GLFW window backend. The existing native-window/GUI interop still uses GLFW.

`function/camera` belongs to the `vultra` infrastructure library and has no scene-renderer or ImGui dependency:

- `OrbitCamera`: left-drag orbit, middle/right-drag pan, wheel dolly; radius-relative zoom limits and clip planes. Supply a positive scene radius, positive distance and a valid vertical FOV in radians.
- `FpsCamera`: right-drag look, WASD translation, QE world-Y movement and Shift acceleration. Movement uses elapsed seconds with normalized diagonal speed; angles and vertical FOV are radians.
- Both produce a right-handed `RenderCamera` with depth in `[0,1]`, for raster or ray rendering. Call `camera()` with a nonempty framebuffer extent. An optional minimum far plane includes debug geometry without changing framing.

Controllers read the window input directly. `Gui::inputCapture()` supplies only UI ownership flags; it is not the input source. For an ImGuiApp, update the controller in `onPreRender()` after the base call so the current UI capture decision is available:

```cpp
void Viewer::onPreRender()
{
    ImGuiApp::onPreRender();
    m_Camera.update(getWindow().input(), getWindow().size(), getGui().inputCapture()); // Orbit
}
```

FPS uses `update(input, deltaSeconds, capture)` instead. A DesktopApp without UI can update either controller during `onUpdate()` and omit capture. Mouse coordinates and orbit pan extents use logical window units; projection extents use framebuffer pixels. Window scroll survives `ImGui::Render()`, which clears ImGui's own wheel value.

The glTF/Debug Draw viewers share OrbitCamera. Sponza, meshlet Sponza, ray query, ray tracing and the XR desktop rig reuse FpsCamera. XR eye projection and head pose still come from OpenXR. `test-camera` covers event accumulation, focus loss, UI ownership, orbit/pan/FPS behavior and GPU-observed wheel zoom through the GLFW/ImGui callback chain.

## RenderGraph

`addPass(name, uses, execute)` declares resource accesses and records VRI commands in a callback. Assemble passes with producers before consumers. `exportResource()` and `sideEffect=true` identify observable outputs.

The graph tracks RAW/WAR/WAW dependencies, culls passes without observable effects and inserts barriers before each pass. It rejects reads of uninitialized resources, duplicate resource declarations within one pass, and resource type/usage mismatches. Overwrite dependencies are conservative; resources are not versioned.

The supported scope is one graphics queue, single-mip/single-layer 2D color or D32 textures, and storage/copy buffers. Depth access can be read, write or read/write. Graph-owned resources live until graph destruction; examples rebuild on resize. There is no transient aliasing, subresource state tracking or cross-queue scheduling. A pass handles its own internal synchronization.

Callers own imported resources and guarantee their initial contents and lifetime. Per-frame `bind()` replaces an imported texture only with an equivalent descriptor. Environment precomputes and synchronizes its multi-mip IBL textures separately, then uses them as read-only external resources.

Data-driven and Python/Lua construction are not implemented. A future frontend should select passes, parameters and connections while reusing the existing C++ graph compiler and executor.

## Debug Drawing

Debug bounds, grid, axes and sphere lines load the built-in renderer's scene depth. They use `LessOrEqual` testing without depth writes, so geometry occludes rear lines while coplanar lines remain visible. The graph declares this as `eDepthRead`; resizing rebuilds the depth attachment with the scene. When debug drawing is enabled, orbit and first-person cameras extend their far plane to include the generated line geometry's bounding sphere, with a small margin. The model and lines share that projection; model framing, zoom and the near plane are unchanged. `test-debugdraw` checks occlusion, depth preservation and far clipping through GPU readback.

## Meshlet Rendering

```powershell
xmake run example-meshshading-sponza
xmake run example-meshshading-sponza --meshlet-colors
xmake run example-meshshading-sponza --indexed
xmake run example-sponza --meshlets --no-meshlet-culling
```

The dedicated example requests `VriFeature_MeshShader`, builds meshlets from the original Sponza assets and renders them through Slang task/mesh stages. Its UI exposes **Mesh shading**, **Meshlet frustum culling** and **Meshlet colors**. The indexed switch uses the same material/lighting passes for comparison. Other scene viewers can opt in with `--meshlets`; ordinary runs retain the indexed path and do not build meshlet data.

`GpuScene(device, asset, true, workers)` enables the geometry buffers. Set `BuiltinRenderer::settings.meshShading` to select the mesh path; `meshletCulling` and `meshletColors` control culling and visualization. Only the forward geometry path uses meshlets currently; shadow maps retain indexed draws. The sphere test uses Vulkan's zero-to-one depth clip volume. Every task invocation reaches the group barriers, including partially filled final groups. Source tangents are consumed unchanged after their import transform.

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

FileWatch is vendored at commit `a59891baf375b73ff28144973a6fafd3fe40aa21`, with its upstream header and MIT license preserved in `external/FileWatch`.

## Research Utilities

```powershell
xmake run example-research --frames 60 --dump captures/run01 --capture captures/reference.png
xmake run example-research --frames 1 --compare captures/reference.png
```

Frame dumping writes `frame_000000.png`-style images and `timings.csv` into a new directory. Research captures and comparisons use the scene texture without GUI. Other desktop examples capture the displayed image, including GUI where present: the last frame with `--frames`, or the first frame otherwise.

`readback(device, texture, mip=0)` returns top-left-origin RGBA float pixels. It supports RGBA8/BGRA8, RGBA16F and RGBA32F for a selected mip of a single-layer, single-sample texture. Alpha is preserved and floating-point values are not clipped. sRGB readback preserves stored values. `savePng()` clamps to [0,1] and quantizes to 8 bits without gamma, exposure or tone mapping.

`compare(reference, test, peak=1)` computes RGB MSE/PSNR without alpha; identical images yield infinite PSNR. SSIM uses luminance weights 0.2126/0.7152/0.0722, an 11x11 Gaussian window with sigma 1.5, K1=0.01 and K2=0.03, and only complete windows. Inputs must match and be at least 11x11. Experiments must choose a consistent color space and peak value. See the [SSIM paper](https://ece.uwaterloo.ca/~z70wang/publications/ssim.pdf) for the method.

Profiler records CPU command-recording time and GPU timestamps for up to 64 non-nested passes, including each pass's preceding barriers. Presentation, waits between frames and PNG encoding are excluded. Warm up before measuring. Vultra currently uses one frame in flight and synchronous readback; frame dumping affects application throughput and CPU/GPU work does not overlap.

## OpenXR

`OpenXRSystem -> Device(creationHooks) -> OpenXRSession` uses `XR_KHR_vulkan_enable2` for instance/device creation and GPU selection. Each eye has its own swapchain. Views use predicted display time pose/FOV, with session state, invalid tracking, `shouldRender` and acquire/wait/release handled explicitly.

The desktop mirror shows both eyes side by side with preserved aspect ratios. Mirror reads complete before releasing acquired eye images, which return to color-attachment layout. When no valid views are available, the desktop UI can still update. In this example, `--frames` counts application loop iterations; the log reports actual eye-rendered frames separately. `--capture` saves the desktop mirror.

sRGB RGBA/BGRA formats are preferred over UNORM. Following the [OpenXR linear-composition contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrSwapchain.html), eye shaders output linear color: sRGB attachments encode in hardware, while UNORM attachments store linear values. The sample triangle's authored sRGB colors are decoded for XR. Mirror sampling returns linear values, which are encoded once for the desktop UNORM output.

Controllers, hand tracking, depth composition and in-headset ImGui are not implemented. Desktop ImGui is available. Four RGBA/BGRA sRGB/UNORM formats and mirror color parity are covered by offscreen tests; they do not validate the real headset/runtime path. That path remains unverified because the runtime reported no available headset during local validation.

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

## Verification

The test targets cover image metrics/PNG, CLI validation/logging, RenderGraph/GPU behavior, FileWatch failure and recovery, application lifecycle, GUI themes/viewports/layout persistence, asset cache invalidation/recovery and compressed uploads, IBL, OpenPBR reference evaluation and shadow filtering. Run them with `xmake test -v`; they are not part of default `xmake run`.

Shader failure/recovery tests intentionally compile invalid input and label the expected error. A successful process exit alone does not establish correct rendering; inspect GPU diagnostics and relevant readback results. The OpenPBR test compares 49 Slang/C++ cases, but does not certify full material or glTF conformance. Hardware-independent checks cannot replace headset validation.

For code changes, use `.clang-format` and `.clang-tidy`, the generated compilation database and finite-frame runs of affected examples. Temporary verification data belongs under `build/.tmp/`. Keep personal layouts and experiment outputs out of source control.

Use a complete LLVM installation for clang-tidy, including its matching `lib/clang/<version>/include` resource headers. A tidy-only installation can incorrectly pick up MSVC's SIMD headers and report narrowing errors inside `stb_image_resize2`. Run examples through `xmake run` so their pinned package DLL directories, including Slang's downstream compiler, are on the runtime search path.
