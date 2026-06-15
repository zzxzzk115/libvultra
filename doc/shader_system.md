# Shader System (`vshadersystem`)

**English** | [简体中文](zh_CN/shader_system_CN.md)

Vultra's shaders are authored in **extended GLSL** and compiled through
`vshadersystem`, an external toolchain pinned in `xmake`. A single source file
(`.vshader`) carries an INI/TOML-like header, a keyword/permutation table, and
one section per pipeline stage. The toolchain compiles these into binary
**shader libraries** (`.vshlib` for Vulkan SPIR-V, `.vshweblib` for WebGPU) that
the runtime loads and indexes by a stable, deep-namespaced `id`.

This document covers the authoring format, permutations, include resolution,
project shader libraries, compilation (build-time and CLI), and runtime
consumption.

Related docs:

- [Scripted Render Passes](scripted_render_passes.md) — how a pass selects a
  shader by *library + id*.
- [GPU-Driven Pipeline](gpu_driven_pipeline.md) — the highend passes these
  shaders feed.
- [Material Custom Nodes](material_custom_nodes.md) — material graphs cook to
  generated `.vshader` files.

## The `.vshader` file

A `.vshader` is a plain-text file split into bracketed sections. The header
section `[vshader]` carries metadata; the remaining sections are the keyword
table and per-stage source.

```ini
[vshader]
id       = "builtin/compatibility/basecolor_cpu"
language = glsl
version  = 460

[keywords]
VTX_HAS_UV0 : bool permute

[vert]
#include "include/common/cpu_scene.glsl"
// ... GLSL vertex stage ...

[frag]
#include "include/common/cpu_scene.glsl"
// ... GLSL fragment stage ...
```

(verbatim from `builtin/shaders/passes/compatibility/basecolor_cpu.vshader`.)

### `[vshader]` header

| Key | Meaning |
| --- | --- |
| `id` | Stable, deep-namespaced identifier the runtime looks the shader up by (see below). |
| `language` | Source language — `glsl` across the builtin shaders. |
| `version` | GLSL version, e.g. `460`. |

### The `id` convention

`id` is the lookup key, **not** the file path. It is deep-namespaced so that
engine and project shaders never collide:

- **Engine (builtin) shaders:** `builtin/<profile>/<stem>`
  e.g. `builtin/highend/skybox`, `builtin/compatibility/basecolor_cpu`,
  `builtin/general/gaussian_splat_preprocess.comp`. The `<profile>` segment
  (`highend`, `compatibility`, `general`) matches the subfolder under
  `builtin/shaders/passes/` and the builtin library it is packed into.
- **Project shaders:** `project/<folder>/<stem>`
  e.g. `project/fullscreen/my_pass.frag`.

A render pass references a shader by its `id` together with the *library* it
lives in (`builtin` or `project`) — see
[Scripted Render Passes](scripted_render_passes.md):

```lua
ctx:useGraphicsShader {
    vertexLibrary   = "builtin", vertex   = "builtin/general/fullscreen_triangle.vert",
    fragmentLibrary = "project", fragment = "project/fullscreen/my_pass.frag",
}
ctx:useComputeShader { library = "builtin", compute = "builtin/general/...comp" }
```

### Stage sections

Each pipeline stage is its own bracketed section. A graphics shader places
multiple stages in one file (e.g. `[vert]` + `[frag]` in `skybox.vshader`); a
compute or ray-tracing shader has a single stage section. The stage tags
recognized by the toolchain (canonical name and accepted aliases) are:

| Stage | Tag (and aliases) |
| --- | --- |
| Vertex | `[vert]` / `[vertex]` |
| Fragment | `[frag]` / `[fragment]` |
| Geometry | `[geom]` |
| Compute | `[comp]` / `[compute]` |
| Mesh | `[mesh]` |
| Task | `[task]` |
| Ray generation | `[rgen]` / `[raygen]` |
| Ray miss | `[rmiss]` / `[miss]` / `[raymiss]` |
| Closest hit | `[rchit]` / `[closesthit]` / `[raychit]` |
| Any hit | `[rahit]` / `[anyhit]` / `[rayahit]` |
| Intersection | `[rint]` / `[intersect]` / `[rayint]` |

