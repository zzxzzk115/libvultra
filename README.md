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
- **Lua scripting** — engine service bindings for scene, entity, transform, input, timing, asset,
  and render access, generated from a single IR-based binding pipeline.
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
- **First-class i18n** — lz4-embedded JSON catalogs, a fully localized editor (English, Simplified
  Chinese, Japanese, Korean), OS-language auto-detection, live switching, and a bundled pan-CJK +
  color-emoji font; games can register and override their own catalogs.

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

## Documentation

Design and subsystem documentation lives under [`doc/`](./doc/):

| Topic | Document |
| --- | --- |
| Asset system architecture | [doc/architecture/asset-system.md](./doc/architecture/asset-system.md) |
| Render system architecture | [doc/architecture/render-system.md](./doc/architecture/render-system.md) |
| GPU-driven pipeline | [doc/gpu_driven_pipeline.md](./doc/gpu_driven_pipeline.md) |
| Scripted render passes | [doc/scripted_render_passes.md](./doc/scripted_render_passes.md) |
| Render upscaler plugins | [doc/render_upscaler_plugins.md](./doc/render_upscaler_plugins.md) |
| Material custom nodes | [doc/material_custom_nodes.md](./doc/material_custom_nodes.md) |
| Particle system | [doc/particle_system.md](./doc/particle_system.md) |
| Lua scripting | [doc/lua_scripting.md](./doc/lua_scripting.md) |
| Lua API design | [doc/lua_api_design.md](./doc/lua_api_design.md) |
| Script binding codegen | [doc/script_binding_codegen.md](./doc/script_binding_codegen.md) |
| Plugin system | [doc/plugins.md](./doc/plugins.md) |
| Internationalization (i18n) | [doc/i18n.md](./doc/i18n.md) |
| Cross-platform export | [doc/cross_platform_export.md](./doc/cross_platform_export.md) |

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
| [SDL3](https://github.com/libsdl-org/SDL) | Cross-platform windowing and input |

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
</content>
</invoke>
