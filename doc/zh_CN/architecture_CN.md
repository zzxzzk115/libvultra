# Architecture

[English](../architecture.md) | **简体中文**

本文档是 VultraEngine 整体构成的高层地图。若需深入了解
各个子系统，请参阅 [子系统文档](#subsystem-documentation) 中的链接。

## 两个层次

VultraEngine 以两个 CMake/xmake 目标的形式发布：

| 目标 | 类型 | 职责 |
| --- | --- | --- |
| **`vultra`** | 静态库 | 引擎本体：核心运行时、RHI、资源系统、ECS/world、渲染、脚本、物理、音频、XR、平台后端。可独立使用（`examples/` 直接链接它）。 |
| **`vultra-app`** | 可执行文件 | 应用外壳：项目启动器、编辑器、独立运行时播放器，以及 `vultra asset …` / `vultra shader …` 工具宿主 —— 全部集于一个二进制文件中。 |

桌面运行时是一个自包含的可执行文件外加一个 `resources.vpk` 资源包；
无需单独发布引擎 DLL。编辑时，`vultra-app` 读取松散的项目文件；
而对于独立构建，它会从资源包中加载已烹饪（cook）的资源。

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

## 核心层（`source/vultra/src/core`）

地基所在。大多是与平台、功能无关的底层管道：

- **`base`** —— 共享基础设施（UUID、哈希、容器、日志辅助工具）。
- **`math`** —— 基于 GLM 的数学约定。
- **`rhi`** —— 渲染硬件接口（Render Hardware Interface）。在两套后端之上的统一抽象：`rhi/backends/vk`
  （Vulkan，主要的桌面/Android 后端，也承载 OpenXR 与光线追踪）和
  `rhi/backends/webgpu`（原生 + 通过 WASM/Emscripten 的浏览器 WebGPU）。
- **`os`** —— 窗口与平台胶水层（桌面上的 SDL3、Web/WASM 上的 GLFW、Android 上的 Game Activity）。
- **`input`**、**`timing`** —— 输入状态与帧计时。
- **`engine`** / **`app`** —— 引擎上下文、帧流水线，以及拥有并驱动（tick）各子系统的应用宿主。
- **`plugin`** —— 原生插件加载器（`PluginManager`）与 `EnginePlugin` ABI。
- **`builtin`** —— 通过烘焙进二进制文件的 zstd `builtin.vpk` 提供的内嵌引擎资源（着色器、字体、渲染图、i18n 目录）访问。
- **`profiling`**、**`i18n`** —— Tracy/RenderDoc 钩子与翻译运行时。

## 功能层（`source/vultra/src/function`）

引擎的功能子系统。每个都是一个 `EngineSubsystem`，按既定顺序被放置（emplace）并驱动（tick），
其中大多数通过 **服务注册表**（见下文）暴露一个窄接口。

| 领域 | 模块 | 备注 |
| --- | --- | --- |
| World 与场景 | `world`、`scene`、`camera` | 基于 EnTT 的 ECS；反射组件；`.vscn`（反）序列化。 |
| 渲染 | `rendering`、`framegraph`、`material`、`resource` | 基于帧图的 SRP 风格声明式渲染器；内置 + 项目渲染图。 |
| 材质与着色器 | `material_graph` | 基于节点的材质编译器 → 生成的着色器源码。 |
| 脚本 | `scripting` | 由单一 IR 流水线生成的 Lua（sol2）绑定；支持实时热重载。 |
| 动画 | `animation` | 基于 ozz 的骨骼运行时 + 动画器状态机。 |
| 物理 | `physics` | Jolt 刚体仿真。 |
| 音频 | `audio` | 基于 miniaudio 的 3D 空间音频 + 监听器。 |
| 特效 | `particle` | GPU 计算粒子仿真，带 CPU 回退。 |
| UI | `ui`、`imgui` | 游戏内 Canvas/RectTransform UI 套件；用于编辑器/调试的 Dear ImGui。 |
| XR | `openxr` | OpenXR 立体相机与渲染图模板（Vulkan 后端）。 |
| 可扩展性 | `plugin` | 在核心加载器之上的原生 + Lua 插件生命周期。 |
| 工具 | `debugging`、`debug_draw`、`editor`、`jobs` | 帧调试器、调试图元、编辑器扩展服务、任务调度。 |

### 服务注册表

各子系统以 `IxxxService` 接口的形式发布能力（例如 `IAssetService`、`IRenderService`、
`IScriptService`、`IPhysicsService`、`IAudioService`、`IWorldService`，……，位于
`function/services/`）。消费方从 `EngineContext` 的注册表中按接口解析它们，而不是
依赖具体类型。注册表以服务名为键，因此解析可以跨越 DLL 边界 —— 这正是单独构建的原生
**插件** 触达宿主服务的方式。

## 应用外壳（`source/vultra/src/vultra_app`）

`vultra-app` 是一个轻量宿主，它从命令行选择一种模式并驱动引擎：

- **项目启动器** —— 创建/打开 `.vproject` 工作区。
- **编辑器** —— 基于编辑器扩展服务构建的完整编辑 UI（场景、检视器、内容浏览器、渲染图与材质图
  编辑器、动画器图、帧调试器、性能分析器）。
- **运行时播放器** —— 从松散文件或 `resources.vpk` 资源包运行项目。
- **工具宿主** —— `vultra asset …` 与 `vultra shader …` 命令行工具。
- **运行时 MCP/RPC** —— 一个可选的本地端点，用于编辑器自动化、无头/离屏
  仿真，以及可视化抓帧（用于具身 AI 与工具链工作流）。

## 数据流

**资源：** 源文件 → 导入（`vasset`）→ UUID 资源注册表 → 烹饪（cook）→ `resources.vpk` 资源包
→ 通过 `vfilesystem` 以 `res://` 挂载 → 由资源子系统按需加载，并进行 GPU
上传与驻留追踪。

**渲染：** ECS world → `RenderWorldCooker` 产出逐帧渲染 world → 声明式
渲染图（`.vrg.json`，内置或项目）由声明式渲染器解析 → 调度为
帧图 → 内置 Pass 适配器（以及 Lua 编写的 Pass）记录 GPU 工作。项目可以替换
或扩展 Pass，而无需 fork 引擎。

**脚本与插件：** Lua 状态在实体脚本与插件之间共享；原生插件
注册 Lua API 与引擎服务，普通脚本与编辑器随后在其之上构建。

<a id="subsystem-documentation"></a>

## 子系统文档

| 主题 | 文档 |
| --- | --- |
| GPU 驱动 / 渲染管线 | [gpu_driven_pipeline_CN.md](gpu_driven_pipeline_CN.md) |
| 脚本化渲染 Pass | [scripted_render_passes_CN.md](scripted_render_passes_CN.md) |
| 渲染超分辨率插件 | [render_upscaler_plugins_CN.md](render_upscaler_plugins_CN.md) |
| 材质自定义节点 | [material_custom_nodes_CN.md](material_custom_nodes_CN.md) |
| 粒子系统 | [particle_system_CN.md](particle_system_CN.md) |
| Lua 脚本 | [lua_scripting_CN.md](lua_scripting_CN.md) |
| Lua API 设计（规约） | [lua_api_design_CN.md](lua_api_design_CN.md) |
| 脚本绑定代码生成 | [script_binding_codegen_CN.md](script_binding_codegen_CN.md) |
| 插件系统 | [plugins_CN.md](plugins_CN.md) |
| 国际化（i18n） | [i18n_CN.md](i18n_CN.md) |
| 跨平台导出 | [cross_platform_export_CN.md](cross_platform_export_CN.md) |
