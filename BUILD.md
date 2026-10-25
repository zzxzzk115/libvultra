# Build and verification

For complete minimal application code, start with [get_started.md](docs/get_started.md).

## Prerequisites

- Windows x64: Visual Studio 2022 with the C++ toolchain.
- Linux x86_64: C++23 compiler, CMake, pkg-config, Python 3, Vulkan development files,
  X11/Wayland development libraries, wayland-protocols and xkbcommon.
- [xmake](https://xmake.io/guide/quick-start.html#installation) on PATH.
- Vulkan 1.3 GPU/driver for rendering. Ray tracing, ray queries and mesh shading require their device features.
- Python/libclang and clang-format for API regeneration only. Ordinary builds use checked-in bindings and parsers.

Dependency versions and static package recipes live in [external/xmake.lua](external/xmake.lua) and
[external/packages](external/packages/packages). The initial source build of Slang takes time.
MSVC targets and packages use matching MT/MTd settings. Preserve the [VRI patches](external/vri/README.md).
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
PNG outputs include the triangle, uploaded checker, 321 × 237 scene/final output and a 64 × 64 test pattern. Android
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
compiler and watching. `game_shader_runtime` loads cooked game materials/variants and typed instances;
`shader_material` adds their GPU bindings/pipelines and `ShaderRuntime` snapshot ownership without a compiler,
parser, importer, scene or window. `ShaderRuntime::reload()` explicitly replaces cooked assets transactionally;
source polling/`compiling()` is available when `game_shader` is also selected. Compile options apply to source
assets; cooked programs retain their archived target/layout.
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
