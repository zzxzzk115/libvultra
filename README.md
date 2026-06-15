# VultraEngine

<h4 align="center">
  A C++23  game engine for VR/XR rendering research and game development.
</h4>

<p align="center">
  <a href="https://github.com/zzxzzk115/VultraEngine/actions/workflows/build_windows.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/VultraEngine/build_windows.yaml?branch=master&label=Build-Windows&logo=github" alt="Build-Windows" />
  </a>
  <a href="https://github.com/zzxzzk115/VultraEngine/actions/workflows/build_linux.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/VultraEngine/build_linux.yaml?branch=master&label=Build-Linux&logo=github" alt="Build-Linux" />
  </a>
  <a href="https://github.com/zzxzzk115/VultraEngine/actions/workflows/build_macos.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/VultraEngine/build_macos.yaml?branch=master&label=Build-macOS&logo=github" alt="Build-macOS" />
  </a>
  <a href="https://github.com/zzxzzk115/VultraEngine/actions/workflows/build_android.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/VultraEngine/build_android.yaml?branch=master&label=Build-Android&logo=github" alt="Build-Android" />
  </a>
  <a href="https://github.com/zzxzzk115/VultraEngine/actions/workflows/build_wasm.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/VultraEngine/build_wasm.yaml?branch=master&label=Build-WASM&logo=github" alt="Build-WASM" />
  </a>
  <a href="https://www.codefactor.io/repository/github/zzxzzk115/VultraEngine">
    <img src="https://www.codefactor.io/repository/github/zzxzzk115/VultraEngine/badge" alt="CodeFactor" />
  </a>
  <a href="https://github.com/zzxzzk115/VultraEngine/issues">
    <img src="https://img.shields.io/github/issues/zzxzzk115/VultraEngine" alt="Issues" />
  </a>
  <a href="https://github.com/zzxzzk115/VultraEngine/blob/master/LICENSE">
    <img src="https://img.shields.io/github/license/zzxzzk115/VultraEngine" alt="License" />
  </a>
</p>

<p align="center">
  <b>English</b> | <a href="doc/zh_CN/README_CN.md">简体中文</a>
</p>

> [!NOTE]
> This project was formerly named **libvultra**; it is now **VultraEngine**.

## Motivation

VultraEngine started as a research vehicle for **VR/XR graphics**: a place to prototype
stereo rendering, novel-view synthesis, Gaussian Splatting, GPU-driven pipelines, and ray
tracing on top of a real, modern RHI — without fighting a monolithic commercial engine. At
the same time it is built to be a practical **game development** runtime: a self-contained
player, an editor, a data-driven asset pipeline, scripting, and a plugin system.

The goal is a single codebase that is equally comfortable as a **lab bench** for rendering
experiments and as a **shipping runtime** for interactive applications across desktop, mobile,
web, and head-mounted displays.

## Highlights

- **Multi-backend RHI** — Vulkan and WebGPU behind one render hardware interface, including
  native WebGPU and browser WebGPU via WebAssembly / Emscripten.
- **OpenXR VR/XR runtime** — scene-driven XR cameras, stereo render-graph templates,
  editor/runtime mirror views, and view-synthesis experiments on the Vulkan backend.
- **Editable, data-driven render pipelines** — an SRP-style architecture where renderers are
  declarative `.vrg.json` **render graphs** with a live-preview graph editor, and project passes
  can be authored in Lua without forking the engine.
- **Material Graph** — a node-based material editor and compiler that emits real shader sources
  and per-model GBuffer output nodes (`.vmatgraph.json`).
- **`vshadersystem` — an extended GLSL toolchain** — a `.vshader` dialect with deep-namespaced
  shader IDs, VFS-mounted `#include` resolution, shader libraries, and multi-backend compilation
  (Vulkan SPIR-V / WebGPU), usable both as a build step and a runtime CLI.
