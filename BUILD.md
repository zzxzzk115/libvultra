# Build and verification

For minimal application code, start with [get_started.md](docs/get_started.md). Modules and concepts are described
in [architecture.md](docs/architecture.md).

**Verified platforms:** Windows x64 with Vulkan. macOS arm64 (Apple M2) with Vulkan through MoltenVK and with Metal:
the test suite passes on both, except `test-meshlets` on Vulkan, which MoltenVK cannot run without mesh shading;
the examples were not run there. Linux, Android and Web were not rebuilt after the module restructuring on this
branch; their instructions below are kept as instructions and marked where they apply.

## Prerequisites

- Windows x64: Visual Studio 2022 with the C++ toolchain. Run xmake from PowerShell or cmd; under Git Bash it
  detects the platform as mingw.
- Linux x86_64 (not re-verified on this branch): C++23 compiler, CMake, pkg-config, Python 3, Vulkan development
  files, X11/Wayland development libraries, wayland-protocols and xkbcommon.
- macOS arm64: Xcode with its command line tools. Metal, the default there, needs nothing else. The Vulkan
  backend (`--libvultra_graphics_api=vulkan`) needs the LunarG Vulkan SDK with `VULKAN_SDK` pointing at its
  `macOS` directory; the loader is linked from there. Its programs therefore run only where that SDK is
  installed, unless they sit in an application bundle that carries `libvulkan.1.dylib` and `libMoltenVK.dylib` in
  `Contents/Frameworks` and a MoltenVK driver manifest in `Contents/Resources/vulkan/icd.d`: the library is
  linked with an `@executable_path/../Frameworks` rpath for that.
