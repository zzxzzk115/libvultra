# VultraEngine

<h4 align="center">
  一款面向 VR/XR 渲染科研与游戏开发的 C++23 游戏引擎。
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
  <a href="../../README.md">English</a> | <b>简体中文</b>
</p>

> [!NOTE]
> 本项目曾用名 **libvultra**，现已更名为 **VultraEngine**。

## 缘起

VultraEngine 最初是一个用于 **VR/XR 图形** 研究的载体：在一套真正现代的 RHI 之上原型化立体渲染、
新视角合成（novel-view synthesis）、高斯泼溅（Gaussian Splatting）、GPU 驱动管线与光线追踪，
而不必与庞大臃肿的商业引擎搏斗。与此同时，它也被构建为一个实用的 **游戏开发** 运行时：自包含的
播放器、编辑器、数据驱动的资产管线、脚本系统以及插件系统。

我们的目标是用同一套代码库，既能作为渲染实验的 **实验台（lab bench）**，又能作为面向桌面、移动、
Web 与头显的交互式应用的 **可交付运行时（shipping runtime）**。

## 亮点

- **多后端 RHI** —— Vulkan 与 WebGPU 统一在同一套渲染硬件接口之下，并通过 WebAssembly / Emscripten
  支持原生 WebGPU 与浏览器 WebGPU。
- **OpenXR VR/XR 运行时** —— 场景驱动的 XR 相机、立体渲染图模板、编辑器 / 运行时镜像视图，以及在
  Vulkan 后端上的视角合成实验。
- **可编辑、数据驱动的渲染管线** —— SRP 风格的架构，渲染器即声明式的 `.vrg.json` **渲染图**，配套
  实时预览的图编辑器，项目自定义 Pass 可用 Lua 编写，无需 fork 引擎。
- **材质图（Material Graph）** —— 基于节点的材质编辑器与编译器，可生成真实的着色器源码以及逐模型的
  GBuffer 输出节点（`.vmatgraph.json`）。
- **`vshadersystem` —— 扩展的 GLSL 工具链** —— 一种 `.vshader` 方言，支持深层命名空间的着色器 ID、
  基于 VFS 挂载的 `#include` 解析、着色器库以及多后端编译（Vulkan SPIR-V / WebGPU），既可作为构建
  步骤，也可作为运行时 CLI 使用。
- **`vasset` 资产系统** —— 自定义的「导入到运行时」管线，包含导入元数据、基于 UUID 的资产注册表、
  源资产烘焙，以及面向网格、纹理、材质、动画、音频与高斯泼溅的 `VPK` 包清单。
- **虚拟文件系统** —— `vfilesystem` 提供 `res://` URI 与挂载的 `VPK` 包，使得编辑时的散装项目文件
  与运行时的烘焙包共用同一条代码路径。
- **实体 - 组件 - 系统（ECS）世界** —— 基于 EnTT 的 ECS，组件均可反射 —— 变换、相机、灯光、
  网格 / 材质、动画机（Animator）、Jolt 刚体物理、3D 音频源 / 监听器、完整的 Canvas/RectTransform
  UI 组件集、粒子发射器与脚本 —— 全部可在检视面板中编辑，并随场景往返序列化。
- **带热重载的 Lua 脚本** —— 面向场景、实体、变换、输入、计时、资产、音频与渲染的引擎服务绑定，
  由统一的、基于 IR 的绑定流水线生成；实体脚本支持实时热重载（`Script.reloadEntity` /
  `Script.reloadAll`），编辑器文件监视器会在保存时自动拾取改动。
- **3D 空间音频** —— 带距离衰减的位置音频，监听器跟随活动相机或显式的 `AudioListener`，由 miniaudio
  驱动。
- **人类与 AI 皆可读的项目格式** —— 场景是可 diff 的纯文本 `.vscn`（`组件/字段 = 值`），渲染图、
  材质图、项目与清单则为 JSON —— 便于版本控制、外部工具链以及 AI 编码智能体直接读取与编辑。