- **`vasset` asset system** — a custom import-to-runtime pipeline with import metadata, a
  UUID-based asset registry, source cooking, and `VPK` package manifests for meshes, textures,
  materials, animations, audio, and Gaussian Splats.
- **Virtual file system** — `vfilesystem` provides `res://` URIs and mounted `VPK` packages, so
  the same code path serves loose project files while editing and cooked bundles at runtime.
- **Entity-Component-System world** — an EnTT-based ECS with reflected components — Transform,
  Camera, Light, Mesh/Material, Animator, Jolt rigid-body physics, 3D audio source/listener, a full
  Canvas/RectTransform UI set, particle emitters, and scripts — all editable in the inspector and
  round-tripped through scenes.
- **Lua scripting with hot reload** — engine service bindings for scene, entity, transform, input,
  timing, asset, audio, and render access, generated from a single IR-based binding pipeline; entity
  scripts reload live (`Script.reloadEntity` / `Script.reloadAll`) and an editor file watcher picks up
  edits on save.
- **3D spatial audio** — positional audio with distance attenuation and a listener that rides the
  active camera or an explicit `AudioListener`, backed by miniaudio.
- **Human- and AI-readable project formats** — scenes are diffable plain-text `.vscn`
  (`Component/field = value`), while render graphs, material graphs, projects, and manifests are JSON —
  easy for version control, external tooling, and AI coding agents to read and edit directly.
- **Plugin system** — runtime-loadable native C++ and/or Lua plugins with a managed catalog,
  per-project enablement, render-pass/shader-library contribution, and an editor-extension API.
- **Modern rendering features** — deferred lighting, shadow maps, SSAO, SSR, FXAA, tone mapping,
  selection outlines, a meshlet / visibility-buffer GPU-driven path, hardware ray tracing examples,
  and 3D Gaussian Splatting.
- **Built-in graphics-research tooling** — an in-editor **Frame Debugger** (per-pass frame-graph
  inspection with resource thumbnails), a **GPU/CPU Profiler** (command-buffer timing with Tracy
  integration), a live Render Graph viewer, and RenderDoc / validation-layer hooks — so rendering
  experiments are observable without leaving the engine.
- **AI-assisted development** — an editor Agent layer built on MCP, AI Auto Layout, and per-project
  `ai/` workspace scaffolding (specs, tasks, knowledge) that lets coding/agent tools drive the editor
  and reason about a project.
- **Embodied-AI friendly runtime** — a localhost Runtime MCP/RPC endpoint for editor automation,
  headless/offscreen simulation, and browser/Python visual capture streams.
- **Built-in i18n** — lz4-embedded JSON catalogs, a localized editor (English, Simplified Chinese,
  Japanese, Korean), OS-language auto-detection, live switching, and a bundled pan-CJK + color-emoji
  font; games can register and override their own catalogs.

## Architecture

VultraEngine began life as **`libvultra`** — a headless rendering/runtime library with no editor,
which is the **`vultra`** static library you see today. Editor, launcher, and tooling were layered on
top over time to grow it into the full engine. That history is reflected in the current two-layer split:

- **`vultra`** — the engine library (static). It contains the core runtime, RHI, asset system,
  ECS/world layer, scripting, rendering systems, OpenXR integration, and platform backends. It is
  usable on its own, and the `examples/` in this repository are **`libvultra` examples** that link
  directly against it.
- **`vultra-app`** — the application shell. The same executable acts as project launcher, editor,
  command-line asset/shader tool host, and standalone packaged runtime.

The desktop runtime is a single self-contained executable plus a `VPK` asset package — there is no
separate engine DLL to ship alongside the app. `vultra-app` can run directly from project resources
while editing, or load cooked assets from `resources.vpk` for standalone builds.

## Rendering

The rendering stack is built around SRP-style camera cooking, renderer selection, a frame graph,
and declarative render graphs:

