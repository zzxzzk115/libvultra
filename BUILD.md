# Build and verification

For complete minimal application code, start with [get_started.md](docs/get_started.md).

## Prerequisites

- Windows x64: Visual Studio 2022 with the C++ toolchain.
- Linux x86_64: C++23 compiler, CMake, pkg-config, Python 3, Vulkan development files,
  X11/Wayland development libraries, wayland-protocols and xkbcommon.
- [xmake](https://xmake.io/guide/quick-start.html#installation) on PATH.
- Vulkan 1.3 GPU/driver for rendering. Ray tracing, ray queries and mesh shading require their device features.
- .NET 10 SDK for C# samples or an all-target build; installed .NET 10 runtime for C# execution.
- Python/libclang and clang-format for API regeneration only. Ordinary builds use checked-in bindings and parsers.

Dependency versions and static package recipes live in [external/xmake.lua](external/xmake.lua) and
[external/packages](external/packages/packages). The initial source build of Slang takes time.
MSVC targets and packages use matching MT/MTd settings. Preserve the [VRI patches](external/vri/README.md).

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

| Option | Purpose |
| --- | --- |
| `libvultra_features` | Comma-separated feature union; root default `all`, embedded default `gpu`. |
| `libvultra_examples` | Category, `category.mode`, or comma-separated union; default `all`. |
| `libvultra_build_examples`, `libvultra_build_tests` | Default on at the root, off when embedded. |
| `libvultra_window_backend` | `glfw` (default) or `sdl3`. |
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
do not register standalone apps/tools. VGui and scripting have optional static companion targets; research
consumers explicitly add `vultra-research-extension`.

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

## Offline rendering

AI/QA can render without a desktop connection. The small [offline example](examples/offline/main.cpp) uses
direct VRI, a project compute pass, C++/JSON graphs, profiling and PNG/linear HDR readback:

```sh
xmake f --libvultra_features=foundation --libvultra_build_tests=n --libvultra_build_examples=y \
    --libvultra_examples=offline
xmake run example-offline --frames 3 --output build/.tmp/offline-cpp
xmake run example-offline --frames 3 --graph examples/research/color_gain.vgraph \
    --output build/.tmp/offline-graph
```

Use fresh output directories. `--gain` applies to the direct path; JSON owns its own parameters.
For project/scene experiments, restore the full selection above, then:

```sh
xmake build vultra-batch
env -u DISPLAY -u WAYLAND_DISPLAY ./build/linux/x86_64/release/vultra-batch \
    --project resources/research.vproject --graph examples/research/color_gain.vgraph \
    --width 640 --height 360 --frames 3 --warmup 1 --revision YOUR_BUILD_ID --output build/.tmp/batch-run
```

The batch CLI requires an input, positive extent/frame count, build revision and new output directory.
`--path reference --seed 42` selects reference transport; `--help` lists other options.
The output associates PNG/PFM images, graph resources and timings with the effective experiment and device.
For the optional Python host, build `vultra-research`, install `./extensions/research/python` with pip and pass the
built host library to `vultra.Research`. Runtime use of these built tools requires no xmake.

## Export a player

After restoring the full selection, build the packer/player. Linux paths are shown; use your build's executable
paths on Windows:

```sh
xmake build vultra-pack vultra-runtime
./build/linux/x86_64/release/vultra-pack resources/research.vproject build/.tmp/research.vpk
./build/linux/x86_64/release/vultra-runtime build/.tmp/research.vpk --frames 60
./build/linux/x86_64/release/vultra-pack --embed ./build/linux/x86_64/release/vultra-runtime \
    build/.tmp/research.vpk build/.tmp/research-game
./build/.tmp/research-game --frames 60
```

Built executables and `packProject()`/`VpkArchive::embedProject()` perform export without xmake.
Player debug UI requires the `api_ui` feature and `--debug-ui`; F1 toggles it. Native/Lua/C# adapters are explicit
features in a smaller player selection. Native modules need sidecar files; C# needs its assemblies and .NET 10.
The base player embeds engine libraries/resources but still requires system graphics/display libraries and drivers.

## Development checks

```sh
xmake test -v
xmake codegen --check
python3 tests/codegen.py
clang-format --dry-run --Werror path/to/changed.cpp
clang-tidy -p . path/to/changed.cpp
```

After annotation changes run `xmake codegen` first. Use the current configuration's `compile_commands.json`;
changing backends requires regeneration. Use a complete LLVM installation with matching resource headers.
Scoped tests and finite-frame image readback must cover changed behavior; expected failure tests still report errors.
Normal xmake tests reject GPU validation errors on stdout or stderr even when the program exits successfully.

Explicit offscreen variants avoid native windows:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY xmake test -v -j1 'test-gpu/offline' 'test-gui-settings/offline' \
    'test-gui-offscreen/*' 'test-vgui-offscreen/*' 'test-vpk/*'
python3 tests/build_modules.py
python3 tests/build_tools.py
python3 tests/build_examples.py
```

The Python probes build isolated feature/target selections and check actual package closure, copied tools without
xmake and finite readbacks. Pass case names to focus a changed boundary. Example image checks need Pillow;
Linux dependency checks use `readelf`. These checks do not replace native window, Windows or headset acceptance.

Full X11 tests need a window manager honoring exact resize/iconify requests, such as Openbox, including under
an isolated Xvfb display. A bare Xvfb server or a tiling compositor is not an equivalent test environment.
Wayland-specific checks use `VULTRA_WINDOW_SYSTEM=wayland xmake test -v 'test-window/*' 'test-camera/*'`.
Keep verification output under `build/.tmp/` and isolate application layouts; do not overwrite desktop state.

Run `sh scripts/setup_vscode.sh` or `./scripts/setup_vscode.ps1` for clangd/shader editor settings.
Source `scripts/setup_xmake_completion.zsh` or dot-source `scripts/setup_xmake_completion.ps1` for shell completion;
add that source command with an absolute helper path to your shell profile for persistence.
Personal settings, compilation databases and captures stay out of source control.