(The alias set is enumerated in `builtin/xmake.lua`'s `inject_platform_define`;
the canonical-to-`ShaderStage` mapping is in
`source/vultra/src/core/rhi/shader_compiler.cpp`.) The file suffix mirrors the
stage for single-stage shaders — `*.vert.vshader`, `*.frag.vshader`,
`*.comp.vshader`, `*.rgen.vshader`, etc. — but the `[...]` section tag, not the
filename, is authoritative.

Inside a stage section the body is ordinary GLSL for that stage. Ray-tracing
shaders enable the relevant extensions in-source, e.g.
`#extension GL_EXT_ray_tracing : require`.

## Keywords and permutation

The `[keywords]` section declares compile-time switches that drive `#ifdef`
permutations. The form

```ini
VTX_HAS_UV0 : bool permute
```

declares a boolean keyword `VTX_HAS_UV0` whose `permute` flag tells the
compiler to build **every combination** of the permuting keywords as a separate
variant. Each keyword becomes a preprocessor define inside the stage source, so
shaders branch on it with normal preprocessor guards:

```glsl
#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif

#if VTX_HAS_UV0
layout(location = 3) in vec2 a_TexCoord0;
#endif
```

A shader with three permuting keywords (e.g.
`gaussian_splat_preprocess.comp.vshader` with `USE_MULTIVIEW`,
`USE_DIRECT_PREFIX`, `USE_FOVEATED_LAYER_OUTPUT`) yields 2x2x2 compiled
variants; the runtime selects the right variant from the requested define set.

### Global keywords

Beyond per-shader `[keywords]`, the build feeds a **global keyword file**
(`.vkw`) shared by all builtin shaders. `builtin/shaders/builtin_keywords.vkw`
declares the vertex-attribute permutations:

```text
keyword permute global VTX_HAS_COLOR=0|1
keyword permute global VTX_HAS_NORMAL=0|1
keyword permute global VTX_HAS_UV0=0|1
keyword permute global VTX_HAS_UV1=0|1
keyword permute global VTX_HAS_TANGENT=0|1
```

The build also injects a `PLATFORM_WEBGPU` define per backend, so a single
source tree compiles into both Vulkan and WebGPU libraries.

## `#include` resolution via the VFS

`#include "include/common/cpu_scene.glsl"` resolves through Vultra's virtual
file system, not the OS filesystem. At build time the entire
`builtin/shaders/include/**.glsl` tree is packed into an include library
(`.vshglsl`) and **mounted at the VFS root**, so every shader resolves
`#include "include/..."` by absolute VFS path regardless of where the including
file sits in the directory tree (see `pack_include_library` in
`builtin/xmake.lua`).

Project shaders likewise resolve includes against their project mount, and the
runtime/CLI path injects engine include sources as *virtual includes*
(`shaderVirtualIncludes`, see `makeToolAssetImportOptions` in
`source/vultra_app/src/main.cpp`) so project material/shader cooking can pull in
builtin headers without copying them.

## Project shader libraries (`.vshaderlib.lua`)

A project declares its shader library with a small Lua manifest. The shipped
example `resources/shaders/project.vshaderlib.lua` is:

```lua
return ShaderLibrary {
    name = "project",
    root = "shaders",
    shaders = {
        "**/*.vshader",
    },
}
```

| Field | Meaning |
| --- | --- |
| `name` | Library name passed to `ctx:useGraphicsShader { fragmentLibrary = "project", ... }`. |
| `root` | Folder (relative to the manifest) the globs are resolved against. |
| `shaders` | Glob patterns selecting the `.vshader` sources to compile into the library. |

The library `name` is how render passes address the library; the builtin
libraries use the reserved name `builtin`. A pass selects a specific shader by
*library name* + *shader id* (`fragmentLibrary` / `fragment`, etc.).