- `.vrg.json` — render-graph nodes, resources, editor layout, and pass parameters.
- `.vrp.lua` — Lua-authored render pipeline / pass definitions.
- `.vshaderlib.lua` — project shader-library declarations and shader globs.
- `.vmatgraph.json` — material graphs compiled to generated shader sources.

The built-in renderer ships explicit tiers (high-end, compatibility, and ray-tracing paths), and the
project graph system lets games **replace or extend passes** — declaratively or with full
Lua-authored `setup`/`execute` passes — without forking the engine.

## Asset & Content Pipeline

- `.vproject` — identifies the project, asset root, default scene, and editable render graph.
- `.vimport` — tracks imported sources and their cooked outputs.
- `.vscn` — scene entities and reflected component fields.
- `resources.vpk` — the runtime asset bundle.
- `res://` — paths resolved through the asset system and virtual file system.

Assets and shaders are also reachable through the integrated tool host:

```bash
vultra asset import resources --reimport
vultra asset pack resources resources.vpk --zstd 6
vultra shader compile -i path/to/shader.vshader -o build/shaders
```

## Platform Targets

| Platform | Primary backend | Notes |
| --- | --- | --- |
| Windows | Vulkan + WebGPU | Editor, launcher, runtime, tools, OpenXR, examples. |
| Linux | Vulkan + WebGPU | Editor/runtime path with SDL, optional Wayland. |
| macOS | Vulkan + WebGPU | Desktop runtime/editor builds with packaged runtime rpaths. |
| Android | Vulkan | Runtime-oriented path with bundled `resources.vpk`. |
| Web | WebGPU | WebAssembly / Emscripten builds with preloaded `resources.vpk`. |

## Showcase

The following are **`libvultra` examples** that ship in this repository and link directly against the
`vultra` library:

- [GLTF Viewer](./examples/gltf_viewer/main.cpp)
- [Demo App](./examples/demo_app/main.cpp)
- [Sponza SRP Example](./examples/sponza/main.cpp)
- [OpenXR Triangle](./examples/openxr/triangle/main.cpp)
- [OpenXR Sponza](./examples/openxr/sponza/main.cpp)
- [OpenXR Gaussian Splatting](./examples/openxr/gaussian_splatting/main.cpp)
- [ImGui Desktop + WASM](./examples/imgui/main.cpp)
- [Gaussian Splatting](./examples/gaussian_splatting/main.cpp)
- [Ray Tracing Examples](./examples/raytracing/)
- [Mesh Shading Example](./examples/meshshading/triangle/)

