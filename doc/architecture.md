# Architecture

**English** | [简体中文](zh_CN/architecture_CN.md)

This document is a high-level map of how VultraEngine is put together. For deep dives into
individual subsystems, follow the links in [Subsystem documentation](#subsystem-documentation).

## Two layers

VultraEngine ships as two CMake/xmake targets:

| Target | Kind | Responsibility |
| --- | --- | --- |
| **`vultra`** | static library | The engine: core runtime, RHI, asset system, ECS/world, rendering, scripting, physics, audio, XR, platform backends. Usable on its own (the `examples/` link directly against it). |
| **`vultra-app`** | executable | The application shell: project launcher, editor, standalone runtime player, and the `vultra asset …` / `vultra shader …` tool host — all in one binary. |

The desktop runtime is a single self-contained executable plus a `resources.vpk` asset package; there
is no separate engine DLL to ship. While editing, `vultra-app` reads loose project files; for a
standalone build it loads cooked assets from the package.

```
                        ┌──────────────────────────────────────────┐
                        │                vultra-app                 │
                        │  launcher · editor · runtime · tool host  │
                        │           · Runtime MCP/RPC               │
                        └───────────────────┬──────────────────────┘
                                            │ uses
                        ┌───────────────────▼──────────────────────┐
                        │                  vultra                   │
                        │   function/  (subsystems + services)      │
                        │   core/      (RHI, OS, engine, base)      │
                        └──────────────────────────────────────────┘
```

## Core layer (`source/vultra/src/core`)

The foundation. Largely platform- and feature-agnostic plumbing:

- **`base`** — shared primitives (UUIDs, hashing, containers, logging helpers).
- **`math`** — GLM-based math conventions.
- **`rhi`** — the Render Hardware Interface. One abstraction over two backends: `rhi/backends/vk`
  (Vulkan, the primary desktop/Android backend, also hosts OpenXR and ray tracing) and
  `rhi/backends/webgpu` (native + browser WebGPU via WASM/Emscripten).
- **`os`** — windowing and platform glue (SDL3 on desktop, GLFW on Web/WASM, Game Activity on Android).
- **`input`**, **`timing`** — input state and frame timing.
- **`engine`** / **`app`** — the engine context, frame pipeline, and application host that own and tick
  the subsystems.
- **`plugin`** — the native plugin loader (`PluginManager`) and the `EnginePlugin` ABI.
- **`builtin`** — access to embedded engine resources (shaders, fonts, render graphs, i18n catalogs)
  delivered through a zstd `builtin.vpk` baked into the binary.
- **`profiling`**, **`i18n`** — Tracy/RenderDoc hooks and the translation runtime.

## Function layer (`source/vultra/src/function`)

The engine's feature subsystems. Each is an `EngineSubsystem` emplaced and ticked in a defined order,
and most expose a narrow interface through the **service registry** (see below).

| Area | Modules | Notes |
| --- | --- | --- |
| World & scene | `world`, `scene`, `camera` | EnTT-based ECS; reflected components; `.vscn` (de)serialization. |
| Rendering | `rendering`, `framegraph`, `material`, `resource` | SRP-style declarative renderer over a frame graph; builtin + project render graphs. |
| Materials & shaders | `material_graph` | Node-based material compiler → generated shader sources. |
| Scripting | `scripting` | Lua (sol2) bindings generated from a single IR pipeline; live hot reload. |
| Animation | `animation` | ozz-based skeletal runtime + an animator state machine. |
| Physics | `physics` | Jolt rigid-body simulation. |
| Audio | `audio` | miniaudio-backed 3D spatial audio + listener. |
| Effects | `particle` | GPU-compute particle simulation with a CPU fallback. |
| UI | `ui`, `imgui` | In-game Canvas/RectTransform UI set; Dear ImGui for editor/debug. |
| XR | `openxr` | OpenXR stereo cameras and render-graph templates (Vulkan backend). |
| Extensibility | `plugin` | Native + Lua plugin lifecycle on top of the core loader. |
| Tooling | `debugging`, `debug_draw`, `editor`, `jobs` | Frame debugger, debug primitives, editor-extension service, job scheduling. |

### The service registry

Subsystems publish capabilities as `IxxxService` interfaces (e.g. `IAssetService`, `IRenderService`,
`IScriptService`, `IPhysicsService`, `IAudioService`, `IWorldService`, …, in
`function/services/`). Consumers resolve them by interface from the `EngineContext`'s registry rather
than depending on concrete types. The registry is keyed by service name, so resolution works across a
DLL boundary — which is how a separately built native **plugin** reaches the host's services.

## Application shell (`source/vultra/src/vultra_app`)

`vultra-app` is a thin host that selects a mode from the command line and drives the engine:

- **Project launcher** — creates/opens `.vproject` workspaces.
- **Editor** — full editing UI (scene, inspector, content browser, render-graph and material-graph
  editors, animator graph, frame debugger, profiler) built on the editor-extension service.
- **Runtime player** — runs a project from loose files or a `resources.vpk` package.
- **Tool host** — the `vultra asset …` and `vultra shader …` command-line tools.
- **Runtime MCP/RPC** — an optional localhost endpoint for editor automation, headless/offscreen
  simulation, and visual capture (used for embodied-AI and tooling workflows).

## Data flow

**Assets:** source files → import (`vasset`) → UUID asset registry → cook → `resources.vpk` package
→ mounted through `vfilesystem` as `res://` → loaded on demand by the asset subsystem, with GPU
upload and residency tracking.

**Rendering:** the ECS world → `RenderWorldCooker` produces a per-frame render world → a declarative
render graph (`.vrg.json`, builtin or project) is resolved by the declarative renderer → scheduled as
a frame graph → builtin pass adapters (and Lua-authored passes) record GPU work. Projects can replace
or extend passes without forking the engine.

**Scripting & plugins:** the Lua state is shared across entity scripts and plugins; native plugins
register Lua APIs and engine services that ordinary scripts and the editor then build on.

## Subsystem documentation

| Topic | Document |
| --- | --- |
| GPU-driven / rendering pipeline | [gpu_driven_pipeline.md](gpu_driven_pipeline.md) |
| Scripted render passes | [scripted_render_passes.md](scripted_render_passes.md) |
| Render upscaler plugins | [render_upscaler_plugins.md](render_upscaler_plugins.md) |
| Material custom nodes | [material_custom_nodes.md](material_custom_nodes.md) |
| Particle system | [particle_system.md](particle_system.md) |
| Lua scripting | [lua_scripting.md](lua_scripting.md) |
| Lua API design (spec) | [lua_api_design.md](lua_api_design.md) |
| Script binding codegen | [script_binding_codegen.md](script_binding_codegen.md) |
| Plugin system | [plugins.md](plugins.md) |
| Internationalization (i18n) | [i18n.md](i18n.md) |
| Cross-platform export | [cross_platform_export.md](cross_platform_export.md) |
