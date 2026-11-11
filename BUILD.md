# Build and verification

For minimal application code, start with [get_started.md](docs/get_started.md). Modules and concepts are described
in [architecture.md](docs/architecture.md).

**Verified platform:** Windows x64 with Vulkan. Linux, macOS, Android and Web were not rebuilt after the module
restructuring on this branch; their instructions below are kept as instructions and marked where they apply.

## Prerequisites

- Windows x64: Visual Studio 2022 with the C++ toolchain. Run xmake from PowerShell or cmd; under Git Bash it
  detects the platform as mingw.
- Linux x86_64 (not re-verified on this branch): C++23 compiler, CMake, pkg-config, Python 3, Vulkan development
  files, X11/Wayland development libraries, wayland-protocols and xkbcommon.
- [xmake](https://xmake.io/guide/quick-start.html#installation) on PATH.
- A Vulkan 1.3 GPU and driver. Ray tracing, ray queries and mesh shading require their device features.
- Python 3 for `tests/build_modules.py`; clang-format and clang-tidy for the development checks.

Dependency versions and package recipes live in [external/xmake.lua](external/xmake.lua) and
[external/packages](external/packages/packages). The initial source build of Slang takes time. MSVC targets and
packages use matching MT/MTd runtimes. VRI is pinned by commit with [local patches](external/vri/README.md).

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
| `libvultra_graphics_api` | `vulkan` (default, native platforms); WebAssembly requires `webgpu` or `webgl2`. |
| `libvultra_vri_dir` | Build VRI from a local checkout instead of the pinned archive; no patches are applied to it. |
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

The `xr` module needs an active OpenXR runtime and a device. No runtime or headset was tested on this branch.

## Android and Web

Not rebuilt on this branch after the module restructuring; treat the commands as a starting point. `PlatformApp`
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
python3 scripts/package_android.py "$OUT/android/android/$ABI/release/libexample-basics.so" \
    "$OUT/pack/project.vpk" "$OUT/apk" --sdk "$ANDROID_SDK_ROOT" --gradle "$GRADLE" --abi "$ABI"
```

[scripts/package_android.py](scripts/package_android.py) packages already built files into a debug-signed APK and
does not call xmake; `--application-id` and `--manifest` select another application, such as the offline example
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
`--platform_pack=<project.vpk>`). It was updated by inspection for the new modules and has not been built.

## Development checks

```sh
xmake build -y --all
xmake test -v
python tests/build_modules.py
clang-format --dry-run --Werror path/to/changed.cpp
clang-tidy -p . path/to/changed.cpp
```

- `xmake test` runs the `test-*` targets in [tests/xmake.lua](tests/xmake.lua); select one with
  `xmake test -v test-render-path/*` or a group with `xmake test -v 'test-graph-*/*'`. A test fails when its output
  contains a GPU validation error, even if the program exits successfully; the full output is kept under
  `build/.tmp/gpu-diagnostics/`.
- `test-camera/offline` and `test-gui-settings/offline` are windowless variants of those tests.
- `python tests/build_modules.py [module...]` builds the library once per module, each selected alone, in
  `build/.tmp/modules/<module>` with its own configuration. It reads platform, architecture, mode and
  `libvultra_vri_dir` from the main configuration, so configure the root project first.
- clang-tidy uses the root `compile_commands.json`, which xmake regenerates for the current configuration.

Rendering changes need finite-frame runs, validation-layer checks and image readback; compilation or a successful
exit code alone does not prove correct rendering. Keep verification output under `build/.tmp/`.

Run `./scripts/setup_vscode.ps1` or `sh scripts/setup_vscode.sh` for clangd and shader editor settings, and
dot-source `scripts/setup_xmake_completion.ps1` or source `scripts/setup_xmake_completion.zsh` for shell
completion. Personal settings, compilation databases and captures stay out of source control.
