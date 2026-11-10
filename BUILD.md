# Build and verification

For complete minimal application code, start with [get_started.md](docs/get_started.md).

## Prerequisites

- Windows x64: Visual Studio 2022 with the C++ toolchain.
- macOS: Apple Command Line Tools and `VULKAN_SDK` pointing to a Vulkan SDK containing `lib/libvulkan.1.dylib`.
  The Vulkan configuration builds the pinned MoltenVK runtime described below. Full Xcode is not required for it.
- Linux x86_64: C++23 compiler, CMake, pkg-config, Python 3, Vulkan development files,
  X11/Wayland development libraries, wayland-protocols and xkbcommon.
- [xmake](https://xmake.io/guide/quick-start.html#installation) on PATH.
- Vulkan 1.3 GPU/driver for rendering. Ray tracing, ray queries and mesh shading require their device features.
- Python/libclang and clang-format for API regeneration only. Ordinary builds use checked-in bindings and parsers.

Dependency versions and static package recipes live in [external/xmake.lua](external/xmake.lua) and
[external/packages](external/packages/packages). The initial source build of Slang takes time.
MSVC targets and packages use matching MT/MTd settings. Preserve the [VRI patches](external/vri/README.md).
On macOS, Vulkan consumers also require the local `moltenvk-vultra` runtime package. It retains MoltenVK 1.2.11 and
backports the SPIRV-Cross buffer-array length expression fix. `xmake run` selects its ICD through `VK_DRIVER_FILES`;
the SDK's loader and validation layers remain in use. Standalone launches must set `VK_DRIVER_FILES` to the package's
`share/vulkan/icd.d/MoltenVK_icd.json`. For a separate installation, copy that ICD and `lib/libMoltenVK.dylib`, preserving
their relative paths. This does not modify the installed Vulkan SDK or configure a system-wide driver.
The optional `random` feature uses the small vendored [PCG32 library](external/pcg_basic/README.md); no installation
or dynamic runtime is required. Builtin packs retain its license and source attribution.

## Configure and build

Run from the repository root:

```sh
xmake f -m release -y
xmake build -y --all
xmake run example-basics vri --frames 60
```

Root builds default to all features/examples with tests and OpenXR enabled. Configuration persists between runs.
`xmake run` builds its target and prepares its actual assets; tests run separately. It also supplies package
DLL search paths and the examples' repository-root working directory.

This checkout builds the library and its basic/offline/UI examples. Selected library features require no EnTT,
Lua or .NET. Standalone programs can use only the capabilities they select.

| Option | Purpose |
| --- | --- |
| `libvultra_features` | Comma-separated feature union; root default `all`, embedded default `gpu`. |
| `libvultra_examples` | Category, `category.mode`, or comma-separated union; default `all`. |
| `libvultra_build_examples`, `libvultra_build_tests` | Default on at the root, off when embedded. |
| `libvultra_window_backend` | `glfw` (default) or `sdl3`. |
| `libvultra_graphics_api` | `vulkan` (default); WASM requires explicit `webgpu` or `webgl2`. |
| `libvultra_example_pack` | Host-cooked VPK embedded in Web `basics.vri` / `basics.graph`. |
| `libvultra_with_openxr` | Disable with `n` when XR is not needed; explicit XR selections require it. |

For a minimum example, disable the full test collection and let the example declare its requirements:

```sh
xmake f --libvultra_features=foundation --libvultra_build_tests=n --libvultra_build_examples=y \
    --libvultra_examples=basics.vri
xmake run example-basics vri --frames 3 --capture build/.tmp/triangle.png
```

Select `basics` for the whole category or `basics.vri,offline,ui` for a union.
Compiled modes are listed by `xmake run <target> --help`; unknown selections fail configuration.
[examples/selection.lua](examples/selection.lua) and [scripts/build_features.lua](scripts/build_features.lua)
are the authoritative requirement lists. Restore the full collection with:

```sh
xmake f --libvultra_features=all --libvultra_examples=all --libvultra_build_tests=y --libvultra_build_examples=y
```

## Use as a subproject

Include a pinned checkout in a C++23 parent project:

```lua
set_project("my-renderer")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")
if is_plat("windows") then
    set_runtimes(is_mode("debug") and "MTd" or "MT")
end
includes("external/libvultra")

target("my-renderer")
    set_kind("binary")
    add_deps("vultra")
    add_files("source/**.cpp")
    set_rundir("$(projectdir)")
target_end()
```

Add actual features with `--libvultra_features=desktop,graph,shader,image_io`; this requires no scene or importer.
For a root library-only build also disable examples/tests. Embedded builds preserve parent editor settings and
do not register standalone apps/tools. VGui is included in `vultra` only when its feature is selected; RmlUi headers
remain private to the implementation.

Source-based builtin rendering expects the library's `builtin/shaders` and `external` include roots. Resolve
application paths before using `ScopedWorkingDirectory` around the library shader root; GPU owners remain inside
that scope. Shipping applications must cook/package their shader closure rather than depend on the checkout.

## GPU selection

`Device` convenience constructors probe Vulkan adapters at startup and reject candidates missing
required features or a graphics queue. An explicit `VriDeviceCreationDesc` preserves its requested adapter;
explicit best-effort convenience requests permit feature negotiation, as before. Eligible devices are ranked lexicographically by discrete,
integrated, unknown, then software type; dedicated VRAM; exposed queue-type coverage; and exposed queue
count. Equal scores retain enumeration order. The selected device is retained, unused probes are destroyed,
and the startup log records the GPU, VRAM, queue counts and enabled feature mask. Required features are
never silently removed from strict requests. Queue counts describe VRI's exposed queues and can include aliases, not independent
hardware engines. The pinned VRI Vulkan implementation exposes one queue per type, limiting that tie-breaker.

OpenXR creation hooks keep the runtime-selected physical device and strict required-feature handling.
No OS GPU preference is necessary for ordinary automatic selection. Run `xmake test -v test-device-selection/*`
for baseline submission, supported Ray Query/bindless submission and unsupported-feature rejection.

## HiDPI editor UI

EditorGui uses the monitor content scale for fonts, control spacing and theme rounding. The bundled
Lato Latin font is embedded at build time, with a base size of 16 UI units; retain its
[SIL OFL license](resources/ui/LICENSE.txt) when distributing the engine. ImGui 1.92's dynamic atlas
bakes glyphs at the requested font size and framebuffer density. Atlas subrect uploads use VRI;
cached views are released before their textures. Ordinary frames do not recreate the atlas.
GLFW resets its process-wide scale-to-monitor creation hint before ImGui creates native-sized
platform windows.
Font scale follows detached viewport DPI. Shared style spacing follows the main window's monitor.

Initial Window extents use base-DPI units (GLFW scale-to-monitor, SDL display content scale).
Subsequent size/framebufferSize queries still return the backend's native units and pixels respectively.
Framebuffer density remains separate from content scaling, so Retina-style rendering is not enlarged twice.
Offscreen UI defaults to scale 1; applications can use ImGuiStyle::FontScaleMain for deliberate UI zoom.
Theme changes retain custom spacing and scale their rounding to the current DPI.

Run `xmake test -v 'test-gui-*/*'` for window/offscreen layout and GPU widget tests. The latter draws and
reads back fonts at 1x, 2x and 1x again, covering atlas updates alongside mouse/text/property interaction.
The window settings test also renders four frames into a detached viewport and checks its actual native size.

## Desktop and XR

On Linux select the backend at configuration and the window system per process:

```sh
xmake f --libvultra_window_backend=sdl3 -y
VULTRA_WINDOW_SYSTEM=wayland xmake run example-ui --frames 60
VULTRA_WINDOW_SYSTEM=x11 xmake run example-ui --frames 60
```

Both backends support X11/XWayland and Wayland. Detached ImGui viewports require Windows/X11; Wayland supports
in-window docking. The Linux file picker needs D-Bus and an `xdg-desktop-portal` backend; its Wayland dialog
has no parent with the pinned NFD version.

XR execution needs an active runtime and a device. Runtime registration can select the default; a per-process
`XR_RUNTIME_JSON` points to an installed runtime manifest when needed. Monado's simulated device is useful for
development; follow its [upstream setup](https://monado.freedesktop.org/getting-started.html).
Simulation does not validate physical tracking, controllers or headset latency.

## Android and Web applications

`android_app` selects the GameActivity host; `web_app` selects the animation-frame host. Derive from PlatformApp
and pass AppLaunch through the constructor; [the example entry](examples/common/entry.cpp) shows the Android
`android_main` and desktop/Web `main` boundaries. No desktop window backend is selected for these hosts.
The existing `example-basics` modes `window`, `vri` and `graph` use this path. Other examples are still being ported;
their host-side tools, GUI, language and XR requirements must not be confused with supported mobile features.

For shader examples, prepare the existing offline pack with a native configuration that selects `offline` first:

```sh
OUT="$PWD/build/.tmp/platform-examples"  # Choose a fresh output directory.
mkdir -p "$OUT"
xmake run example-offline-cook . "$OUT/pack"
```

From the repository root, with NDK r29 and SDK platform/build-tools 35 installed:

```sh
ABI=arm64-v8a  # x86_64 for emulator QA.
XMAKE_CONFIGDIR="$OUT/android-config" xmake f -y -p android -a "$ABI" -m release \
    --ndk="$ANDROID_NDK_HOME" --ndk_sdkver=29 --runtimes=c++_static -o "$OUT/android" \
    --libvultra_features=foundation --libvultra_examples=basics.window,basics.vri,basics.graph,offline \
    --libvultra_build_examples=y --libvultra_build_tests=n --libvultra_with_openxr=n
XMAKE_CONFIGDIR="$OUT/android-config" xmake build -y example-basics example-offline
python3 scripts/package_android.py "$OUT/android/android/$ABI/release/libexample-basics.so" \
    "$OUT/pack/project.vpk" "$OUT/apk" --sdk "$ANDROID_SDK_ROOT" --gradle "$GRADLE" --abi "$ABI"
```

The packaging script takes built files and does not call xmake. It creates a debug APK with GameActivity 4.4.2,
AppCompat 1.7.1, stored VPK, static C++ runtime, signing and 16 KiB alignment checks. Its template uses AGP 8.7.3;
Gradle 8.9 and JDK 21 were tested. Set JAVA_HOME/PATH and, optionally, GRADLE_USER_HOME before packaging.
For an installed APK, launch `org.vultra.example/com.google.androidgamesdk.GameActivity` with Intent string-array
extra `vultra.arguments`, for example `--esa vultra.arguments vri,--frames,45`. Without a mode, it lists modes and exits.
Use the same script with `libexample-offline.so` and `--application-id org.vultra.example.offline` for the offline example.

After activating Emscripten 6.0.3, build WebGL2 from the repository root:

```sh
XMAKE_CONFIGDIR="$OUT/web-config" xmake f -y -p wasm -a wasm32 -m release -o "$OUT/web" \
    --libvultra_graphics_api=webgl2 --libvultra_features=foundation \
    --libvultra_examples=basics.window,basics.vri,basics.graph --libvultra_example_pack="$OUT/pack/project.vpk" \
    --libvultra_build_examples=y --libvultra_build_tests=n --libvultra_with_openxr=n
XMAKE_CONFIGDIR="$OUT/web-config" xmake build -y example-basics
```

For WebGPU, cook a separate WGSL pack with the native tool before activating Emscripten:

```sh
xmake run example-offline-cook . "$OUT/pack-wgsl" wgsl
# General cooker: xmake run vultra-shader shader.slang --profile wgsl --output shader.vshaderc
```

Build the same clear/VRI/graph modes in a separate configuration:

```sh
XMAKE_CONFIGDIR="$OUT/webgpu-config" xmake f -y -p wasm -a wasm32 -m release -o "$OUT/webgpu" \
    --libvultra_graphics_api=webgpu --libvultra_features=foundation \
    --libvultra_examples=basics.window,basics.vri,basics.graph --libvultra_example_pack="$OUT/pack-wgsl/project.vpk" \
    --libvultra_build_examples=y --libvultra_build_tests=n --libvultra_with_openxr=n
XMAKE_CONFIGDIR="$OUT/webgpu-config" xmake build -y example-basics
```

Serve the chosen output over HTTP, with its `.html`, `.js` and `.wasm` files together:

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory "$OUT/webgpu/wasm/wasm32/release"
# http://localhost:8000/example-basics.html?arg=window
```

For WebGL2, serve `$OUT/web/wasm/wasm32/release` instead; select `vri`, `graph` or `window` in the URL.
The VPK is embedded; deployment requires no xmake. WebGL2 consumes cooked SPIR-V through VRI's SPIRV-Cross
backend. WebGPU consumes WGSL emitted directly by Slang from the same sources. Each `.vshaderc` contains one
target's code and reflected layout; build separate packs for SPIR-V and WGSL. Pipeline creation rejects mismatched
targets. The basic examples do not establish support for every shader stage, material or renderer on WebGPU.
The additional HDR/storage-image shaders in the example pack are cooked but do not yet have WebGPU runtime acceptance.
See [roadmap](docs/roadmap.md) for remaining GUI, renderer, XR and packaging work.

Use HTTPS or localhost for WebGPU. Opening the HTML without a mode shows the available modes on the page.
The page also displays loading/runtime errors; the console retains full diagnostics. `--frames` is for finite QA;
finishing destroys the GPU canvas. Test the browser's normal GPU configuration first: the forced headless
SwiftShader profile rendered offscreen but failed even a plain WebGPU canvas test. Normal Edge completed a
45-frame clear run and a six-frame canvas/readback comparison. An independent default-adapter probe reported
SwiftShader; the application's adapter was not measured, so hardware WebGPU is not validated by these results.
Later repeated requests stalled during adapter/device creation in both plain JavaScript and the application, with browser
settings unchanged. Repeated WebGPU startup and device-loss recovery remain unaccepted; no browser test validates Quest.
The hidden in-app browser separately runs host-cooked WGSL in the direct and graph triangle modes, with matching
canvas/readback pixels. This does not resolve the independent Edge startup issue above.

## Android and Web foundation probes

These are finite offscreen tests of the minimal `gpu,asset_source,image_io` selection, not ports of DesktopApp,
the editor or player. They draw a triangle, then two rotating textured/lit indexed cubes with depth and a second
postprocess pass. Three parameter updates, draw-order invariance and every postprocess pixel are checked.
PNG outputs include the triangle, uploaded checker, 321 Ã— 237 scene/final output and a 64 Ã— 64 test pattern. Android
maps an uncompressed APK asset; Web links the pack into WASM and makes no project-VPK request. The host packer
emits SPIR-V and WGSL from the same Slang triangle, multipass and Compute sources; no handwritten WGSL copy is used.

`platform-shader-runtime` consumes the `example-offline-cook` VPK. Select `shader_runtime,asset_source,image_io`,
set `platform_pack` to that pack, and build this target from `tests/platform`. Pass a fresh output PNG path when
running it. It reuses ColoredMesh and checks translated/nonuniform transforms, changing push constants and linear
RGB against a CPU reference, with no window or Slang runtime dependency.

For cooked game-material QA, select `game_shader,image_io` when building `platform-pack`; this also embeds
SPIR-V/WGSL versions of `material.vshader`. In a separate native or WebGPU configuration, select
`shader_material,image_io` with that `platform_pack`, then build `platform-shader-material`. The probe requires
a fresh output directory and checks four frames: defaults, a uniform edit, a variant change and a persisted instance.
It rejects host compiler selection at compile time.
`platform-shader-reload` uses the same feature/pack selection and takes a fresh output directory. It checks an
embedded material plus two independent instances across corrupt data, missing/failed preparation, a wrong target
and successful recovery. These finite probes need no window or xmake at execution time.

Use a host compiler for the packer, Android NDK r29/API 29 for native code, Android SDK platform/build-tools 35,
a JDK for APK packaging, and Emscripten 6.0.3 for the tested browser builds. Keep each configuration isolated:

```sh
ROOT="$PWD"
OUT="$ROOT/build/.tmp/platform-probe"  # Choose a fresh directory.
mkdir -p "$OUT"
cd tests/platform
XMAKE_CONFIGDIR="$OUT/host-config" xmake f -P . -F xmake.lua -p linux -a x86_64 -m release \
    -o "$OUT/host" --libvultra_features=asset_source,shader,image_io \
    --libvultra_graphics_api=vulkan --libvultra_build_examples=n --libvultra_build_tests=n \
    --libvultra_with_openxr=n -y
XMAKE_CONFIGDIR="$OUT/host-config" xmake build -P . -F xmake.lua -y platform-pack
"$OUT/host/linux/x86_64/release/platform-pack" "$ROOT/tests/platform" "$OUT/pack"
```

For Android, set `ANDROID_NDK_HOME`/`ANDROID_SDK_ROOT` to installed tools, then configure from `tests/platform`:

```sh
ABI=arm64-v8a  # Use x86_64 in a separate output/configuration for emulator QA.
XMAKE_CONFIGDIR="$OUT/$ABI-config" xmake f -P . -F xmake.lua -p android -a "$ABI" -m release \
    --ndk="$ANDROID_NDK_HOME" --ndk_sdkver=29 --runtimes=c++_static -o "$OUT/$ABI" \
    --libvultra_features=gpu,asset_source,image_io --libvultra_graphics_api=vulkan \
    --libvultra_build_examples=n --libvultra_build_tests=n --libvultra_with_openxr=n \
    --platform_pack="$OUT/pack/project.vpk" -y
XMAKE_CONFIGDIR="$OUT/$ABI-config" xmake build -P . -F xmake.lua -y platform-render platform-vpk-memory
python3 package_android.py "$ANDROID_SDK_ROOT" "$ABI" \
    "$OUT/$ABI/android/$ABI/release/libplatform-render.so" "$OUT/pack/project.vpk" "$OUT/apk-$ABI"
```

Packaging the already built library/VPK uses SDK/JDK tools, not xmake. The resulting APK uses a temporary debug
key, stores VPK/native code uncompressed and checks signing/16 KiB native-library alignment. Install on an
x86_64 AVD with `adb install`, launch `org.vultra.platformprobe/android.app.NativeActivity`, check the `Vultra`
logcat tag for `PLATFORM_RENDER_PASSED`, then retrieve `files/platform.png` with `adb exec-out run-as
org.vultra.platformprobe cat files/platform.png`. The activity finishes after the test. Use `-no-window -gpu host`
for triangle-only host-GPU checks. The full probe passes with `-no-window -gpu lavapipe`; the tested NVIDIA host-GPU
emulator corrupts CPU-uploaded texture data, while its triangle and Compute-only paths pass. The SDK's SwiftShader
emulator driver lacks VRI's required `shaderDrawParameters`. These are explicit QA driver selections, not engine
fallbacks. Check the success marker and readbacks; an Activity launch alone does not establish rendering success.
ARM64 compilation and emulator pixels do not validate Quest OpenXR, tracking, controllers or eye submission.

For Web, first activate the Emscripten SDK environment, then configure from the same test directory:

```sh
API=webgpu  # Repeat with webgl2 in its own output/configuration.
XMAKE_CONFIGDIR="$OUT/$API-config" xmake f -P . -F xmake.lua -p wasm -a wasm32 -m release \
    -o "$OUT/$API" --libvultra_features=gpu,asset_source,image_io --libvultra_graphics_api="$API" \
    --libvultra_build_examples=n --libvultra_build_tests=n --libvultra_with_openxr=n \
    --platform_pack="$OUT/pack/project.vpk" -y
XMAKE_CONFIGDIR="$OUT/$API-config" xmake build -P . -F xmake.lua -y platform-render platform-vpk-memory
python3 -m http.server 8000 --directory "$OUT/$API/wasm/wasm32/release"
```

Open `http://localhost:8000/platform-render.html`; successful rendering displays the readback and logs the same
success marker. Serve the `.html`, `.js` and `.wasm` together; deployment needs no xmake or external VPK. API
selection is explicit and unsupported platform/API pairs fail configuration. Browser initialization needs
ASYNCIFY; this finite probe is not an animation-frame or canvas lifecycle implementation. Headless software
WebGPU/WebGL2 tests verify rendering behavior, not hardware/browser coverage. [Platform gates](docs/roadmap.md)
record the remaining lifecycle, shader, export and physical-headset work.

Each probe writes `platform-capabilities.json` beside its PNGs. Device requests optional RT/Ray Query/Mesh features
with VRI's best-effort negotiation; the report records the actual granted mask and `GetDeviceDesc()`/format-query
results. Compute-capable devices dispatch into a buffer and copy its image to a texture. Otherwise a fragment pass
renders the same exact CPU-checked pattern; the log names the selected path and reason. Missing required scene
formats select the simple triangle explicitly. Capability flags do not establish RT/Mesh execution on a platform.
Existing ordinary Device construction stays strict; shader, recording and readback errors are never fallback triggers.

## Offline rendering

AI/QA can render without a desktop connection. The small [offline example](examples/offline/main.cpp) uses
direct VRI, a project compute pass, C++/JSON graphs, profiling and PNG/linear HDR readback:

```sh
xmake f --libvultra_features=foundation --libvultra_build_tests=n --libvultra_build_examples=y \
    --libvultra_examples=offline
xmake run example-offline --frames 3 --output build/.tmp/offline-cpp
xmake run example-offline --frames 3 --graph examples/offline/color_gain.vgraph \
    --output build/.tmp/offline-graph
```

Use fresh output directories. `--gain` applies to the direct path; JSON owns its own parameters.
The same rendering implementation also accepts host-cooked shaders from a VPK:

```sh
xmake build example-offline-cook
./build/linux/x86_64/release/example-offline-cook "$PWD" build/.tmp/offline-pack
./build/linux/x86_64/release/example-offline --shaders build/.tmp/offline-pack/project.vpk \
    --graph color_gain.vgraph --output build/.tmp/offline-packed
```

With `--shaders`, `--graph` names a VPK entry. Neither built command needs xmake at execution time.
`shader_runtime` selects artifact loading/pipeline construction without Slang or FileWatch; `shader` adds the host
compiler and watching. `material_block` adds reflected CPU material values, symbolic AssetId resources and checked
snapshot persistence without selecting a compiler or game-material schema. `game_shader_runtime` loads cooked game materials/variants and typed instances;
`shader_material` adds their GPU bindings/pipelines and `ShaderRuntime` snapshot ownership without a compiler,
parser, importer, scene or window. `ShaderRuntime::reload()` explicitly replaces cooked assets transactionally;
source polling/`compiling()` is available when `game_shader` is also selected. Compile options apply to source
assets; cooked programs retain their archived target/layout.
`ShaderCompileOptions::optimization` selects none, default, high or maximal Slang optimization; the default remains
Slang's default level. For SPIR-V, default, high and maximal also run the pinned SPIRV-Tools performance pipeline.
None skips those transforms. To keep the selected Slang level and skip just the SPIRV-Tools pass, set
`ShaderCompileOptions::runSpirvToolsOptimization` to false or pass `--skip-spirv-tools` to the host cooker.
WGSL compiles directly with Slang. `debugInformation` can override the compiler library's build default
(standard in debug, none in release). These options participate in the program cache request. The host cooker
selects the levels with `--optimization none|default|high|maximal` and `--debug-info none|minimal|standard|maximal`.
Record these options with experiment inputs; a higher level is not a performance guarantee. Cooked artifacts retain
their compile fingerprint, but do not yet contain a complete human-readable compiler request.
Use host `game_shader` to cook `.vshader` assets. Recook artifacts after reflection-layout changes; the development
format remains version `1`. The full `rendering` feature still selects host preparation. The Android `offline` selection uses the runtime feature and the existing graph/draw code.
Follow [Android applications](#android-and-web-applications) to build/package `libexample-offline.so` with
`--application-id org.vultra.example.offline`. The finite GameActivity entry runs both C++ and packaged JSON graphs,
then finishes. Require `OFFLINE_EXAMPLE_PASSED` in the `Vultra` log and retrieve `files/direct/display.png`,
`files/graph/display.png` and both `linear.pfm` files with `adb exec-out run-as org.vultra.example.offline cat ...`.
Clear only this test application's data before rerunning; existing captures are never overwritten. The offline
example does not create a surface or OpenXR session.

## Compute context and C ABI

The optional `compute` feature builds the synchronous buffer/kernel C++ context and generated C ABI on top of
`shader`/`gpu`. It selects no window, UI, engine or Python runtime. Use
[ComputeContext](source/drivers/include/vultra/drivers/compute/compute_context.hpp) for explicit buffer allocation,
transfers, kernel compilation and dispatch. The current scalar/layout subset and ownership rules are documented
in [architecture.md](docs/architecture.md#properties-and-c-abi).

Normal native builds consume checked-in generated bindings. Regenerate with `xmake codegen`; run
`python3 tests/codegen_compute.py` alongside the existing generator tests when changing this domain.
Linux Vulkan is the initial acceptance; other platforms and broader resource/argument layouts remain open.

## Development checks

```sh
xmake test -v
xmake codegen --check
python3 tests/codegen.py
python3 tests/codegen_domains.py
clang-format --dry-run --Werror path/to/changed.cpp
clang-tidy -p . path/to/changed.cpp
```

After annotation changes run `xmake codegen` first. Use the current configuration's `compile_commands.json`;
changing backends requires regeneration. Use a complete LLVM installation with matching resource headers.
`source/api/codegen.json` selects declaration-header groups; new annotated types in those groups need no per-type
manifest entry. Run generator tests when changing discovery, marshalling or emitters. A consuming project can
invoke the same tool with its own manifest and compilation database.
Scoped tests and finite-frame image readback must cover changed behavior; expected failure tests still report errors.
Normal xmake tests reject GPU validation errors on stdout or stderr even when the program exits successfully.

Explicit offscreen variants avoid native windows:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY xmake test -v -j1 'test-gui-settings/offline' \
    'test-gui-offscreen/*' 'test-camera/offline' 'test-vpk-memory/*' 'test-graph-*/*'
python3 tests/build_modules.py foundation gpu graph_json shader_graph shader_graph_json
```

The Python drivers check only this library's selected sources, packages and targets. Run a focused selection
while developing, or omit case names to run the complete driver. Example checks include CLI selection and offline
image parity; tool checks copy executables and remove xmake from PATH. They need Pillow for image inspection.

```sh
python3 tests/build_examples.py offline
python3 tests/build_tools.py tool_import tool_shader builtin_pack
```

Linux dependency checks use `readelf`. These checks do not replace native window, Windows or headset acceptance.

Full X11 tests need a window manager honoring exact resize/iconify requests, such as Openbox, including under
an isolated Xvfb display. A bare Xvfb server or a tiling compositor is not an equivalent test environment.
Wayland-specific checks use `VULTRA_WINDOW_SYSTEM=wayland xmake test -v 'test-window/*' 'test-camera/*'`.
Keep verification output under `build/.tmp/` and isolate application layouts; do not overwrite desktop state.

Run `sh scripts/setup_vscode.sh` or `./scripts/setup_vscode.ps1` for clangd/shader editor settings.
Source `scripts/setup_xmake_completion.zsh` or dot-source `scripts/setup_xmake_completion.ps1` for shell completion;
add that source command with an absolute helper path to your shell profile for persistence.
Personal settings, compilation databases and captures stay out of source control.

## Explicit queue verification

Configure the desired host backend and enable library tests first. This fixture submits a graphics-producer,
compute-consumer and transfer-readback chain using caller-owned VRI fences. It checks exact results, Frame reuse,
GPU wait behavior on independent Metal queues and unsubmitted compute/blit discard. Queue types may alias on
Vulkan, so its control submits dependencies in producer order. Successful data checks do not prove physical overlap.

```sh
mkdir -p build/.tmp
QUEUE_CHECK_DIR="$(mktemp -d "$PWD/build/.tmp/frame-queues-XXXXXX")"
xmake build vultra-shader
xmake run vultra-shader "$PWD/tests/shaders/frame_queues.slang" \
    --debug-info none --output "$QUEUE_CHECK_DIR/probe.vshaderc"
xmake build test-frame-queues
xmake run test-frame-queues "$QUEUE_CHECK_DIR/probe.vshaderc"
xmake build test-device
xmake run test-device
```

VultraLab exposes these sources as `test-android-frame-queues` and `test-android-device` for standalone Android
verification. API 33 shell runs are separate from the API 29 application build. Their successful data/state checks
do not establish that the Android Khronos layer loaded or that a headset compositor was exercised.

`test-queue-textures` adds selected mip/layer render views, independent compute, a dependent texture consumer and
complete transfer readback. Six cycles cover three mips and two layers and verify unchanged subresources as well.
The serial control changes only one GPU wait, retaining the same output and workload:

```sh
xmake run vultra-shader "$PWD/tests/shaders/queue_textures.slang" \
    --debug-info none --output "$QUEUE_CHECK_DIR/textures.vshaderc"
xmake build test-queue-textures
xmake run test-queue-textures "$QUEUE_CHECK_DIR/textures.vshaderc"
xmake run test-queue-textures "$QUEUE_CHECK_DIR/textures.vshaderc" --serialize-independent
```

VultraLab also exposes `test-android-queue-textures`. Native Metal controls require its three distinct queues and
hold both initial submissions behind a GPU start gate. Vulkan submits in producer order so aliased queues remain
legal; distinct queue handles alone do not establish concurrent execution.

For an external macOS scheduling capture, use a full Xcode installation's `xctrace` and its `Metal System Trace`
template. An installed command-line-tools developer directory alone is insufficient; set `DEVELOPER_DIR` for the
capture command if needed. Launch the native test with its normal runtime library environment and finite
`--iterations 1048576 --frames 24` arguments, then attach to that executable's PID for a short capture. Repeat with
the same binary/shader and `--serialize-independent`, waiting for each target to finish and checking all readbacks.
The verified workflow attaches to an already running target; local direct-launch attempts did not execute the
target before the recording limit and are not accepted captures. Filter exported `metal-gpu-intervals` by the
actual target PID and GPU Active state, distinguishing fragment/compute work from blit. Keep captured timing
separate from uninstrumented performance measurements. A shader-instruction timeline or memory-allocation record
requires its own enabled instrument and evidence; stage overlap alone does not provide them.

## Descriptor binding verification

With the chosen Metal or Vulkan configuration:

```sh
xmake build vultra-shader
xmake build test-descriptor-bindings
xmake run vultra-shader tests/shaders/descriptor_bindings.slang --output build/.tmp/descriptor_bindings.vshaderc
xmake run test-descriptor-bindings build/.tmp/descriptor_bindings.vshaderc
```

The shader's reflected storage bindings and four-element input array must match the layout. Six descriptor types
have partially-bound layout creation checks. Fixed/full, partial/sparse and variable/sparse models each run two
compute frames and compare 64 uint channels, including unchanged sentinels and a view replacement after completion.
Sparse cases leave slots 1 and 2 unset while consuming slots 0 and 3. View offsets use the reported storage alignment.
On Vulkan, a second device without bindless must accept an ordinary fixed array and reject a partial array with
Unsupported before native layout creation. That rejection has explicit begin/end markers and one expected diagnostic;
all other native diagnostics remain failures. On the tested MoltenVK runtime the variable model uses its documented
fixed-capacity path. Android exercises the enabled native variable-count layout at the declared allocation maximum.
This fixture reads array data; the separate length-query fixture checks the runtime's array-size translation too.

## Reflected Slang binding verification

With host shader compilation enabled and the chosen Metal or Vulkan test configuration:

```sh
xmake build test-shader-bindings
xmake run test-shader-bindings
```

The default entry compiles four variants of `tests/shaders/shader_bindings.slang`, then runs the shared binding
fixture. It checks exact uint readback, capacity-one and adjacent arrays, sparse descriptor sets, typed uniforms,
push constants, atomic failed resource edits, partial native creation cleanup and layout-aware pipeline reload.
Compatible code edits preserve bindings; array-length and same-sized uniform-type changes preserve the old pipeline
and output until a complete replacement is prepared. It needs no `.vshader` schema, imported scene or material layer.

An embedded executable selecting only `shader_runtime` can run the same source with pre-cooked inputs. Use
`vultra-shader tests/shaders/shader_bindings.slang --entry compute:main` on the host, adding the following defines
and an `--output` path for each archive:

| Archive | Defines |
| --- | --- |
| `bindings-2.vshaderc` | `--define INPUT_COUNT=2` |
| `bindings-3.vshaderc` | `--define INPUT_COUNT=3` |
| `compatible.vshaderc` | `--define INPUT_COUNT=2 --define OUTPUT_OFFSET=2` |
| `changed-type.vshaderc` | `--define INPUT_COUNT=2 --define FLOAT_MULTIPLIER=1` |

Pass those four archive paths in table order, followed by a writable `reload.vshaderc` path whose parent directory
already exists. This mode does not invoke a shader compiler. Keep each run's writable archive separate.

`test-shader-uniform-storage` checks selected GPU constant-buffer ownership with the same host/default convention:

```sh
xmake build test-shader-uniform-storage
xmake run test-shader-uniform-storage
```

For a runtime-only consumer, cook `tests/shaders/shader_uniform_storage.slang` on the host with `--entry compute:main`
and pass that archive as the executable's single argument. Two owned blocks and an external constant-buffer view
produce exact values 16, 93, 27 and 33. Rejected names/sizes and a failure on the second native buffer map preserve the
old result; retry commits both uniforms and the external view together. Partial view creation is also injected.
Identity uploads skip mapping, a single-block edit preserves the other block, and updates reuse buffers/views.
The owned bindings include a nested name and a capacity-one constant-buffer array. Three same-sized CPU blocks with
different scalar type, shape or field offsets reject before any mapping or descriptor update; the old GPU result
remains exact. Full updates use typed blocks, while the partial update also exercises explicit raw-byte input.
Borrowed wrappers support explicit constant-read state declarations. The existing ShaderMaterial regression separately
checks property-upload failure, retained pixels, retry, variant failure and texture-resolution failure.

The storage fixture also prepares shared binding candidates while changing `frame` from borrowed to owned and back.
A failure on the second candidate map preserves the old result, diagnostics and native object counts. The new owned
frame starts at zero and produces 15; a typed snapshot edit and the later borrowed view each produce 33. The numerical
fixture explicitly clamps its float-to-uint input to a valid range for zero-initialized frame data.

The same test creates `ShaderBindings::createInstanceExpected()` instances with independent buffers and descriptors
under the parent's exact native layout. Preparing an instance with different values and a different frame resource
keeps the parent's result at 16; the same pipeline produces 124 with the instance and 16 again with the parent.
Uninitialized instance uploads reject, and partial first/second uniform-view allocation failures retain the parent's
buffers, views and output. Instance destruction releases its storage without destroying the shared layout.

`test-shader-uniform-reconcile` checks a complete replacement after reflected layout changes:

```sh
xmake build test-shader-uniform-block test-shader-uniform-reconcile
xmake run test-shader-uniform-block
xmake run test-shader-uniform-reconcile
```

The CPU fixture compares the entire 432-byte candidate after field reordering, offset/stride changes, nested array
resizing and both matrix-major conversions. Type, shape and array-rank changes reset values; shrinking preserves
the common prefix. Identity reconciliation preserves revision, and invalid layouts preserve values and diagnostics.
The GPU fixture compiles two variants without a `.vshader` material schema. Both uniform layout and descriptor
locations change. Exact readback is `{17,14,12,30}` before replacement, `{17,14,12,0}` after migration and
`{17,14,12,31}` after explicit defaults. Injected map and native pipeline failures retain the old GPU result; retry
publishes a complete new binding/pipeline owner after the previous GPU use completes.
The replacement uses `ShaderBindings::reconcileExpected()` after releasing the original caller-side CPU block.
Editing an independent accepted-value snapshot does not change migrated GPU values. Uninitialized source bindings
and incompatible candidate resources reject without publishing diagnostics. CPU initial-byte import is also checked
for exact size, independent ownership and initial revision zero.

For a `shader_runtime`-only consumer, cook `tests/shaders/shader_uniform_reconcile.slang` on the host twice with
`--entry compute:main --define NEW_LAYOUT=0` and `--entry compute:main --define NEW_LAYOUT=1`, giving each its own
`--output` archive. Pass the old and new archive paths, in that order, as the executable's two arguments. This
mode uses the same GPU fixture without invoking a runtime shader compiler.

## Generic material block verification

Cook the two ordinary Slang layouts on the host, then run the same C++ consumer with those archives:

```sh
xmake build vultra-shader test-material-block
xmake run vultra-shader tests/shaders/material_block.slang --entry compute:main --define NEW_LAYOUT=0 \
    --output build/.tmp/material-old.vshaderc
xmake run vultra-shader tests/shaders/material_block.slang --entry compute:main --define NEW_LAYOUT=1 \
    --output build/.tmp/material-new.vshaderc
xmake run test-material-block build/.tmp/material-old.vshaderc build/.tmp/material-new.vshaderc \
    build/.tmp/material-block-output
```

The test restores self-contained layouts, packed bytes and resource IDs before preparing actual GPU bindings.
It checks all 527 RGBA32F pixels for nested constant-buffer arrays, a matrix, structure arrays, texture arrays and
a sampler. Weighted RGB and alpha exercise distinct values and dynamically uniform constant-buffer indexing;
an instance edit switches the runtime index from one to zero for the entire dispatch. Divergent descriptor indexing
is a separate shader/device contract and is not exercised here.
Independent binding instances retain the same layout but have different buffers and pixels after an element edit.
Layout replacement changes field order, matrix major order, numeric arrays and resource-array capacity; the
constant-buffer array grows from two to three elements and shrinks back to two. New elements are zeroed and can be
edited independently; surviving elements retain their values. Partial, duplicate and out-of-range owned arrays reject.
Rejected late CPU edits, a second-buffer map failure, resolver errors and pipeline-creation failure preserve the
accepted state/output; retry publishes the candidate after GPU completion. Persistence rejects 26 malformed inputs.
The scratch directory is optional; without it, the fixture writes `material-block-output` beside its first archive.
An embedded target selecting only `material_block,texture_upload` can compile the same fixture and consume the
host-cooked archives from an empty directory, without Slang, ANTLR, a scene or game-material metadata.

Lab's `test-android-material-block-activity` runs this same Expected main through the shared worker/join host.
Package the two archives as root assets and put their names on separate lines in `arguments.txt`; no scratch asset
is required. Use the matching library name in the NativeActivity manifest and independently verify loaded Khronos
validation, the result marker and normal worker destruction.

The pinned Slang recipe includes the constant-bool-cast patch needed by expression-sized arrays such as
`NEW_LAYOUT ? 3 : 2`. Rebuild an existing cached Slang package after changing its recipe/patches. The native shader
toolchain fingerprint includes those inputs so host cooks are invalidated when they change; runtime consumers
continue to load checked cooked metadata without compiler code.
For an existing host package cache, use `xmake require -y --force --shallow --build "slang-static 2026.11"` once,
then rebuild the host tool and recook the inputs. The qualified version selects this package explicitly.

## Reflected binary16 parameter verification

The same material and binding APIs accept binary16 parameters from ordinary Slang. Cook both layouts with the host
tool, then execute the source-free fixture:

```sh
xmake build vultra-shader test-shader-half
xmake run vultra-shader tests/shaders/shader_half.slang --entry compute:main --define NEW_LAYOUT=0 \
    --output build/.tmp/half-old.vshaderc
xmake run vultra-shader tests/shaders/shader_half.slang --entry compute:main --define NEW_LAYOUT=1 \
    --output build/.tmp/half-new.vshaderc
xmake run test-shader-half build/.tmp/half-old.vshaderc build/.tmp/half-new.vshaderc
```

CPU checks use independent full-byte oracles for adjacent half fields, mixed 16/32-bit data, packed arrays, both
matrix orders, preserved special-value payloads and atomic rejection. The native compute shader reads half scalar,
vector, array and matrix values alongside float32/uint fields. All 209 RGBA32F pixels are checked before and after
a live numeric edit and complete layout replacement. Numeric edits retain the existing buffer/pipeline. A saved
MaterialBlock is restored before GPU preparation. An independent consumer selecting only `material_block` can build
the same test and run from an empty directory without Slang, ANTLR, game-material metadata, scene or importer.

This test requires native half arithmetic and uniform storage support. The VRI `vulkan_float16_features.patch`
independently enables queried supported arithmetic/storage bits; Vulkan arithmetic support alone is insufficient.
Rebuild cached VRI consumers with the package config before testing. This fixture is separate from
`test-float16-rounding`, which tests explicit conversion algorithms without requiring native half arithmetic.
It does not measure scene performance or qualify an approximate OpenPBR precision policy.

With the host compiler enabled, `xmake test -v test-shader-half/default` compiles both control layouts and runs the
same checks without archive arguments. Cooked-only consumers require the two explicit archive paths above.

## Material function graph authoring

Export an ordinary Slang function from the graph, then use the same shader tool to compile its Surface consumer:

```sh
xmake build vultra-shader test-material-graph test-material-graph-gpu
xmake run test-material-graph
xmake run vultra-shader tests/shaders/material_graph.vmatgraph \
    --output build/.tmp/material-graph/material_graph.slangh
xmake run vultra-shader tests/shaders/material_graph_unlit.vshader \
    --include build/.tmp/material-graph --include builtin/shaders \
    --output build/.tmp/material-graph/unlit.vshaderc
xmake run test-material-graph-gpu tests/shaders/material_graph.vmatgraph build/.tmp/material-graph/native
```

Graph export accepts `--output` and ordinary application options; shader profile/entry/variant/link/compile options
belong to the subsequent shader cook. The default graph output is a `.slangh` sibling. Regenerate that header after
graph edits. Normal Slang includes, dependencies and compatible pipeline reload continue to use the existing owners.

The CPU fixture checks deterministic ordering/pruning, typed literal persistence, 15 structural and 15 document
rejections, duplicate object keys and UTF-8. `python3 tests/build_modules.py material_graph` verifies the CPU-only
feature closure and runs the same fixture. An embedded target selecting only `material_graph` needs no VRI, shader
compiler, scene or rendering module.

The GPU fixture checks all 209 RGBA32F pixels against independent sampled color/alpha arithmetic. A single texture
node returns a struct consumed through two named fields. A node type error reports its ID and retains old pixels,
handle and generation. Native pipeline rejection likewise retains the accepted owner; retry replaces it. A numeric
parameter edit reuses the buffer/pipeline. The fixture saves old/new ordinary program archives in its scratch folder.
An embedded target selecting `material_block,texture_upload` can build the same C++ fixture without the graph or
compiler features and run:

```sh
runtime-material-graph-gpu --cooked OLD.vshaderc NEW.vshaderc SCRATCH_DIRECTORY
```

The existing `test-unlit-surface` also accepts the graph-produced game archive with its usual cooked builtin root and
scratch arguments. Its complete forward/deferred HDR, depth/shadow coverage, clipping, back-face and live-edit oracles
apply unchanged. This verifies the authoring backend and ordinary consumers; it does not establish a visual graph
editor, automatic JSON regeneration, complete material-model/reference integration or Android device performance.

## Slang type-conformance verification

With host compilation enabled, run the shared-binding dynamic-dispatch fixture:

```sh
xmake build test-shader-type-conformances vultra-shader
xmake run test-shader-type-conformances
```

It links two model implementations with different payload field layouts. Four selections cover sparse IDs 3/11,
swapped IDs, one implementation, and IDs 0/UINT32_MAX. A second interface independently uses ID 3. Exact GPU readback
checks 80 uint channels including a uniform-only dispatch edit. Host checks require different program keys while
reusing frontend IR, four reopened disk-cache hits, twelve rejected requests with valid-cache recovery, cooked
round trips and source/native-pipeline reload failure with old-output retention and later recovery. An optional
single argument chooses a fresh writable host output directory; the fixture retains its cooked programs there.

An application selecting only `shader_runtime` can use the same C++ fixture with four host-cooked archives. Cook
`tests/shaders/type_conformances.slang` with `--entry compute:main --link type_conformance_models.slang` and the
following repeated triples:

| Archive | Type-conformance triples |
| --- | --- |
| `selection-0.vshaderc` | `AddModel IMaterialModel 3`, `MultiplyModel IMaterialModel 11`, `AddModel IMarker 3` |
| `selection-1.vshaderc` | `AddModel IMaterialModel 11`, `MultiplyModel IMaterialModel 3`, `AddModel IMarker 3` |
| `selection-2.vshaderc` | `AddModel IMaterialModel 3`, `AddModel IMarker 3` |
| `selection-3.vshaderc` | `AddModel IMaterialModel 0`, `MultiplyModel IMaterialModel 4294967295`, `AddModel IMarker 3` |

For example, each triple is supplied as `--type-conformance AddModel IMaterialModel 3`. Use `--output` for the
corresponding archive, then pass all four paths in table order to the fixture. This mode only loads cooked code;
it performs no source compilation or watching. Unselected dispatch IDs are outside the shader input contract.
These checks exercise the compiler/binding primitive, not a completed general material graph or additional builtin
Surface model.

## Material type-variant verification

With host game-shader compilation enabled:

```sh
xmake build test-material-type-conformances test-shader-asset vultra-shader
xmake run test-material-type-conformances
xmake run test-shader-asset
```

The new fixture compiles three `.vshader` variants with declared conformance sets and one global interface
selection. A uniform edit changes the model without recreating the pipeline. Another variant swaps dispatch IDs
and requires a second storage output; missing resources preserve the old prepared result. The single-model variant
also executes. Three rejected whole-asset reloads cover corrupt bytes, late resource failure and partial native
pipeline creation; the two old material instances and metadata remain accepted. Compatible code, an explicit ID
remap and restoration each publish together and produce exact results. Each backend checks 384 float channels.
Host controls cover five invalid ID spellings, global/variant conflicts, failed-cook artifact retention and variant
stripping. The existing archive fixture now rejects 32 malformed cases, including missing or invalid conformance
metadata, duplicate IDs/implementations and count/name limits.

For a compiler-free `shader_material` consumer, use the same C++ fixture with three cooked assets and a separate
writable root. The archive inputs can be produced with:

```sh
xmake run vultra-shader tests/shaders/material_type_conformances.vshader \
    --type-conformance AddModel IMarker 3 --output build/.tmp/material-base.vshaderc \
    --reflection build/.tmp/material-base.json
```

Cook a second archive with `--define OUTPUT_BIAS=4`. For the third, make a writable copy of the `.vshader` and both
`type_conformance_*.slang` modules, exchange IDs 3 and 11 only in the Default variant, and cook without the bias.
Pass base, biased and remapped archive paths followed by the writable root. This mode performs no source compilation
or watching. The reflection reports must retain each effective type/interface/ID triple, including global inputs;
`--variant OnlyAdd` must retain only that variant and its two effective conformances.

All game `.vshaderc` assets must be recooked for the required per-variant `type_conformances` field. The format stays
at version 1, with no compatibility reader. This check concerns custom shader material models, not additional builtin
Surface models or a complete material graph.

## Buffer length query verification

Configure Metal or Vulkan, then cook both layouts from the same numerical shader:

```sh
xmake build vultra-shader
xmake build test-buffer-dimensions
xmake run vultra-shader tests/shaders/buffer_dimensions.slang --define BUFFER_DIMENSIONS_DIRECT=1 \
    --output build/.tmp/dimensions-direct.vshaderc
xmake run vultra-shader tests/shaders/buffer_dimensions.slang --define BUFFER_DIMENSIONS_DIRECT=0 \
    --output build/.tmp/dimensions-array.vshaderc
xmake run test-buffer-dimensions build/.tmp/dimensions-direct.vshaderc build/.tmp/dimensions-array.vshaderc
xmake run test-shader-compiler
```

The first program uses 26 scalar input bindings; the second uses a fixed descriptor array. On Metal this
exercises direct and argument-buffer paths with three compute and three graphics frames each. Vulkan uses ordinary
sets for both layouts. Exact whole-buffer and RGBA32_UINT image readbacks cover selected views, offset/range and
size-zero views, post-completion descriptor updates, interrupted compute encoders, compatible pipeline switches and
restored nonzero vertex slots. A separate Metal fixture fills all 31 native buffer slots: its size-query fragment
stage must reject creation with the expected slot-exhaustion diagnostic. Preserve and classify that diagnostic;
do not treat other diagnostics as expected. The original nested structured-buffer regression must read 37 in all
four optimization modes. Huge buffers and mesh/task execution are outside this fixture's runtime coverage.
The tested unmodified SDK 1.4.309 MoltenVK passes the direct subset but emits an undeclared identifier for an array
index containing a member access. Its independent SPIRV-Cross CLI reproduces that failure. The local runtime package
backports the relevant expression fix from [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross/commit/896fc8807f985c2c1b941f07d2f7a8cd1ca1752a).
Both complete numerical/image models pass with that package on Apple M4 and loaded Khronos/synchronization
validation. The shaders, comparisons and thresholds are unchanged. The package has a distinct pipeline-cache UUID;
an old runtime's serialized MSL cache must not be accepted after changing the translator.

The companion Lab exposes the identical mains as `test-android-descriptor-bindings` and
`test-android-buffer-dimensions` for standalone ADB use. Prefer the application-context entries below when qualifying
external Android validation: merely requesting a layer in a standalone process does not establish that it loaded.
Neither fixture measures scene frame time or qualifies XR transitions.

## Android application-context GPU tests

Lab also builds `test-android-descriptor-bindings-activity`, `test-android-buffer-dimensions-activity` and
`test-android-graph-heaps-activity` as shared libraries. They reuse the same test mains through one NativeActivity host in
`tests/support/android_gpu_test_activity.cpp`. A worker thread runs the fixture; destruction joins before releasing
the borrowed activity pointer, following the [NativeActivity callback contract](https://developer.android.com/ndk/reference/struct/a-native-activity-callbacks). The debug manifest template is `tests/support/android_gpu_test_manifest.xml.in`.
Its API 33 minimum/target identifies this test context and does not change the production library's Android minimum.

Replace `@PACKAGE@` with a task-owned debug package and `@LIBRARY@` with the shared target name without `lib`/`.so`.
Package with core `scripts/package_android_xr_probe.py --library ... --manifest ... --sdk ... --build-tools ...`.
Add `arguments.txt` and the cooked programs using repeated `--asset APK_NAME=FILE` arguments. Each nonempty line
of `arguments.txt` names one program asset in the APK root or one literal `--` option, in argv order:
`descriptor_bindings.vshaderc`, or
`direct.vshaderc` followed by `array.vshaderc`. An empty file invokes a no-asset fixture with only argv[0]; use
this for `test-android-graph-heaps-activity`. For the LUT fixture, a third `--benchmark` line selects its existing
finite benchmark mode. Options are passed unchanged to the test main; unknown options fail there. The host accepts
at most 16 arguments and 64 MiB per asset,
rejects path traversal, and writes each argument file through the existing Expected atomic writer into private app data.
It does not cook shaders on Android. Input helpers return Expected; mandatory Android callbacks report startup or
lifecycle errors explicitly. These entry points are test infrastructure, not an editor or scene benchmark.

Install the APK over ADB, load the external Khronos layer for that debug package, enable synchronization validation,
and confirm the actual layer-load message in the package's logcat. Retain both logcat and
`files/gpu-test-output.log`: native Android VRI diagnostics use logcat, while the fixture's markers/readback summaries
use the file. The `VultraGpuTest` tag reports STARTED, RESULT (with exit code), and DESTROYED. A successful RESULT
alone is insufficient; require destruction and no unexpected validation diagnostics. The AYN Thor qualification also
uses a recording-only on/off control to establish that the synchronization-validation property changes actual hazard
reporting; the deliberately conflicting commands are discarded without submission. Classify only the exact expected
VRI rejection inside its fixture/activity scope. Restore pre-test GPU debug settings and remove only owned diagnostic
packages/files. Keep the device awake during testing. This host does not provide XR headset or performance acceptance.

## Texture view verification

Cook `tests/shaders/texture_views.slang` with the host shader tool, including these compute entries in one archive:
`sample1D`, `sample1DArray`, `sample2D`, `sample2DArray`, `sample3D`, `sampleCube`, `sampleCubeArray`, `sampleDepth`,
`sampleStencil`, `sampleMS`, and `sampleMSArray`. Use SPIR-V 1.5, as with the queue fixture. Then run:

```sh
xmake build test-texture-views
xmake run test-texture-views /absolute/path/to/texture-views.vshaderc
```

The test independently uploads mip/layer/depth values and samples actual views, checks view dimensions, cube faces,
D32 and separate D32S8 depth/stencil planes on one/two layers and two mips, and each sample of ordinary/array 4x MSAA attachments. Metal's 1D cases use
their legal single mip and constant LOD zero. `test-resources-expected` additionally checks canonical cache reuse,
invalid selections making no native call, partial native failure cleanup, missing-handle errors and retry.
`test-queue-textures` borrows the same texture-owned views across explicit queue submissions.

The sampled stencil declaration explicitly uses `vk::image_format("unknown")`; it must not bake in R32UI or R8UI
for a combined-format image view. Check all native warnings as well as VUIDs and synchronization hazards.
The fixture performs 242 exact value/dimension checks. It also uses independent depth/stencil clear values,
clears one plane while loading the other, binds only stencil, exercises the combined-depth convenience, and
discards each plane while verifying only the defined contents of the retained plane. VultraLab exposes its source as
`test-android-texture-views`, and the failure/recovery fixture as `test-android-resources-expected`, for API 33
standalone runs. These do not replace
the API 29 application. Rebuild consumers for the Texture layout change. Backend view support alone does not establish graph dependencies. Declare graph ranges as described below; native
heap aliasing remains outside this contract.

## Graph inspection and intermediate capture verification

```sh
xmake build test-graph-inspection-expected
xmake run test-graph-inspection-expected
```

This fixture needs no cooked shader. It checks rejected capture/name/query operations, native telemetry
count/list failures and recovery, unknown telemetry, aliased-plan rejection and explicit ordering across inserted
captures. Seven GPU cases compare intermediate/final copies against independent uploads, including integer/HDR,
three mips, arrays/cube arrays, full 3D depth and separate depth/stencil planes. It also discards a recording after
an intermediate copy and retries. Lab exposes the same source as `test-android-graph-inspection-expected` for an
independent API 33 run. Compressed and multisample intermediate captures are not part of this fixture's runtime
qualification. The external stereo fixture separately verifies inspection failure before submission and
exact stereo output recovery.

## Lab stereo lifecycle verification

In the companion VultraLab checkout:

```sh
xmake build test-stereo-renderer-expected
xmake run test-stereo-renderer-expected
xmake run test-stereo-renderer-expected /absolute/path/to/builtins.vpk
```

The optional argument is a cooked built-in shader directory or VPK. Source mode needs the configured shader
compiler and core working directory. This fixture checks candidate rejection, partial native initialization,
recording failure, explicit discard, known-rejected submission/retry, pending mutation guards, preview detach
rejection, pruned reference capture and measurement-only output access against independent exact HDR clears.
Android exposes `test-android-stereo-renderer-expected`; its cooked run explicitly requests prefer-FP16 environment
decoding. It is an offscreen correctness check and does not qualify XR runtime transitions or frame performance.

## Scene ray geometry and policy verification

Use a fresh output directory and a device with native ray-query support:

```sh
RAY_GEOMETRY_DIR="$PWD/build/.tmp/scene-rays"
mkdir -p "$RAY_GEOMETRY_DIR"
xmake run vultra-shader tests/shaders/scene_rays.slang --ray-query \
    --output "$RAY_GEOMETRY_DIR/scene-rays.vshaderc"
xmake build test-scene-rays
xmake run test-scene-rays "$RAY_GEOMETRY_DIR/scene-rays.vshaderc"
```

The fixture observes actual 64-byte VRI instance uploads: independent TLAS policies reference the same nonzero BLAS
addresses while retaining different opacity/culling flags. Twenty-four exact front/back queries cover the ordinary and
candidate-filtered policies, alpha-blend masks, rigid transforms and completed in-place position writes. Allocation
failure/retry, malformed bounds, stale geometry rejection, failed recording, rejected BLAS/TLAS submissions and
restored input states are checked. Native AS object identities and scratch footprints agree after rebuild/retry. The test requires ray query;
capability rejection is not a ray-query execution result.

Lab exposes `test-android-scene-rays-activity` through its common worker/join NativeActivity host. Package the probe
as a root asset, list its name in `arguments.txt`, and use that matching library name in the manifest. Verify loaded
Khronos/synchronization validation and normal destruction separately from the numerical result.

`test-scene-positions` exercises actual position edits through one retained BuiltinRenderer graph, cold/warm shadow
caches and complete HDR/display/four-shadow readbacks. Narrowing the quad changes the image and redraws casters;
unchanged positions reuse shadows, and restoring positions restores all six images exactly. It also retains the
existing built-in append/prepare/record/submit/discard recovery checks.

```sh
xmake build test-scene-positions
xmake run test-scene-positions /absolute/path/to/builtin-shaders.vpk
```

The version-1 VPK contains `builtin/shaders/passes/{environment,shadow,forward,forward_opaque,gbuffer,skybox,
deferred_lighting,tone_mapping}.vshaderc`, cooked by vultra-shader with the builtin shader and external include roots.
Lab exposes the same main as `test-android-scene-positions-activity`; package that VPK as one root asset and list it in
arguments.txt. This raster fixture requires no ray-query or mesh-shader support. Rebuild VRI/consumers with the pinned
`acceleration_scratch_rebuild` package identity before testing notified positions on Vulkan.

## Reference history and recovery verification

In the companion Lab checkout:

```sh
xmake build test-reference-transport
xmake run test-reference-transport /absolute/path/to/reference.vpk
xmake build test-reference-history-expected
xmake run test-reference-history-expected
xmake run test-reference-history-expected /absolute/path/to/reference.vpk
xmake run test-reference-path-tracer
```

The compact fixture uses prepared geometry and an optional VPK containing
`builtin/shaders/passes/path_trace.vshaderc` and `passes/accumulate.vshaderc`. Cook the reference entry with
`vultra-shader --ray-query`; its recorded requirement must match the shader. It compares independent/dependent
FP32 sums and RGBA16F averages against exact constants across eight reference edit causes, rejected preparation,
recording/submit recovery, native partial initialization and rejected shader replacement. The complete reference
fixture retains its furnace, AOV, motion, lighting, deterministic reset and Cornell convergence controls.
Android exposes `test-android-reference-history-expected`. A device without enabled ray query/bindless support
reports a checked unsupported factory; that outcome is separate from rendering qualification.
`test-reference-transport` checks the complete integrator's eight AOVs, emission, mask, mirrored instances, analytic
Lambertian lighting, rigid motion and environment misses using prepared input and optional cooked bytes. Its analytic
bound is the existing full reference fixture's bound. Android builds `test-android-reference-transport`; Metal and
the tested Adreno pass its complete eight-frame prepared fixture. Reference sampled globals occupy set 3, so recook
its VPK together with the matching Lab runtime. Keep this qualification separate from capability rejection, the
exact 15-frame history/recovery fixture, frame-time measurements and XR acceptance. The history fixture passes on
Metal and Adreno with explicit nearest-even half-output publication; its original pre-fix negative is retained.
`test-graph-expected` additionally verifies named pass-restoration failure, remaining restore attempts and refusal
to execute/recompile a graph after incomplete recovery. These are correctness checks, not XR/FPS acceptance.

The standalone half-precision regression requires no ray queries, bindless resources or float16 arithmetic:

```sh
xmake build vultra-shader
xmake build test-float16-rounding
xmake run vultra-shader tests/shaders/float16_rounding.slang \
    --output build/float16-rounding.vshaderc --include builtin/shaders
xmake run test-float16-rounding build/float16-rounding.vshaderc
```

It compares every adjacent finite binary16 midpoint and both neighboring binary32 values, on both signs, against an
endpoint/parity oracle. It checks both binary32 results and actual RGBA16F writes, including subnormals, overflow,
signed zero and NaN classification. Lab exposes `test-android-float16-rounding` for cooked Android diagnostics.

## Pass construction verification

Pass construction has an independent GPU/ownership fixture:

```sh
xmake build test-pass-factories
xmake run test-pass-factories /absolute/path/to/gain.vshaderc
```

Use `gain.vshaderc` produced by `example-offline-cook`. The fixture checks Expected factory/append rejection,
callback-before-owner cleanup, a compiled prefix after failure, partial native pipeline cleanup, recording retry
and exact color readback after numeric edits. Lab's `test-android-pass-factories` uses the same cooked workload.
It also checks atomic group registration, late conflicts in every built-in installer position, retained definition
addresses/factories, candidate factory cleanup, empty groups and retry. Lab's `test-falcor-passes` checks each
Falcor group conflict and image registration rejection before its source/directory/VPK rendering checks.
`test-lab-session` also compares all four bound raster factories with direct HDR/depth construction and checks a
late foreign-device append failure. Rebuild consumers after the GraphPass/PassDefinition C++ layout change.

`test-tone-mapping-bindings` verifies the built-in Pass's shared reflection path using four cooked programs. Prepare
a writable copy of `builtin/shaders/passes/tone_mapping.slang`, use the same logical source path for each cook, and
retain each variant's text separately:

| Archive | Change from the original source |
| --- | --- |
| `base.vshaderc` | None |
| `shifted.vshaderc` | Change the hdrColor binding from `(0, 0)` to `(5, 3)` |
| `uint4.vshaderc` | Change `float4 values` to `uint4 values` |
| `compatible.vshaderc` | Change the final output alpha from `1` to `0.5` |

Cook each copy with `vultra-shader path/to/tone_mapping.slang --entry vertex:screenVertex --entry fragment:fragmentMain
--include builtin/shaders --output path/to/archive.vshaderc`, then run:

```sh
xmake build test-tone-mapping-bindings
xmake run test-tone-mapping-bindings base.vshaderc shifted.vshaderc uint4.vshaderc compatible.vshaderc build/.tmp/tone-run
```

Use a separate writable root for each run. The fixture installs only its cooked tone entry there. A new Pass accepts
set 3/binding 5, while same-sized uint4 push parameters reject typed initialization. An existing Pass rejects both
layout changes without replacing its pipeline, generation or code identity. Compatible code changes alpha to 0.5;
restoring the original archive restores alpha 1. Six exact 5x3 RGBA16F readbacks check 360 channels. This executable
does not compile shaders; it also runs in a `rendering`-feature consumer without the shader compiler or scene
preparation features. Existing `test-shader-surface` covers three tone output formats, independent numerical color
references and native initialization/view/parameter failures.

ToneMappingPass construction and append now use only `create()` and `addPassesExpected()`.
The renderer composition entry is `addToneMappingPassExpected()`. The throwing constructor, `addPasses()` and
`BuiltinRenderer::addToneMappingPass()` are removed; rebuild callers. `test-graph-definition` checks direct/catalog
tone parity through Expected texture creation, graph append/build/compile/record, frame submission and readback.
Its parameter storage outlives graph callbacks, and the existing image tolerances and NaN rejection are unchanged.

## Unlit Surface verification

Build `test-unlit-surface` and `vultra-shader`. Cook `tests/shaders/unlit_surface.vshader` using only
`--include builtin/shaders`. For the engine shader root, cook these files under `builtin/shaders/passes/` to matching
`.vshaderc` siblings, using `--include builtin/shaders --include external`:

| Program | Explicit `--entry` values |
| --- | --- |
| shadow, forward | `vertex:vertexMain`, `fragment:fragmentMain` |
| gbuffer | `vertex:vertexMain`, `fragment:fragmentBase`, `fragment:fragmentMaterial` |
| skybox, tone_mapping, deferred_lighting | `vertex:screenVertex`, `fragment:fragmentMain` |
| environment | `vertex:screenVertex`, `fragment:diffuseMain`, `fragment:specularMain`, `fragment:brdfMain` |

Pass the cooked material, engine shader root and a writable scratch directory:

```sh
xmake run test-unlit-surface /absolute/unlit.vshaderc /absolute/cooked /absolute/scratch
```

The test uses Expected entry points and a programmatic scene, without importing assets. Full-image HDR and depth
readback checks indexed Forward/Deferred parity, independent radiance values, light/ambient/IBL invariance,
zero-scattering GBuffer data, debug modes 1–5, mirrored/back faces, direct DepthOnly coverage, numeric and sampled
alpha clipping, shadow removal and numeric recovery without pipeline recreation. The two-texel alpha texture removes
exactly half the quad. HDR tint edits also check negative-component clamping against independent pixel values.
This fixture can also be built by an independent consumer selecting only `rendering` and run
from an empty working directory using those cooked files. That mode requires no shader compiler, ANTLR, scene
importer or scene preparation. It does not exercise the optional MeshForward pipeline or establish Unlit support in
the imported-material visibility/reference paths.

Game archives now require explicit Surface metadata (or null for Pass-only assets); recook old game archives.
The archive test includes 48 malformed schema cases, failed-save retention and byte-exact recovery/round trips.

## Graph-file persistence verification

Graph-file persistence has an independent CPU test:

```sh
xmake build test-graph-persistence-expected
xmake run test-graph-persistence-expected build/.tmp/fresh-graph-persistence
```

The output directory must not exist. It checks version-1 UTF-8/numeric round trips, malformed/oversized input,
Expected file failures, atomic-save preservation and retry. `test-graph-definition` additionally compares direct
C++ and file-defined GPU outputs. Lab's Android `test-android-graph-persistence-expected` target uses the same CPU
workload with an explicit writable fresh directory; it requires no graphics context or GPU validation layer.

## Texture range state and rollback verification

Build/run `test-resources-expected` and `test-graph-expected` with the selected Metal or Vulkan backend.

The resource fixture checks range/aspect/dimension rejection before native commands, exact barrier coverage and
old states, repeated read/write behavior, descriptor-free transitions, and owner-bound snapshot restoration.
Its barrier recorder is a CPU boundary check; it is not GPU data verification. The graph fixture seeds mixed mip
states, injects a recording failure, discards/restores an unsubmitted checkpoint and retries the recording.
That mixed-state callback intentionally reads no texels.

For GPU data verification, run `test-queue-textures` in both modes above: the same Texture tracker now covers all
three mips/two layers through graphics, compute and transfer. The existing `test-rendering` additionally checks
selected IBL mip readback at mips 0, 4 and 8. `test-visibility-raster` requires cooked raster/resolve shader paths;
Lab's `test-graph-history`, `test-falcor-passes` and `test-reference-path-tracer` exercise affected callers.
Rebuild all consumers after the Texture and graph-checkpoint layout changes.

VultraLab exposes `test-android-graph-expected` alongside the resource, queue, view and history fixtures for API 33
standalone builds. These do not replace the API 29 application. Standalone execution is not proof that Khronos
loaded: use a debuggable APK with the layer configured and require a positive loader message. Check native warnings
as well as VUIDs/hazards. Native range tracking and graph range dependencies are separate checks; both are exercised
below. Independent aspect states, heap aliasing, automatic queue scheduling and resource retirement remain separate work.

## Graph subresource verification

Build and run `test-graph-subresources` without shader assets on Metal/MoltenVK, or build the same source as
VultraLab's `test-android-graph-subresources` for API 33. It checks invalid ranges before allocation, independent
producer/lifetime selection, overlapping declarations, read-before-write, whole-resource export coverage and
buffer/history restrictions. Actual R32_UINT transfer/readback covers 2D arrays, single-mip 1D arrays, 3D full-depth
mips and cube faces: 30 mip/layer cells and 3,952 exact texel checks across initial output, unchanged contents after
failed/discarded recording, retry and full-shape capture. One pass reads mip zero and writes mip one, or reads and
writes different layers for the 1D case. The old producers of only those overwritten cells must be culled.

Native Metal's unsupported 1D/1D-array mip chains must return Unsupported with no handle. The pinned
`metal_texture_dimensions.patch` prevents the prior Metal validation assertion before allocation. Preserve the
initial failure and use a matching package/header/library rebuild; changing a patch without a new package identity
can otherwise keep an old cached binary. Ordinary `test-graph-expected` and `test-graph-resources` cover the preceding
failure/history/allocation contracts. Graph reports retain zero counts as the documented remaining-range selection.
These checks do not establish independent depth/stencil state, all-format pipeline conformance, heap aliasing,
multi-queue retirement or renderer speedups.

## Selective history verification

Build and run `test-history-invalidation` with the selected Metal or Vulkan backend. It requires native
storage-texture clear support and uses exact GPU readback, without external shader assets. It verifies independent
history reset/retention, allocation/view identity, atomic invalid-selection rejection, empty/duplicate selections,
recording/submission failure and discard/checkpoint recovery, stale checkpoints, activation of a culled history,
and whole-graph reset. Rebuild all consumers after the RenderGraph metadata/checkpoint layout change.

VultraLab exposes the same source as `test-android-history-invalidation` for standalone Android verification.
This target is separate from the application APK. Its `test-falcor-passes` additionally checks two independent
accumulation chains against analytic frame averages after selective and whole-graph resets.

## Storage texture clear verification

Configure the host graphics backend and enable library tests. No external shader archive is needed: this checks
the backend command, including its internal Metal kernels. Each supported case uploads sentinels to every mip and
layer/depth plane, clears mip 0 to typed values and then zero, and checks complete readback after each submission.
Three 32-bit value types cover 1D, arrays, 2D, 3D, cube and cube-array shapes; additional 2D cases cover narrow,
normalized, sRGB, half and packed formats. Unsupported optional formats are printed explicitly. Required typed
shape coverage cannot be skipped.

```sh
xmake build test-storage-texture-clear
xmake run test-storage-texture-clear
```

Metal and MoltenVK on the tested Apple M4 each pass 62 cases. The Adreno 740 standalone Vulkan run passes 59;
BGRA8 UNORM/sRGB storage and its additional fractional-sRGB case are reported unsupported. Android Khronos-layer
loading remains unverified for that shell executable. VultraLab exposes it as `test-android-storage-texture-clear`;
this API 33 shell target does not replace the API 29 application. The existing `test-compute-encoder-state` also
repeats dispatch after a storage clear without rebinding, checking preservation of user compute state.
Graph history/reset and the library's graph-created texture restrictions have separate tests and contracts.

## OpenPBR atlas verification

Use a fresh ignored directory and the configured host graphics backend. This checks the complete FP32 upload against
the frozen atlas format and exercises all eight tables against metadata-driven sampling. It preserves the original
lookup values and interpolation; it is not a visual-quality approximation.
The public factory is `createOpenPbrLutsExpected(Device&)`; the old throwing wrapper is removed. The existing
`test-shader-program` also checks staging-map failure diagnostics and successful retry with identical atlas pixels.

```sh
LUT_CHECK_DIR="$PWD/build/.tmp/openpbr-lut-contract"
python3 tests/generate_openpbr_atlas.py "$LUT_CHECK_DIR/frozen-atlas.rgba32f"
xmake build vultra-shader
xmake run vultra-shader "$PWD/tests/shaders/openpbr_luts.slang" \
    --include "$PWD/builtin/shaders" --include "$PWD/external" \
    --output "$LUT_CHECK_DIR/probe.vshaderc"
xmake build test-openpbr-luts
xmake run test-openpbr-luts "$LUT_CHECK_DIR/probe.vshaderc" "$LUT_CHECK_DIR/frozen-atlas.rgba32f"
```

VultraLab exposes the same source as `test-android-openpbr-luts`. The standalone executable was checked on Android 13
with an API 33 configuration; the application still builds for API 29. The observed standalone shell run passed its
byte and sampling checks without verified validation-layer loading. Loaded-layer application checks are separate evidence;
the standalone success message does not establish layer coverage.
The same main is also available as `test-android-openpbr-luts-activity`, using VultraLab's worker/join host.
Package `arguments.txt` with two asset names, one per line: the cooked probe and the frozen atlas. Put both assets
at the APK root. Set the manifest library to `test-android-openpbr-luts-activity`. API 33 application-context checks
verify the full upload and all 32,768 samples with actually loaded Khronos/synchronization validation and normal
worker destruction. Enable and verify the layer separately; a passing data comparison alone does not prove it loaded.

## Packed native graph regions

`test-graph-heaps` exercises disjoint native regions and dependency-ordered reuse at a nonzero offset in addition
to the existing alias/image/history tests. The mixed packing fixture keeps two buffers and array/3D mip chains in
one compatible heap, finishes every writer before readback, and checks both single-buffer recording and six
recording destinations on three caller-submitted queues. Its GPU waits/signals are explicit. Resource/heap
retention remains the caller's responsibility; these checks do not measure physical queue overlap or frame rate.

For the depth/stencil sample-zero resolve fixture, cook `tests/shaders/depth_resolve.slang` to a
`depth-resolve.vshaderc` archive with the host shader tool and build `test-depth-resolve`.
Run the test with the archive path as its argument. It writes distinct depths and stencil values to
the four samples and reads back each resolved aspect for D16, D32, S8 and D32S8. It also probes D24S8
when the device reports support. The fixture covers the selected array layer and target mip, retained
source samples, explicit waits before transfer readback and discarded recording recovery.

In Lab, build/run `test-graph-heaps-report`, `test-buffer-ranges-report`, `test-depth-history-report`,
`test-graph-history` and `test-gpu` (run with `--offline`) with the selected Metal or Vulkan profile. They consume the checked
`graphReportExpected()` API; all report formats stay at version 1. Native heap requests and per-resource offsets/
footprints are separate from unknown resident allocation measurements. The Android Activity target uses the same
core heap fixture and writes the same readback summaries through the common worker host.

## Buffer readback

With the selected Metal or Vulkan configuration:

```sh
xmake build test-buffer-readback
xmake run test-buffer-readback
```

No shader or scene assets are needed. The test checks exact odd-byte intervals, mixed access-state restoration
limited to the selected interval, recording failure/discard/retry, timeout and map retry, repeated reads and
completion before staging destruction. An explicit compute-queue copy producer/fence chain feeds the graphics
readback. It does not measure physical queue overlap or use a compute shader.

VultraLab exposes the same main as `test-android-buffer-readback-activity`, using the existing worker/join host.
Package its shared library with that manifest library name and an empty `arguments.txt` asset. Verify actual
Khronos/synchronization-layer loading and normal worker destruction separately from the exact-data success line.

## Graph buffer snapshots

```sh
xmake build test-graph-buffer-capture
xmake run test-graph-buffer-capture
```

This asset-free fixture compares 32,753 completed GPU bytes per backend. It covers complete, selected and tail
snapshots before later writes, selects the last overlapping writer, rejects uninitialized neighboring bytes for
whole-buffer copies, and retries discarded recording without changing preceding snapshot contents. Nine finite
generations exercise ordinary allocations, actual identical-resource reuse and actual native-heap overlap.
The final heap generation records on caller-supplied graphics/compute/transfer destinations with explicit GPU
fence waits/signals. These copies establish ordering and lifetime behavior, not physical asynchronous overlap.
`test-graph-inspection-expected` retains the full texture-capture checks. Lab's `test-session-expected` consumes a
graph-created buffer snapshot through the marked-output readback interface and checks live edits/history recovery.

The Android target is `test-android-graph-buffer-capture-activity`, exposed by Lab's common worker/join host.
Package its shared library with that manifest library name and an empty `arguments.txt`. It needs no shader or
scene assets. Check exact data, actually loaded Khronos/synchronization validation and normal destruction.

## Buffer history

```sh
xmake build test-buffer-history
xmake run test-buffer-history
```

The asset-free fixture checks 6,714 actual GPU bytes across seven completed graph frames and discarded work. It verifies
owned arbitrary/zero initial data, mixed texture/buffer selection atomicity, cross-frame retention, selected
overlapping reset epochs, neighboring contents,
dependency/feedback invalidation, stable allocation identities, native-heap exclusion, callback/discard/retry and
an explicit initial-graphics/compute-reader/graphics-writer fence chain. Allocation/map rejection preserves a
compiled graph prefix. This establishes history ordering/data contracts, not shader dispatch overlap or frame rate.

Lab exposes the same fixture as `test-buffer-history-report`, adding range/initial-allocation JSON checks and
unsupported-telemetry coverage. Both Activity targets, `test-android-buffer-history-activity` and
`test-android-buffer-history-report-activity`, use the worker/join host. Package the matching shared library and
manifest library name with an empty `arguments.txt`; no shader or scene assets are needed. Verify actual loaded
Khronos/synchronization validation and normal destruction separately from the data success line.