For a broader, continually growing collection of examples — including full-project and game-oriented
samples — see [zzxzzk115/vultra-examples](https://github.com/zzxzzk115/vultra-examples).

![Example: GLTF Viewer](./media/images/example-gltf-viewer.png)
![Example: Sponza](./media/images/example-sponza.png)

## Getting Started

Build instructions are intentionally kept out of this README and will live in a dedicated
**`BUILD.md`** (coming soon), covering desktop, WebAssembly, and Android toolchains.

## Command-Line Reference

The runtime executable is `vultra` (the build target is `vultra-app`). The same binary is the project
launcher, editor, runtime player, and tool host. Run `vultra help` for the built-in usage text.

```
vultra [options]
vultra <subcommand> ...
```

### Options

**Project & runtime**

| Option | Description |
| --- | --- |
| `--project <dir\|.vproject>` | Open a project. Required by `--editor`. |
| `--editor` | Launch the editor (requires `--project`). |
| `--vpk <file>` | Run a packaged project from a `.vpk`. |
| `--scene <res://...>` | Scene to load on start. |
| `--plugins-dir <dir>` | Discover and enable every plugin in a directory (runtime opt-in). |
| `--render-mode <visible\|offscreen\|none>` | Render mode (default `visible`). `offscreen` = no window but render services stay active; `none` = no window and no render backend. |

**Automation (Runtime MCP / RPC)**

| Option | Description |
| --- | --- |
| `--mcp` | Enable the localhost Runtime MCP/RPC endpoint. |
| `--mcp-host <host>` | MCP bind host. |
| `--mcp-port <port>` | MCP port (e.g. `8848`). |

**XR**

| Option | Description |
| --- | --- |
| `--xr`, `--no-xr` | Enable / disable the OpenXR session. |
| `--xr-mirror`, `--no-xr-mirror` | Enable / disable the desktop mirror view. |

**Graphics debugging**

| Option | Description |
| --- | --- |
| `--validation`, `--no-validation` | Vulkan validation layers. |
| `--debug-markers`, `--no-debug-markers` | GPU debug markers. |
| `--renderdoc`, `--no-renderdoc` | RenderDoc in-app integration. |

**Export**

| Option | Description |
| --- | --- |
| `--export` | Export a packaged build (non-interactive). |
| `--export-output <dir>`, `--out <dir>` | Export output directory. |
| `--export-platform <platform>` | Target platform for the export. |
| `--export-run` | Run the exported build after export. |

**Misc**

| Option | Description |
| --- | --- |
| `-h`, `--help` | Show usage. |

> `--backend` / `--render-backend` and `--render-profile` are accepted for forward compatibility but
> are not applied yet.

**Default resolution behavior**

- Without `--vpk`, `vultra` first looks for `<executable-name>.vpk` next to the executable, then for
  `resources.vpk` in common locations.
- With no VPK and no project, it opens the **Project Launcher**.
- `--render-mode=none` disables visual-capture tools; `--render-mode=offscreen` keeps them available.

### Tool subcommands

These bypass the engine UI (the first argument selects the tool):

```bash
# Asset pipeline ('asset' also accepts 'vasset' / 'vasset-cli')
vultra asset import <asset-root> [--reimport]
vultra asset pack <asset-root> <out.vpk> [--zstd N] [--include logical/path] [--root res://...]
vultra asset validate-vpk <resources.vpk> [--asset-root <root>] [--registry <asset_registry.tsv>]

# Shader compiler CLI ('shader' also accepts 'vshaderc'); build | compile | pack-glsl | wgsl | ...
vultra shader <vshaderc args>
vultra shader --help

# Stdio<->HTTP bridge to the editor's MCP server (launched by MCP clients)
vultra mcp-stdio-bridge [--host 127.0.0.1] [--port 8848]

# Built-in
vultra help
vultra version
```

### Examples

```bash
# Open the Project Launcher (no project, no package)
vultra

# Edit a project
vultra --editor --project example.vproject

# Edit with Runtime MCP enabled and XR off
vultra --editor --mcp --project example.vproject --no-xr

# Run a packaged project
vultra --vpk resources.vpk --scene res://scenes/main.vscn

# Run a project from loose files (no editor)
vultra --project example.vproject --scene res://scenes/main.vscn

# Headless offscreen runtime with MCP on a fixed port (visual capture available)
vultra --mcp --mcp-port 8848 --project example.vproject --render-mode offscreen --no-xr

# Headless simulation-only: no window, no GPU/render backend
vultra --mcp --mcp-port 8848 --project example.vproject --render-mode none --no-xr

# Vulkan validation + GPU debug markers + RenderDoc capture
vultra --editor --project example.vproject --validation --debug-markers --renderdoc

# XR session in the editor with the mirror view off
vultra --editor --project example.vproject --xr --no-xr-mirror

# Export a packaged build and run it
vultra --export --project example.vproject --export-output build/export --export-run

# Tools
vultra asset import resources --reimport
vultra asset pack resources resources.vpk --zstd 6
vultra shader compile -i path/to/shader.vshader -o build/shaders
```

During development the same arguments work through xmake, e.g.
`xmake run vultra-app --editor --project example.vproject`.

## Documentation

Human-facing documentation lives under [`doc/`](./doc/). Start at the
**[documentation wiki](./doc/wiki.md)** — the organized entry point to every page.

| Start here | |
| --- | --- |
| [Getting Started](./doc/getting_started.md) | Launch modes, creating projects, an editor tour. |
| [Architecture overview](./doc/architecture.md) | How the engine fits together. |
| [Project & assets](./doc/project_and_assets.md) | Project files, the asset pipeline, the VFS. |
| [Scenes & components](./doc/scene_and_components.md) | ECS, the `.vscn` format, the component catalog. |
| [Render graphs](./doc/render_graphs.md) · [Shaders](./doc/shader_system.md) · [Lua scripting](./doc/lua_scripting.md) · [Plugins](./doc/plugins.md) · [VR/XR](./doc/vr_xr.md) | Core subsystems. |

See the [wiki](./doc/wiki.md) for the full index (rendering deep-dives, gameplay systems, material
nodes, i18n, cross-platform export, and more).

## Contributing

Contributions are welcome — bug reports, feature proposals, documentation, examples, and plugins.

- Open issues and feature requests on the [issue tracker](https://github.com/zzxzzk115/VultraEngine/issues).
- The subsystem docs under [`doc/`](./doc/) are the best starting point for understanding the
  architecture before making changes.
- A detailed **`CONTRIBUTING.md`** (coding style, branch/PR workflow, and review process) is planned
  alongside `BUILD.md`.

## Acknowledgements

VultraEngine stands on the shoulders of many open-source projects. Thanks to all their authors and
maintainers.

### Graphics, RHI & XR

| Project | Role |
| --- | --- |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Vulkan API headers and type definitions |
| [VulkanMemoryAllocator-Hpp](https://github.com/YaaZ/VulkanMemoryAllocator-Hpp) | C++ bindings for AMD's Vulkan GPU memory allocator |
| [WebGPU-distribution (Dawn)](https://github.com/eliemichel/WebGPU-distribution) | WebGPU backend headers/binaries (desktop + browser) |
| [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) | VR/XR runtime integration |
| [RenderDoc](https://github.com/baldurk/renderdoc) | In-app GPU capture/debugging integration |
| [vulkan_radix_sort (vrdx)](https://github.com/jaesung-cs/vulkan_radix_sort) | Vulkan GPU radix sort (Gaussian Splat ordering) |
| [debug-draw](https://github.com/glampert/debug-draw) | Immediate-mode 3D debug primitives |
| [Tracy](https://github.com/wolfpld/tracy) | Real-time frame/CPU/GPU profiler (optional) |

### Windowing, Input & Platform

| Project | Role |
| --- | --- |
| [SDL3](https://github.com/libsdl-org/SDL) | Cross-platform windowing and input (primary desktop backend) |
| [GLFW](https://github.com/glfw/glfw) | Alternative windowing/input backend (used on Web/WASM) |

### UI / ImGui Ecosystem

| Project | Role |
| --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | Immediate-mode GUI (docking branch, FreeType + 32-bit wchar) |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | 3D transform gizmos |
| [imoguizmo](https://github.com/fknfilewalker/imOGuizmo) | Orientation cube gizmo |
| [imnodes](https://github.com/Nelarius/imnodes) | Node-graph editor widgets (render/material graphs) |
| [implot](https://github.com/epezent/implot) | Plotting/visualization widgets |
| [ImGuiAl](https://github.com/leiradel/ImGuiAl) | Extra ImGui widgets (terminal, sparkline, msgbox) |
| [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog) | File/folder picker dialog |
| [imgui_graphnode](https://github.com/anthofoxo/imgui_graphnode) | Graphviz-backed graph rendering for ImGui |
| [IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders) | Icon-font glyph constants |
| [FreeType](https://freetype.org/) | Glyph rasterization (UI text + color emoji) |

### Asset Import, Codecs & Math

| Project | Role |
| --- | --- |
| [Assimp](https://github.com/assimp/assimp) | Model import (glTF/FBX/OBJ/...) in the importer path |
| [KTX-Software](https://github.com/KhronosGroup/KTX-Software) | KTX2 / Basis Universal GPU texture transcoding |
| [meshoptimizer](https://github.com/zeux/meshoptimizer) | Mesh optimization, meshlet generation |
| [stb](https://github.com/nothings/stb) | Image load/write |
| [tinyexr](https://github.com/syoyo/tinyexr) | OpenEXR / HDR image loading |
| [miniply](https://github.com/vilya/miniply) | Fast PLY parsing (point clouds / splats) |
| [dds-ktx](https://github.com/septag/dds-ktx) | DDS/KTX header parsing |
| [spz](https://github.com/nianticlabs/spz) | 3D Gaussian Splat compression format |
| [GaussForge](https://github.com/zzxzzk115/GaussForge) | Gaussian Splat processing/IO |
| [miniaudio](https://github.com/mackron/miniaudio) | Audio decoding/playback |
| [GLM](https://github.com/g-truc/glm) | Vector/matrix/quaternion math |
| [OpenCL-Headers](https://github.com/KhronosGroup/OpenCL-Headers) | Optional GPU-accelerated texture transcoding |

### Scripting, ECS, Animation & Physics

| Project | Role |
| --- | --- |
| [sol2](https://github.com/ThePhD/sol2) | Lua ↔ C++ binding layer |
| [EnTT](https://github.com/skypjack/entt) | Entity-component-system |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | Rigid-body physics simulation |
| [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) | Skeletal animation runtime |

### Serialization, Compression & Utilities

| Project | Role |
| --- | --- |
| [cereal](https://github.com/USCiLab/cereal) | C++ object serialization |
| [zstd](https://github.com/facebook/zstd) | VPK package compression |
| [lz4](https://github.com/lz4/lz4) | Fast decompression of embedded builtin blobs (fonts/catalogs) |
| [zlib](https://github.com/madler/zlib) | Deflate compression |
| [xxHash](https://github.com/Cyan4973/xxHash) | Fast hashing for asset UUIDs/checksums |
| [{fmt}](https://github.com/fmtlib/fmt) | String formatting |
| [spdlog](https://github.com/gabime/spdlog) | Logging |
| [magic_enum](https://github.com/Neargye/magic_enum) | Compile-time enum reflection |
| [argparse](https://github.com/p-ranav/argparse) | CLI argument parsing |
| [enkiTS](https://github.com/dougbinks/enkiTS) | Task scheduler (via vtask) |
| [Graphviz](https://gitlab.com/graphviz/graphviz) + [Expat](https://github.com/libexpat/libexpat) | Graph layout for editor visualizations |
| [GoogleTest](https://github.com/google/googletest) | Unit testing |

> Exact versions and per-platform configuration flags are defined in the `xmake.lua` files.
> Third-party packages are fetched through a [maintained xmake-repo fork](https://github.com/zzxzzk115/xmake-repo).

### The Vultra Ecosystem

These first-party libraries are developed alongside the engine and used as submodules/packages:

| Project | Role |
| --- | --- |
| [vasset](https://github.com/zzxzzk115/vasset) | Asset formats, importers, registry, and VPK packing |
| [vfilesystem](https://github.com/zzxzzk115/vfilesystem) | Virtual file system (`res://`, mounted VPK) |
| [vtask](https://github.com/zzxzzk115/vtask) | Task/job scheduling |
| [vbase](https://github.com/zzxzzk115/vbase) | Shared base utilities |
| [vrendergraph](https://github.com/zzxzzk115/vrendergraph) | Frame/render graph abstraction |
| [vshadersystem](https://github.com/zzxzzk115/vshadersystem) | Extended GLSL toolchain and shader libraries |

## License

VultraEngine is released under the [MIT](LICENSE) license.

The project may adopt a donation/sponsorship model in the future to sustain development; the source
will remain open under a permissive license.