- **插件系统** —— 可在运行时加载的原生 C++ 与/或 Lua 插件，具备托管目录（managed catalog）、按项目
  启用、渲染 Pass / 着色器库贡献，以及编辑器扩展 API。
- **现代渲染特性** —— 延迟光照、阴影贴图、SSAO、SSR、FXAA、色调映射、选中描边、meshlet /
  可见性缓冲（visibility-buffer）GPU 驱动路径、硬件光线追踪示例，以及 3D 高斯泼溅。
- **内置图形科研工具** —— 编辑器内置 **帧调试器（Frame Debugger）**（逐 Pass 的渲染图检视，附资源
  缩略图）、**GPU/CPU 性能分析器（Profiler）**（命令缓冲计时，集成 Tracy）、实时渲染图查看器，以及
  RenderDoc / 校验层（validation-layer）挂钩 —— 让渲染实验无需离开引擎即可观测。
- **AI 辅助开发** —— 基于 MCP 的编辑器 Agent 层、AI 自动布局（AI Auto Layout），以及按项目组织的
  `ai/` 工作区脚手架（规格 specs、任务 tasks、知识 knowledge），让编码 / 智能体工具能够驱动编辑器
  并理解项目。
- **面向具身智能（Embodied-AI）的运行时** —— 提供本地 Runtime MCP/RPC 端点，用于编辑器自动化、
  无头 / 离屏仿真，以及浏览器 / Python 视觉采集流。
- **内置多语言（i18n）** —— lz4 内嵌的 JSON 词条目录、已本地化的编辑器（英语、简体中文、日语、
  韩语）、操作系统语言自动检测、运行时即时切换，以及内置的泛中日韩（pan-CJK）+ 彩色 emoji 字体；
  游戏可注册并覆盖自己的词条目录。

## 架构

VultraEngine 最初名为 **`libvultra`** —— 一个没有编辑器的无头渲染 / 运行时库，也就是你今天看到的
**`vultra`** 静态库。编辑器、启动器与工具链是随后逐步叠加上去的，才成长为如今完整的引擎。这段历史
体现在当前的两层划分中：

- **`vultra`** —— 引擎库（静态库）。包含核心运行时、RHI、资产系统、ECS / 世界层、脚本系统、渲染系统、
  OpenXR 集成以及平台后端。它可以独立使用，本仓库中的 `examples/` 即是直接链接它的 **`libvultra` 示例**。
- **`vultra-app`** —— 应用外壳。同一个可执行文件可充当项目启动器、编辑器、命令行资产 / 着色器工具宿主，
  以及独立打包的运行时。

桌面运行时是一个自包含的可执行文件加一个 `VPK` 资产包 —— 无需在 app 旁边附带单独的引擎 DLL。
`vultra-app` 在编辑时可直接从项目资源运行，独立构建时则从 `resources.vpk` 加载烘焙后的资产。

## 渲染

渲染栈围绕 SRP 风格的相机烘焙、渲染器选择、帧图（frame graph）与声明式渲染图构建：

- `.vrg.json` —— 渲染图节点、资源、编辑器布局与 Pass 参数。
- `.vrp.lua` —— Lua 编写的渲染管线 / Pass 定义。
- `.vshaderlib.lua` —— 项目着色器库声明与着色器 glob。
- `.vmatgraph.json` —— 编译为生成着色器源码的材质图。

内置渲染器提供明确的分级（高端、兼容、光线追踪三条路径），项目图系统让游戏可以 **替换或扩展 Pass**
—— 既可声明式，也可用完整的 Lua `setup`/`execute` Pass —— 无需 fork 引擎。

## 资产与内容管线

- `.vproject` —— 标识项目、资产根目录、默认场景与可编辑渲染图。
- `.vimport` —— 跟踪导入的源资产及其烘焙输出。
- `.vscn` —— 场景实体与反射的组件字段。
- `resources.vpk` —— 运行时资产包。
- `res://` —— 通过资产系统与虚拟文件系统解析的路径。