- [xmake](https://xmake.io/guide/quick-start.html#installation) on PATH.
- A Vulkan 1.3 GPU and driver, or a Vulkan 1.2 driver exposing dynamic rendering and synchronization2 as
  extensions (MoltenVK). Ray tracing, ray queries and mesh shading require their device features.
- clang-format and clang-tidy for the development checks.

Dependency versions and package recipes live in [external/xmake.lua](external/xmake.lua) and
[external/packages](external/packages/packages). The initial source build of Slang takes time. MSVC targets and
packages use matching MT/MTd runtimes. VRI is the tagged [v0.2.1 release](external/vri/README.md), without local patches.

## Configure and build

Run from the repository root:

```sh
xmake f -m release -y
xmake build -y --all
xmake run example-basics vri --frames 60
```

A root build defaults to all modules, all examples and the tests. Configuration persists between runs.
`xmake run` builds its target first and runs examples with the repository root as working directory.

| Option | Purpose |
| --- | --- |
| `libvultra_features` | Comma-separated modules: `core`, `platform`, `gpu`, `shader`, `graph`, `assets`, `render`, `ui`, `app`, `xr`, or `all`. Dependencies are added automatically. Root default `all`, embedded default `gpu`. |
| `libvultra_shader_compiler` | Default on. `n` keeps only the cooked shader runtime: no Slang, no shader hot reload. |
| `libvultra_asset_import` | Default on. `n` keeps asset values and VPK archives without importers or meshlet building. |
| `libvultra_with_openxr` | Default on. `n` leaves `xr` out of `all`; selecting `xr` explicitly requires it. |
| `libvultra_graphics_api` | `metal` (default on macOS) or `vulkan` there; `vulkan` on other native platforms; WebAssembly requires `webgpu` or `webgl2`. |
| `libvultra_vri_dir` | Build VRI from a local checkout instead of the pinned release. |
| `libvultra_build_examples`, `libvultra_build_tests` | Default on at the root, off when embedded. |
| `libvultra_examples` | `all`, a group (`basics`, `offline`, `ui`), a mode (`basics.vri`) or a comma-separated union. |
| `libvultra_example_pack` | Host-cooked VPK embedded in the Web `basics.vri` / `basics.graph` examples. |

Selected examples add the modules they need. The repository tests require `render`, `ui`, `app`, the shader
compiler and asset import, so a reduced selection must disable them:

```sh
xmake f --libvultra_features=app --libvultra_build_tests=n --libvultra_examples=basics.vri -y
xmake run example-basics vri --frames 3 --capture build/.tmp/triangle.png
```

Unknown feature or example names fail configuration. [scripts/build_features.lua](scripts/build_features.lua) and
[examples/selection.lua](examples/selection.lua) are the authoritative lists. Restore the full build with:

```sh
xmake f --libvultra_features=all --libvultra_examples=all --libvultra_build_tests=y -y
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

Select modules at configuration, for example `xmake f --libvultra_features=app,graph,shader`. Embedded builds do
not build this repository's examples, tests or tools, and leave the parent's editor settings alone.

Passes that compile library shaders from source (`ForwardPath`, `Environment`, `ToneMappingPass`, `TextureBlit`)
resolve `builtin/shaders` and `external` relative to the working directory, so such programs run with the
libvultra checkout as working directory. The alternative is a cooked pack passed to those classes as an
`AssetSource`.

## Examples and tools

| Target | Arguments |
| --- | --- |
| `example-basics` | `<mode> [--frames N] [--capture file.png]`; modes `window`, `vri`, `graph`, `mesh-shading`. |
| `example-offline` | `[--frames N] [--output dir] [--graph file.vgraph] [--shaders pack.vpk] [--gain x]`. |
| `example-offline-cook` | `<repository-root> <fresh-output-directory> [spirv_1_5\|wgsl]`; writes `project.vpk`. |
| `example-ui` | `[--frames N] [--capture file.png]`. |
| `example-xr` | `[--frames N] [--capture file.png]`; frames the runtime asked to render, capture of the left eye. |
| `vultra-shader` | `<source.slang> --output file.vshaderc [--profile spirv_1_5\|wgsl] [--entry stage:name] [--include dir] [--define NAME=VALUE] [--capability name] [--link module] [--ray-query] [--reflection file.json]`. |
| `vultra-import` | `<model>` (`.gltf`, `.glb`, `.fbx`, untextured `.obj`); bakes the derived asset cache without a GPU. |
| `vultra-builtins` | `<library-root> <output.vpk>`; cooks the library shaders into a VPK. |

All but `example-offline-cook` and `vultra-builtins` accept `--help`, `--log-level` and `--log-file`;
`vultra-import --help` lists its cache, texture and FBX options. The tools exist only in a root build with the
shader compiler or asset import enabled.

## Offline rendering

[example-offline](examples/offline/offline.cpp) renders without a window: a triangle through VRI, a project
compute pass, and a display conversion, built either in C++ or from a `.vgraph` definition.

```sh
xmake run example-offline --frames 3 --output build/.tmp/offline-cpp
xmake run example-offline --frames 3 --graph examples/offline/color_gain.vgraph --output build/.tmp/offline-graph
```

The output directory must not exist; it receives `linear.pfm` and `display.png`. `--gain` applies to the C++ path;
a graph definition owns its parameters. The same program accepts host-cooked shaders from a VPK, in which case
`--graph` names an entry of that pack:

```sh
xmake run example-offline-cook . build/.tmp/offline-pack
xmake run example-offline --shaders build/.tmp/offline-pack/project.vpk --graph color_gain.vgraph \
    --output build/.tmp/offline-packed
```

Each `.vshaderc` holds one target's code and reflected layout; cook SPIR-V and WGSL into separate packs.

## GPU selection

`Device` convenience constructors probe the adapters and reject candidates that lack a required feature or a
graphics queue. Eligible devices are ranked by type (discrete, integrated, other), then dedicated VRAM, then
exposed queues; the startup log records the selected GPU, VRAM, queue counts and feature mask. Required features
are never dropped from a strict request. An explicit `VriDeviceCreationDesc` keeps its requested adapter.
`xmake test -v test-device-selection/*` covers selection and unsupported-feature rejection.

## Desktop, HiDPI and XR

`EditorGui` scales fonts and spacing by the monitor content scale and embeds the Lato Latin font; retain its
[SIL OFL license](resources/ui/LICENSE.txt) when distributing it. ImGui layouts are stored per application name
under `.vultra/<appName>/imgui.ini` in the run directory.

On Linux (not re-verified on this branch) the window system is selected per process:

```sh
VULTRA_WINDOW_SYSTEM=wayland xmake run example-ui --frames 60
VULTRA_WINDOW_SYSTEM=x11 xmake run example-ui --frames 60
```

The Linux file dialog uses the `xdg-desktop-portal` backend of Native File Dialog Extended.

The `xr` module needs an active OpenXR runtime. An `XRFrame` carries both eye views and, per hand, the grip
and aim poses with the select and squeeze values; bindings are suggested for the Khronos simple controller and
the Oculus Touch controller. [example-xr](examples/xr/main.cpp) draws a floor, a car, both hands and a pointer
beam per controller for each eye. It has no window: after `--frames` rendered frames it asks the runtime
to end the session with `OpenXRSession::requestExit()` and keeps its frame loop until the session has stopped;
without `--frames` it runs until the runtime ends the session.

```sh
xmake run example-xr --frames 120 --capture build/.tmp/xr/left.png
```

This was run on Windows with the Meta XR Simulator (v83) as the active runtime: 120 frames were submitted, both
controllers were located in every frame, and the captured left eye showed the scene, the hands and the beams
reaching the floor. No headset was used, and nobody operated the simulated controllers, so the select and
squeeze values were only ever read as zero. The simulator reports the aim pose rolled by 180 degrees about
the pointing direction, which leaves the pointer ray unchanged; the example places the hand model from the grip
pose. At shutdown, after the session stopped in order, the simulator still reports spaces of its own as undestroyed,
leaves one command pool it created on the application's device, and waits ten seconds for its own front end
before `xrDestroyInstance` returns. The validation layer reports that pool and no other object when the device
is destroyed, and every space the module creates is destroyed successfully.

## Android and Web

The Android commands below and the platform probe at the end of this section were run; the Web commands were
not rebuilt on this branch after the module restructuring, so treat those as a starting point. `PlatformApp`
resolves to `AndroidApp` (GameActivity) or `WebApp` on these platforms, and
[examples/common/entry.cpp](examples/common/entry.cpp) shows the `android_main` and `main` entries. Only the
`example-basics` modes `window`, `vri` and `graph` and `example-offline` have platform entries. Shaders are
cooked on the host and loaded from a `project.vpk`, so the device builds disable the compiler and importers.

Cook the pack with a native build first (add `wgsl` as a third argument for WebGPU):

```sh
OUT="$PWD/build/.tmp/platform-examples"
xmake run example-offline-cook . "$OUT/pack"
```

Android, with the NDK and SDK installed:

```sh
ABI=arm64-v8a
XMAKE_CONFIGDIR="$OUT/android-config" xmake f -y -p android -a "$ABI" -m release \
    --ndk="$ANDROID_NDK_HOME" --ndk_sdkver=29 --runtimes=c++_static -o "$OUT/android" \
    --libvultra_features=app --libvultra_examples=basics.window,basics.vri,basics.graph,offline \
    --libvultra_shader_compiler=n --libvultra_asset_import=n \
    --libvultra_build_tests=n --libvultra_with_openxr=n
XMAKE_CONFIGDIR="$OUT/android-config" xmake build -y example-basics example-offline
xmake lua scripts/package_android.lua "$OUT/android/android/$ABI/release/libexample-basics.so" \
    "$OUT/pack/project.vpk" "$OUT/apk" --sdk "$ANDROID_SDK_ROOT" --gradle "$GRADLE" --abi "$ABI"
```

`$GRADLE` is a Gradle 9.6 launcher, such as the one Android Studio downloads to
`~/.gradle/wrapper/dists/gradle-9.6.0-bin/<hash>/gradle-9.6.0/bin/gradle`; the project uses Android Gradle
plugin 9.5.1 and compiles against API 37. Start a mode with
`adb shell am start -n org.vultra.example/com.google.androidgamesdk.GameActivity --esa vultra.arguments vri,--frames,120`.

[scripts/package_android.lua](scripts/package_android.lua) packages already built files into a debug-signed APK
with the newest build tools installed in the SDK, and builds nothing itself; `--application-id` and `--manifest` select another application, such as the offline example
with [its manifest](examples/offline/android_manifest.xml).

Web, after activating Emscripten (`webgl2` consumes the SPIR-V pack, `webgpu` the WGSL pack):

```sh
XMAKE_CONFIGDIR="$OUT/web-config" xmake f -y -p wasm -a wasm32 -m release -o "$OUT/web" \
    --libvultra_graphics_api=webgpu --libvultra_features=app \
    --libvultra_examples=basics.window,basics.vri,basics.graph --libvultra_example_pack="$OUT/pack-wgsl/project.vpk" \
    --libvultra_shader_compiler=n --libvultra_asset_import=n \
    --libvultra_build_tests=n --libvultra_with_openxr=n
XMAKE_CONFIGDIR="$OUT/web-config" xmake build -y example-basics
```

Serve the generated `.html`, `.js` and `.wasm` together over HTTP and pass the mode as `?arg=<mode>`.

[tests/platform](tests/platform/xmake.lua) is a separate xmake project with finite offscreen probes
(`platform-pack` on the host, then `platform-render`, `platform-vpk-memory` and `platform-shader-runtime` with
`--platform_pack=<project.vpk>`). Run xmake in that directory with `-P .`, or it configures the library around
it. The Android probe needs no Gradle:

```sh
cd tests/platform
XMAKE_CONFIGDIR="$OUT/host-config" xmake f -P . -y -m release -o "$OUT/host" \
    --libvultra_build_tests=n --libvultra_build_examples=n
XMAKE_CONFIGDIR="$OUT/host-config" xmake build -P . -y platform-pack
XMAKE_CONFIGDIR="$OUT/host-config" xmake run -P . platform-pack "$PWD" "$OUT/pack"
XMAKE_CONFIGDIR="$OUT/android-config" xmake f -P . -y -p android -a "$ABI" -m release \
    --ndk="$ANDROID_NDK_HOME" --ndk_sdkver=29 --runtimes=c++_static -o "$OUT/android" \
    --platform_pack="$OUT/pack/project.vpk" --libvultra_features=shader,graph,assets,platform \
    --libvultra_shader_compiler=n --libvultra_asset_import=n \
    --libvultra_build_tests=n --libvultra_build_examples=n --libvultra_with_openxr=n
XMAKE_CONFIGDIR="$OUT/android-config" xmake build -P . -y platform-render
cd ../..
xmake lua tests/platform/package_android.lua "$ANDROID_SDK_ROOT" "$ABI" \
    "$OUT/android/android/$ABI/release/libplatform-render.so" "$OUT/pack/project.vpk" "$OUT/apk"
adb install -r "$OUT/apk/platform-probe.apk"
adb shell am start -n org.vultra.platformprobe/android.app.NativeActivity
adb logcat -d -s Vultra    # PLATFORM_RENDER_PASSED and the path of the image it wrote
```

This was run from a Windows host for `x86_64` with NDK 30.0.16248370 and build tools 36.0.0: the probe
installed and passed on the Android emulator (API 37, Vulkan through the host GPU), and its image was read
back. `example-basics` was built and packaged through Gradle the same way; its `vri`, `graph` and `window` modes
ran their frames on the emulator and the triangle was seen on its screen.

For `arm64-v8a` the same was run on an AYN Thor (Snapdragon 8 Gen 2, Adreno 740, Android 13) over wireless
debugging: the probe and `example-offline` passed, and the three modes of `example-basics` ran their frames.
With the device's forced landscape the image on its screen was turned by 90 degrees: VRI v0.2.1 passes the
surface's current transform as the swapchain's pre-transform. A build against a VRI checkout that declares the
identity transform (`--libvultra_vri_dir`) showed the triangle upright on the device; the pinned release does
not have that fix yet. Offscreen results are not affected.

Every packaging makes a new debug key, so `adb install -r` refuses to replace an application packaged earlier;
run `adb uninstall <application id>` first. The Web probes were not run.

## Development checks

```sh
xmake build -y --all
xmake test -v
xmake lua tests/build_modules.lua
clang-format --dry-run --Werror path/to/changed.cpp
clang-tidy -p . path/to/changed.cpp
```

- `xmake test` runs the `test-*` targets in [tests/xmake.lua](tests/xmake.lua); select one with
  `xmake test -v test-render-path/*` or a group with `xmake test -v 'test-graph-*/*'`. A test fails when its output
  contains a GPU validation error, even if the program exits successfully; the full output is kept under
  `build/.tmp/gpu-diagnostics/`.
- `test-camera/offline` and `test-gui-settings/offline` are windowless variants of those tests.
- `xmake lua tests/build_modules.lua [module...]` builds the library once per module, each selected alone, in
  `build/.tmp/modules/<module>` with its own configuration. It reads platform, architecture, mode and
  `libvultra_vri_dir` from the main configuration, so configure the root project first.
- clang-tidy uses the root `compile_commands.json`, which xmake regenerates for the current configuration.

Rendering changes need finite-frame runs, validation-layer checks and image readback; compilation or a successful
exit code alone does not prove correct rendering. Keep verification output under `build/.tmp/`.

Run `./scripts/setup_vscode.ps1` or `sh scripts/setup_vscode.sh` for clangd and shader editor settings, and
dot-source `scripts/setup_xmake_completion.ps1` or source `scripts/setup_xmake_completion.zsh` for shell
completion. Personal settings, compilation databases and captures stay out of source control.
