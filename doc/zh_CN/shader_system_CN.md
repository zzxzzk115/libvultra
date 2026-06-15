# 着色器系统（`vshadersystem`）

[English](../shader_system.md) | **简体中文**

Vultra 的着色器使用 **扩展 GLSL** 编写，并通过
`vshadersystem` 编译，这是一个在 `xmake` 中固定版本的外部工具链。单个源文件
（`.vshader`）包含一个类 INI/TOML 的头部、一个关键字/排列组合表，以及
每个流水线阶段一个段落。工具链将这些编译为二进制
**着色器库**（用于 Vulkan SPIR-V 的 `.vshlib`，用于 WebGPU 的 `.vshweblib`），
运行时通过一个稳定的、深层命名空间的 `id` 加载并索引它们。

本文档涵盖编写格式、排列组合、include 解析、
项目着色器库、编译（构建时与 CLI），以及运行时
使用。

相关文档：

- [脚本化渲染通道](scripted_render_passes_CN.md) — 一个通道如何通过
  *库 + id* 选择着色器。
- [GPU 驱动流水线](gpu_driven_pipeline_CN.md) — 这些着色器所服务的高端通道。
- [材质自定义节点](material_custom_nodes_CN.md) — 材质图烘焙为
  生成的 `.vshader` 文件。

## `.vshader` 文件

`.vshader` 是一个纯文本文件，被拆分为多个用方括号标记的段落。头部
段落 `[vshader]` 承载元数据；其余段落是关键字
表与每阶段的源码。

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

（逐字摘自 `builtin/shaders/passes/compatibility/basecolor_cpu.vshader`。）

### `[vshader]` 头部

| 键 | 含义 |
| --- | --- |
| `id` | 运行时用以查找该着色器的稳定、深层命名空间标识符（见下文）。 |
| `language` | 源语言 — 在所有内置着色器中均为 `glsl`。 |
| `version` | GLSL 版本，例如 `460`。 |

### `id` 约定

`id` 是查找键，**而非**文件路径。它采用深层命名空间，以便
引擎着色器与项目着色器永不冲突：

- **引擎（内置）着色器：** `builtin/<profile>/<stem>`
  例如 `builtin/highend/skybox`、`builtin/compatibility/basecolor_cpu`、
  `builtin/general/gaussian_splat_preprocess.comp`。其中 `<profile>` 段
  （`highend`、`compatibility`、`general`）与
  `builtin/shaders/passes/` 下的子文件夹以及它被打包进的内置库相对应。
- **项目着色器：** `project/<folder>/<stem>`
  例如 `project/fullscreen/my_pass.frag`。

渲染通道通过着色器的 `id` 加上它所在的 *库*
（`builtin` 或 `project`）来引用着色器 — 见
[脚本化渲染通道](scripted_render_passes_CN.md)：

```lua
ctx:useGraphicsShader {
    vertexLibrary   = "builtin", vertex   = "builtin/general/fullscreen_triangle.vert",
    fragmentLibrary = "project", fragment = "project/fullscreen/my_pass.frag",
}
ctx:useComputeShader { library = "builtin", compute = "builtin/general/...comp" }
```

### 阶段段落

每个流水线阶段都是它自己的方括号段落。图形着色器在一个文件中放置
多个阶段（例如 `skybox.vshader` 中的 `[vert]` + `[frag]`）；
计算或光线追踪着色器只有单个阶段段落。工具链识别的阶段标签
（规范名称与可接受的别名）为：

| 阶段 | 标签（及别名） |
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

（别名集合在 `builtin/xmake.lua` 的 `inject_platform_define` 中枚举；
规范名到 `ShaderStage` 的映射在
`source/vultra/src/core/rhi/shader_compiler.cpp` 中。）对于单阶段着色器，文件后缀
与阶段对应 — `*.vert.vshader`、`*.frag.vshader`、
`*.comp.vshader`、`*.rgen.vshader` 等 — 但具有权威性的是 `[...]` 段落标签，
而非文件名。