资产与着色器也可通过集成的工具宿主访问：

```bash
vultra asset import resources --reimport
vultra asset pack resources resources.vpk --zstd 6
vultra shader compile -i path/to/shader.vshader -o build/shaders
```

## 平台目标

| 平台 | 主后端 | 说明 |
| --- | --- | --- |
| Windows | Vulkan + WebGPU | 编辑器、启动器、运行时、工具、OpenXR、示例。 |
| Linux | Vulkan + WebGPU | 基于 SDL 的编辑器 / 运行时路径，可选 Wayland。 |
| macOS | Vulkan + WebGPU | 桌面运行时 / 编辑器构建，带打包运行时 rpath。 |
| Android | Vulkan | 面向运行时的路径，内置 `resources.vpk`。 |
| Web | WebGPU | WebAssembly / Emscripten 构建，预加载 `resources.vpk`。 |

## 展示

以下是本仓库内随附、直接链接 `vultra` 库的 **`libvultra` 示例**：

- [GLTF Viewer](../../examples/gltf_viewer/main.cpp)
- [Demo App](../../examples/demo_app/main.cpp)
- [Sponza SRP 示例](../../examples/sponza/main.cpp)
- [OpenXR Triangle](../../examples/openxr/triangle/main.cpp)
- [OpenXR Sponza](../../examples/openxr/sponza/main.cpp)
- [OpenXR Gaussian Splatting](../../examples/openxr/gaussian_splatting/main.cpp)
- [ImGui 桌面 + WASM](../../examples/imgui/main.cpp)
- [Gaussian Splatting](../../examples/gaussian_splatting/main.cpp)
- [光线追踪示例](../../examples/raytracing/)
- [Mesh Shading 示例](../../examples/meshshading/triangle/)