## Compilation

`vshadersystem` is a multi-backend compiler:

- **Vulkan / SPIR-V** — the default; output libraries are `.vshlib`.
- **WebGPU** — output libraries are `.vshweblib` (built with `--webgpu`).

The versions are pinned in `xmake`. In `builtin/xmake.lua`:

```lua
add_requires("vshadersystem v0.11.1", { configs = vshadersystem_configs })
add_requires("vshadersystem~host v0.11.1", { host = true, kind = "binary", ... })
```

with a `v0.6.2` host/runtime pair selected on the alternate branch. (Verify the
exact active pins in `builtin/xmake.lua`; `external/vasset/.../xmake.lua` also
requires `vshadersystem v0.11.1`.)

### Build-time compilation

The `shader_task` xmake task drives the host `vshaderc` tool to build the
builtin libraries:

- It globs the per-profile shader sets (`passes/highend/**`,
  `passes/compatibility/**`, `passes/general/**`, ...), injects the
  `PLATFORM_WEBGPU` define, packs the include tree into a `.vshglsl` library,
  and runs `vshaderc build --shader_root ... --shader ... --keywords-file ...
  -o <out>`.
- It produces `builtin_highend.vshlib`, `builtin_compatibility.vshlib`, and
  `builtin_compatibility.vshweblib` under `builtin/shader_lib/`.
- The task is incremental: it rebuilds only when a shader source, an include,
  the keyword file, the build script, or `vshaderc` itself is newer than the
  output (`needs_rebuild`).

These compiled libraries are mounted from the `builtin::` pack at runtime; there
is no embedded byte-array fallback (see `loadBuiltinShaderLib` in
`source/vultra/src/function/rendering/shader/shader_system.cpp`).

### `vultra shader compile` CLI

The same compiler is exposed through the application's CLI. The `vultra`
executable dispatches a leading subcommand (`source/vultra_app/src/main.cpp`,
`dispatchToolCommand`):

- `vultra shader ...` (alias `vultra vshaderc ...`) forwards to
  `vshadersystem::tool::run_vshaderc`, exposing the toolchain's own
  subcommands — including `build` (compile a library) and `pack-glsl` (pack an
  include tree).
- `vultra asset import|cook <root>` cooks material graphs to generated
  `.vshader` files and builds the project shader library as part of asset
  import.

Run `vultra shader --help` (i.e. `vshaderc --help`) for the authoritative
subcommand list and flags, since those are defined by the pinned `vshadersystem`
release rather than by Vultra.

## Runtime consumption

At runtime shaders are reached through `IShaderService`
(`source/vultra/include/vultra/function/services/shader_service.hpp`):

- `builtinLibrary()` / `builtinLibrary(ShaderProfile)` return the loaded builtin
  `ShaderLibraryRuntime` for the active profile (`eHighend`, `eCompatibility`,
  or `eGeneral`). The default is chosen from the render config (WebGPU and
  Android force the compatibility library).
- `loadProjectLibrary(uri)` / `reloadProjectLibrary(uri)` /
  `findProjectLibrary(uri)` manage the optional project library. A missing
  project library is not an error — passes fall back to the builtin library —
  and the miss is cached to avoid re-probing the VFS each frame.
- `setRenderPassDiagnostics` / `renderPassDiagnostics` surface shader-resolution
  and pass-definition errors (keyed by the pass's `.lua` path) to the editor's
  code editor.

A render pass resolves a shader by asking the relevant library (builtin or
project, by name) for the requested `id` and permutation, and the
`ShaderLibraryRuntime` hands back the compiled SPIR-V/WGSL variant for pipeline
creation. The `ShaderCompiler` wrapper
(`source/vultra/src/core/rhi/shader_compiler.cpp`) bridges Vultra's `ShaderType`
to `vshadersystem::ShaderStage` for ad-hoc/inline compilation; note the
backend currently assumes the entry point is `main`.