在一个阶段段落内部，其主体是该阶段的普通 GLSL。光线追踪
着色器在源码内部启用相关扩展，例如
`#extension GL_EXT_ray_tracing : require`。

## 关键字与排列组合

`[keywords]` 段落声明驱动 `#ifdef`
排列组合的编译期开关。以下形式

```ini
VTX_HAS_UV0 : bool permute
```

声明一个布尔关键字 `VTX_HAS_UV0`，其 `permute` 标志告知
编译器将所有参与排列的关键字的**每一种组合**构建为单独的
变体。每个关键字在阶段源码内部成为一个预处理器宏定义，因此
着色器使用普通的预处理器守卫对其进行分支：

```glsl
#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif

#if VTX_HAS_UV0
layout(location = 3) in vec2 a_TexCoord0;
#endif
```

具有三个参与排列的关键字的着色器（例如
`gaussian_splat_preprocess.comp.vshader`，带有 `USE_MULTIVIEW`、
`USE_DIRECT_PREFIX`、`USE_FOVEATED_LAYER_OUTPUT`）会产生 2x2x2 个编译
变体；运行时根据请求的宏定义集合选择正确的变体。

### 全局关键字

除了每个着色器的 `[keywords]`，构建还会提供一个由所有内置着色器共享的
**全局关键字文件**（`.vkw`）。`builtin/shaders/builtin_keywords.vkw`
声明顶点属性排列组合：

```text
keyword permute global VTX_HAS_COLOR=0|1
keyword permute global VTX_HAS_NORMAL=0|1
keyword permute global VTX_HAS_UV0=0|1
keyword permute global VTX_HAS_UV1=0|1
keyword permute global VTX_HAS_TANGENT=0|1
```

构建还会为每个后端注入一个 `PLATFORM_WEBGPU` 宏定义，因此单个
源代码树可同时编译为 Vulkan 与 WebGPU 库。

## 通过 VFS 进行 `#include` 解析

`#include "include/common/cpu_scene.glsl"` 通过 Vultra 的虚拟
文件系统解析，而非操作系统文件系统。在构建时，整个
`builtin/shaders/include/**.glsl` 树被打包进一个 include 库
（`.vshglsl`）并**挂载到 VFS 根**，因此每个着色器都通过绝对 VFS 路径解析
`#include "include/..."`，无论引用该 include 的文件位于目录树中的何处
（见 `builtin/xmake.lua` 中的 `pack_include_library`）。

项目着色器同样针对其项目挂载点解析 include，而
运行时/CLI 路径将引擎 include 源注入为 *虚拟 include*
（`shaderVirtualIncludes`，见
`source/vultra_app/src/main.cpp` 中的 `makeToolAssetImportOptions`），因此项目材质/着色器烘焙可以引入
内置头文件而无需复制它们。

## 项目着色器库（`.vshaderlib.lua`）

项目用一个小型 Lua 清单声明其着色器库。随附的
示例 `resources/shaders/project.vshaderlib.lua` 为：

```lua
return ShaderLibrary {
    name = "project",
    root = "shaders",
    shaders = {
        "**/*.vshader",
    },
}
```

| 字段 | 含义 |
| --- | --- |
| `name` | 传递给 `ctx:useGraphicsShader { fragmentLibrary = "project", ... }` 的库名称。 |
| `root` | 用以解析 glob 的文件夹（相对于清单）。 |
| `shaders` | 选择要编译进该库的 `.vshader` 源的 glob 模式。 |

库的 `name` 是渲染通道寻址该库的方式；内置
库使用保留名称 `builtin`。通道通过
*库名* + *着色器 id*（`fragmentLibrary` / `fragment` 等）选择特定着色器。

## 编译

`vshadersystem` 是一个多后端编译器：

- **Vulkan / SPIR-V** — 默认；输出库为 `.vshlib`。
- **WebGPU** — 输出库为 `.vshweblib`（使用 `--webgpu` 构建）。