更多持续扩充的示例集合 —— 包括完整项目与面向游戏的样例 —— 参见
[zzxzzk115/vultra-examples](https://github.com/zzxzzk115/vultra-examples)。

![示例：GLTF Viewer](../../media/images/example-gltf-viewer.png)
![示例：Sponza](../../media/images/example-sponza.png)

## 开始使用

构建说明特意未放入本 README，而将集中于专门的 **`BUILD.md`**（[简体中文](BUILD_CN.md)，编写中），
覆盖桌面端、WebAssembly 与 Android 工具链。

## 命令行参考

运行时可执行文件为 `vultra`（构建目标名为 `vultra-app`）。同一个二进制即是项目启动器、编辑器、
运行时播放器与工具宿主。运行 `vultra help` 查看内置用法说明。

```
vultra [options]
vultra <subcommand> ...
```

### 选项

**项目与运行时**

| 选项 | 说明 |
| --- | --- |
| `--project <dir\|.vproject>` | 打开一个项目。`--editor` 必需。 |
| `--editor` | 启动编辑器（需要 `--project`）。 |
| `--vpk <file>` | 从 `.vpk` 运行打包后的项目。 |
| `--scene <res://...>` | 启动时加载的场景。 |
| `--plugins-dir <dir>` | 发现并启用某目录下的全部插件（运行时显式启用）。 |
| `--render-mode <visible\|offscreen\|none>` | 渲染模式（默认 `visible`）。`offscreen` = 无窗口但渲染服务仍活跃；`none` = 无窗口且无渲染后端。 |

**自动化（Runtime MCP / RPC）**

| 选项 | 说明 |
| --- | --- |
| `--mcp` | 启用本地 Runtime MCP/RPC 端点。 |
| `--mcp-host <host>` | MCP 绑定主机。 |
| `--mcp-port <port>` | MCP 端口（例如 `8848`）。 |

**XR**

| 选项 | 说明 |
| --- | --- |
| `--xr`、`--no-xr` | 启用 / 禁用 OpenXR 会话。 |
| `--xr-mirror`、`--no-xr-mirror` | 启用 / 禁用桌面镜像视图。 |

**图形调试**

| 选项 | 说明 |
| --- | --- |
| `--validation`、`--no-validation` | Vulkan 校验层。 |
| `--debug-markers`、`--no-debug-markers` | GPU 调试标记。 |
| `--renderdoc`、`--no-renderdoc` | RenderDoc 应用内集成。 |

**导出**

| 选项 | 说明 |
| --- | --- |
| `--export` | 导出打包构建（非交互）。 |
| `--export-output <dir>`、`--out <dir>` | 导出输出目录。 |
| `--export-platform <platform>` | 导出目标平台。 |
| `--export-run` | 导出后运行。 |

**其他**

| 选项 | 说明 |
| --- | --- |
| `-h`、`--help` | 显示用法。 |

> `--backend` / `--render-backend` 与 `--render-profile` 出于前向兼容会被接受，但目前尚未生效。

**默认解析行为**

- 未指定 `--vpk` 时，`vultra` 先在可执行文件旁查找 `<可执行文件名>.vpk`，再在常见位置查找
  `resources.vpk`。
- 既无 VPK 也无项目时，打开**项目启动器**。
- `--render-mode=none` 会禁用视觉采集工具；`--render-mode=offscreen` 则保留它们。

### 工具子命令

这些子命令绕过引擎 UI（由第一个参数选择工具）：

```bash
# 资产管线（'asset' 亦接受 'vasset' / 'vasset-cli'）
vultra asset import <asset-root> [--reimport]
vultra asset pack <asset-root> <out.vpk> [--zstd N] [--include logical/path] [--root res://...]
vultra asset validate-vpk <resources.vpk> [--asset-root <root>] [--registry <asset_registry.tsv>]

# 着色器编译器 CLI（'shader' 亦接受 'vshaderc'）；build | compile | pack-glsl | wgsl | ...
vultra shader <vshaderc args>
vultra shader --help

# 连接编辑器 MCP 服务器的 stdio<->HTTP 桥（由 MCP 客户端启动）
vultra mcp-stdio-bridge [--host 127.0.0.1] [--port 8848]

# 内置
vultra help
vultra version
```

### 示例

```bash
# 打开项目启动器（无项目、无包）
vultra

# 编辑项目
vultra --editor --project example.vproject

# 启用 Runtime MCP 并关闭 XR 进行编辑
vultra --editor --mcp --project example.vproject --no-xr

# 运行打包后的项目
vultra --vpk resources.vpk --scene res://scenes/main.vscn

# 从散装文件运行项目（不开编辑器）
vultra --project example.vproject --scene res://scenes/main.vscn

# 固定端口、离屏无头运行 + MCP（可视觉采集）
vultra --mcp --mcp-port 8848 --project example.vproject --render-mode offscreen --no-xr

# 纯仿真无头：无窗口、无 GPU/渲染后端
vultra --mcp --mcp-port 8848 --project example.vproject --render-mode none --no-xr

# Vulkan 校验 + GPU 调试标记 + RenderDoc 捕获
vultra --editor --project example.vproject --validation --debug-markers --renderdoc

# 在编辑器中开启 XR 会话并关闭镜像视图
vultra --editor --project example.vproject --xr --no-xr-mirror

# 导出打包构建并运行
vultra --export --project example.vproject --export-output build/export --export-run

# 工具
vultra asset import resources --reimport
vultra asset pack resources resources.vpk --zstd 6
vultra shader compile -i path/to/shader.vshader -o build/shaders
```

开发期间同样可通过 xmake 传入这些参数，例如
`xmake run vultra-app --editor --project example.vproject`。

## 文档

面向用户的文档请从 **[文档 Wiki](wiki_CN.md)** 开始 —— 它是通往每个页面的有序入口。

| 从这里开始 | |
| --- | --- |
| [快速上手](getting_started_CN.md) | 启动模式、创建项目、编辑器导览。 |
| [架构总览](architecture_CN.md) | 引擎如何组织在一起。 |
| [项目与资产](project_and_assets_CN.md) | 项目文件、资产管线、虚拟文件系统。 |
| [场景与组件](scene_and_components_CN.md) | ECS、`.vscn` 格式、组件目录。 |
| [渲染图](render_graphs_CN.md) · [着色器](shader_system_CN.md) · [Lua 脚本](lua_scripting_CN.md) · [插件](plugins_CN.md) · [VR/XR](vr_xr_CN.md) | 核心子系统。 |

完整索引（渲染深入、玩法系统、材质节点、i18n、跨平台导出等）见 [Wiki](wiki_CN.md)。

## 贡献

欢迎贡献 —— 缺陷报告、功能提案、文档、示例与插件均可。

- 在 [issue 跟踪器](https://github.com/zzxzzk115/VultraEngine/issues) 提交问题与功能请求。
- 在改动之前，[`doc/`](../../doc/) 下的子系统文档是理解架构的最佳起点。
- 详细的 **`CONTRIBUTING.md`**（[简体中文](CONTRIBUTING_CN.md)，编码规范、分支 / PR 工作流与评审流程）
  将与 `BUILD.md` 一并补充。

## 致谢

VultraEngine 站在众多开源项目的肩膀上。感谢所有作者与维护者。

### 图形、RHI 与 XR

| 项目 | 职责 |
| --- | --- |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Vulkan API 头文件与类型定义 |
| [VulkanMemoryAllocator-Hpp](https://github.com/YaaZ/VulkanMemoryAllocator-Hpp) | AMD Vulkan GPU 显存分配器的 C++ 绑定 |
| [WebGPU-distribution (Dawn)](https://github.com/eliemichel/WebGPU-distribution) | WebGPU 后端头文件 / 二进制（桌面 + 浏览器） |
| [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) | VR/XR 运行时集成 |
| [RenderDoc](https://github.com/baldurk/renderdoc) | 应用内 GPU 捕获 / 调试集成 |
| [vulkan_radix_sort (vrdx)](https://github.com/jaesung-cs/vulkan_radix_sort) | Vulkan GPU 基数排序（高斯泼溅排序） |
| [debug-draw](https://github.com/glampert/debug-draw) | 立即模式 3D 调试图元 |
| [Tracy](https://github.com/wolfpld/tracy) | 实时帧 / CPU / GPU 性能分析器（可选） |

### 窗口、输入与平台

| 项目 | 职责 |
| --- | --- |
| [SDL3](https://github.com/libsdl-org/SDL) | 跨平台窗口与输入（桌面主后端） |
| [GLFW](https://github.com/glfw/glfw) | 备用窗口 / 输入后端（用于 Web/WASM） |

### UI / ImGui 生态

| 项目 | 职责 |
| --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | 立即模式 GUI（docking 分支，FreeType + 32 位 wchar） |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | 3D 变换 gizmo |
| [imoguizmo](https://github.com/fknfilewalker/imOGuizmo) | 朝向立方体 gizmo |
| [imnodes](https://github.com/Nelarius/imnodes) | 节点图编辑控件（渲染 / 材质图） |
| [implot](https://github.com/epezent/implot) | 绘图 / 可视化控件 |
| [ImGuiAl](https://github.com/leiradel/ImGuiAl) | 额外的 ImGui 控件（终端、sparkline、消息框） |
| [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog) | 文件 / 文件夹选择对话框 |
| [imgui_graphnode](https://github.com/anthofoxo/imgui_graphnode) | 基于 Graphviz 的 ImGui 图渲染 |
| [IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders) | 图标字体字形常量 |
| [FreeType](https://freetype.org/) | 字形栅格化（UI 文本 + 彩色 emoji） |

### 资产导入、编解码与数学

| 项目 | 职责 |
| --- | --- |
| [Assimp](https://github.com/assimp/assimp) | 模型导入（glTF/FBX/OBJ/…），用于导入器路径 |
| [KTX-Software](https://github.com/KhronosGroup/KTX-Software) | KTX2 / Basis Universal GPU 纹理转码 |
| [meshoptimizer](https://github.com/zeux/meshoptimizer) | 网格优化、meshlet 生成 |
| [stb](https://github.com/nothings/stb) | 图像读写 |
| [tinyexr](https://github.com/syoyo/tinyexr) | OpenEXR / HDR 图像加载 |
| [miniply](https://github.com/vilya/miniply) | 快速 PLY 解析（点云 / 泼溅） |
| [dds-ktx](https://github.com/septag/dds-ktx) | DDS/KTX 头解析 |
| [spz](https://github.com/nianticlabs/spz) | 3D 高斯泼溅压缩格式 |
| [GaussForge](https://github.com/zzxzzk115/GaussForge) | 高斯泼溅处理 / IO |
| [miniaudio](https://github.com/mackron/miniaudio) | 音频解码 / 播放 |
| [GLM](https://github.com/g-truc/glm) | 向量 / 矩阵 / 四元数数学 |
| [OpenCL-Headers](https://github.com/KhronosGroup/OpenCL-Headers) | 可选的 GPU 加速纹理转码 |

### 脚本、ECS、动画与物理

| 项目 | 职责 |
| --- | --- |
| [sol2](https://github.com/ThePhD/sol2) | Lua ↔ C++ 绑定层 |
| [EnTT](https://github.com/skypjack/entt) | 实体 - 组件 - 系统（ECS） |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | 刚体物理仿真 |
| [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) | 骨骼动画运行时 |

### 序列化、压缩与工具

| 项目 | 职责 |
| --- | --- |
| [cereal](https://github.com/USCiLab/cereal) | C++ 对象序列化 |
| [zstd](https://github.com/facebook/zstd) | VPK 包压缩 |
| [lz4](https://github.com/lz4/lz4) | 内嵌内置 blob（字体 / 词条目录）的快速解压 |
| [zlib](https://github.com/madler/zlib) | Deflate 压缩 |
| [xxHash](https://github.com/Cyan4973/xxHash) | 资产 UUID / 校验和的快速哈希 |
| [{fmt}](https://github.com/fmtlib/fmt) | 字符串格式化 |
| [spdlog](https://github.com/gabime/spdlog) | 日志 |
| [magic_enum](https://github.com/Neargye/magic_enum) | 编译期枚举反射 |
| [argparse](https://github.com/p-ranav/argparse) | CLI 参数解析 |
| [enkiTS](https://github.com/dougbinks/enkiTS) | 任务调度器（经由 vtask） |
| [Graphviz](https://gitlab.com/graphviz/graphviz) + [Expat](https://github.com/libexpat/libexpat) | 编辑器可视化的图布局 |
| [GoogleTest](https://github.com/google/googletest) | 单元测试 |

> 精确版本与各平台配置标志定义在各 `xmake.lua` 文件中。
> 第三方包通过一个[维护中的 xmake-repo fork](https://github.com/zzxzzk115/xmake-repo) 获取。

### Vultra 生态

以下是与引擎一同开发、并以子模块 / 包形式使用的第一方库：

| 项目 | 职责 |
| --- | --- |
| [vasset](https://github.com/zzxzzk115/vasset) | 资产格式、导入器、注册表与 VPK 打包 |
| [vfilesystem](https://github.com/zzxzzk115/vfilesystem) | 虚拟文件系统（`res://`、挂载的 VPK） |
| [vtask](https://github.com/zzxzzk115/vtask) | 任务 / 作业调度 |
| [vbase](https://github.com/zzxzzk115/vbase) | 共享基础工具 |
| [vrendergraph](https://github.com/zzxzzk115/vrendergraph) | 帧 / 渲染图抽象 |
| [vshadersystem](https://github.com/zzxzzk115/vshadersystem) | 扩展 GLSL 工具链与着色器库 |

## 许可证

VultraEngine 以 [MIT](../../LICENSE) 许可证发布。

未来项目可能采用捐赠 / 赞助模式以维持开发；源码将始终以宽松许可证保持开放。
