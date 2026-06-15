# VultraEngine Wiki

**English** | [简体中文](zh_CN/wiki_CN.md)

Welcome to the VultraEngine documentation. VultraEngine is a C++23 real-time graphics engine for
**VR/XR rendering research and game development** — a single codebase that works as a lab bench for
rendering experiments and as a shipping runtime across desktop, mobile, web, and head-mounted displays.

This page is the entry point to all engine documentation. New here? Start with
**[Getting Started](getting_started.md)** and the **[Architecture overview](architecture.md)**.

> These pages live in [`doc/`](.) and are also intended to be published to the project's GitHub Wiki.

## Getting Started

| Page | What it covers |
| --- | --- |
| [Getting Started](getting_started.md) | The `vultra` executable, launch modes/flags, creating & opening projects, an editor tour. |
| [Architecture overview](architecture.md) | The `vultra` / `vultra-app` split, core & function layers, the service registry, data flow. |

## Content & Assets

| Page | What it covers |
| --- | --- |
| [Project & assets](project_and_assets.md) | `.vproject` / `.env` / `.vimport`, the vasset import→registry→cook→VPK pipeline, the `res://` / `builtin://` / `plugins://` virtual file system. |
| [Scenes & components](scene_and_components.md) | The EnTT ECS, the human-readable `.vscn` format, component reflection, and the full component catalog. |
| [Shader system](shader_system.md) | `vshadersystem`: the extended-GLSL `.vshader` format, namespaced IDs, keyword permutations, VFS includes, shader libraries, and multi-backend compilation. |

## Rendering

| Page | What it covers |
| --- | --- |
| [Render graphs](render_graphs.md) | Renderer architecture, the high-end / compatibility / ray-tracing tiers, the `.vrg.json` format, and the render-graph editor. |
| [GPU-driven pipeline](gpu_driven_pipeline.md) | Deep dive into the deferred GBuffer pipeline and the experimental meshlet / visibility-buffer path. |
| [Scripted render passes](scripted_render_passes.md) | Authoring project render-graph passes in Lua (`setup` / `execute`). |
| [Material custom nodes](material_custom_nodes.md) | Extending the material graph with custom nodes and shading models. |
| [Render upscaler plugins](render_upscaler_plugins.md) | The upscaler extension points (e.g. DLSS / Streamline bridges). |
| [Particle system](particle_system.md) | The GPU-compute particle system and its CPU fallback. |

## Gameplay

| Page | What it covers |
| --- | --- |
| [Gameplay systems](gameplay_systems.md) | Physics (Jolt), 3D spatial audio (miniaudio), animation (ozz + animator graph), and in-game UI. |
| [VR / XR](vr_xr.md) | OpenXR support: stereo rendering, scene-driven XR cameras, mirror views, and view synthesis. |
| [Scenes & components](scene_and_components.md) | The component catalog used to compose gameplay objects. |

## Scripting & Extensibility

| Page | What it covers |
| --- | --- |
| [Lua scripting](lua_scripting.md) | Gameplay scripting: lifecycle, components, input, physics, audio, UI, animation, coroutines. |
| [Lua API design](lua_api_design.md) | The normative Lua API spec and its conformance rules. |
| [Script binding codegen](script_binding_codegen.md) | How the Lua bindings are generated from `VBIND_*`-annotated headers. |
| [Plugin system](plugins.md) | Runtime-loadable native C++ and Lua plugins, the managed catalog, and the editor-extension API. |

## Platform & Localization

| Page | What it covers |
| --- | --- |
| [Cross-platform export](cross_platform_export.md) | Packaging projects, downloadable export templates, and per-platform export. |
| [Internationalization (i18n)](i18n.md) | Translation catalogs, the localized editor, and adding languages. |

## Building & Contributing

| Page | What it covers |
| --- | --- |
| [Building](../BUILD.md) | Build instructions for desktop, WebAssembly, and Android. *(stub — coming soon)* |
| [Contributing](../CONTRIBUTING.md) | Coding style, branch/PR workflow, and review process. *(stub — coming soon)* |

## Planned pages

These systems exist in the engine but do not have dedicated pages yet. Contributions welcome.

- **Runtime MCP / headless & offscreen** — the localhost Runtime MCP/RPC endpoint, `--render-mode offscreen|none`, and visual capture / preview streams.
- **Gaussian Splatting** — importing, rendering, sorting, and the XR splatting path.
- **Ray tracing** — the hardware ray-tracing renderer tier and examples.
- **Editor guide** — panels, the content browser, undo/redo history, and gizmos in depth.
- **Frame debugger & profiler** — using the in-editor frame-graph debugger and GPU/CPU profiler.
- **Job system** — the `vtask`-based task scheduler.

---

*Documentation that is internal/AI-facing (design plans, refactor roadmaps) lives under [`ai/`](../ai/),
not here — `doc/` is for human-facing documentation.*