版本在 `xmake` 中固定。在 `builtin/xmake.lua` 中：

```lua
add_requires("vshadersystem v0.11.1", { configs = vshadersystem_configs })
add_requires("vshadersystem~host v0.11.1", { host = true, kind = "binary", ... })
```

并在备用分支上选用 `v0.6.2` 的 host/runtime 组合。（请在
`builtin/xmake.lua` 中核实确切的活动固定版本；`external/vasset/.../xmake.lua` 同样
要求 `vshadersystem v0.11.1`。）

### 构建时编译

`shader_task` xmake 任务驱动 host 端 `vshaderc` 工具来构建
内置库：

- 它对每个 profile 的着色器集合进行 glob（`passes/highend/**`、
  `passes/compatibility/**`、`passes/general/**`、...），注入
  `PLATFORM_WEBGPU` 宏定义，将 include 树打包进一个 `.vshglsl` 库，
  并运行 `vshaderc build --shader_root ... --shader ... --keywords-file ...
  -o <out>`。
- 它在 `builtin/shader_lib/` 下产出 `builtin_highend.vshlib`、`builtin_compatibility.vshlib` 与
  `builtin_compatibility.vshweblib`。
- 该任务是增量式的：仅当某个着色器源、某个 include、
  关键字文件、构建脚本，或 `vshaderc` 本身比
  输出更新时才重新构建（`needs_rebuild`）。

这些编译后的库在运行时从 `builtin::` pack 挂载；
没有内嵌的字节数组回退（见
`source/vultra/src/function/rendering/shader/shader_system.cpp` 中的 `loadBuiltinShaderLib`）。

### `vultra shader compile` CLI

同一个编译器通过应用程序的 CLI 暴露。`vultra`
可执行文件分发一个前导子命令（`source/vultra_app/src/main.cpp`，
`dispatchToolCommand`）：

- `vultra shader ...`（别名 `vultra vshaderc ...`）转发到
  `vshadersystem::tool::run_vshaderc`，暴露工具链自身的
  子命令 — 包括 `build`（编译一个库）与 `pack-glsl`（打包一个
  include 树）。
- `vultra asset import|cook <root>` 将材质图烘焙为生成的
  `.vshader` 文件，并作为资产导入的一部分构建项目着色器库。

运行 `vultra shader --help`（即 `vshaderc --help`）以获取权威的
子命令列表与标志，因为这些是由固定的 `vshadersystem`
发行版定义，而非由 Vultra 定义。

## 运行时使用

在运行时，着色器通过 `IShaderService`
（`source/vultra/include/vultra/function/services/shader_service.hpp`）访问：

- `builtinLibrary()` / `builtinLibrary(ShaderProfile)` 返回为活动 profile 加载的内置
  `ShaderLibraryRuntime`（`eHighend`、`eCompatibility`
  或 `eGeneral`）。默认值从渲染配置中选择（WebGPU 与
  Android 强制使用 compatibility 库）。
- `loadProjectLibrary(uri)` / `reloadProjectLibrary(uri)` /
  `findProjectLibrary(uri)` 管理可选的项目库。缺失的
  项目库不是错误 — 通道会回退到内置库 —
  并且该未命中会被缓存以避免每帧重新探测 VFS。
- `setRenderPassDiagnostics` / `renderPassDiagnostics` 将着色器解析
  与通道定义错误（以该通道的 `.lua` 路径为键）呈现给编辑器的
  代码编辑器。

渲染通道通过向相关库（内置或
项目，按名称）请求所需的 `id` 与排列组合来解析着色器，而
`ShaderLibraryRuntime` 交回用于流水线
创建的已编译 SPIR-V/WGSL 变体。`ShaderCompiler` 封装
（`source/vultra/src/core/rhi/shader_compiler.cpp`）将 Vultra 的 `ShaderType`
桥接到 `vshadersystem::ShaderStage` 以进行临时/内联编译；注意该
后端当前假定入口点为 `main`。
