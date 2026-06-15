# VultraEngine Wiki

[English](../wiki.md) | **简体中文**

欢迎阅读 VultraEngine 文档。VultraEngine 是一款面向
**VR/XR 渲染研究与游戏开发** 的 C++23 实时图形引擎 —— 单一代码库既可作为
渲染实验的实验台，也可作为可发布的运行时，覆盖桌面、移动、Web 以及头戴式显示设备。

本页是所有引擎文档的入口。初来乍到？请从
**[快速上手](getting_started_CN.md)** 与 **[架构总览](architecture_CN.md)** 开始。

> 这些页面位于 [`doc/`](..) 目录下，同时也计划发布到项目的 GitHub Wiki。

## 快速上手

| 页面 | 内容概要 |
| --- | --- |
| [快速上手](getting_started_CN.md) | `vultra` 可执行文件、启动模式/参数、创建与打开项目，以及编辑器导览。 |
| [架构总览](architecture_CN.md) | `vultra` / `vultra-app` 拆分、core 与 function 层、服务注册表、数据流。 |

## 内容与资源

| 页面 | 内容概要 |
| --- | --- |
| [项目与资源](project_and_assets_CN.md) | `.vproject` / `.env` / `.vimport`、vasset 的 import→registry→cook→VPK 流水线，以及 `res://` / `builtin://` / `plugins://` 虚拟文件系统。 |
| [场景与组件](scene_and_components_CN.md) | EnTT ECS、可读性强的 `.vscn` 格式、组件反射，以及完整的组件目录。 |
| [着色器系统](shader_system_CN.md) | `vshadersystem`：扩展 GLSL 的 `.vshader` 格式、命名空间 ID、关键字排列组合、VFS include、着色器库，以及多后端编译。 |

## 渲染

| 页面 | 内容概要 |
| --- | --- |
| [渲染图](render_graphs_CN.md) | 渲染器架构、高端 / 兼容 / 光线追踪分级、`.vrg.json` 格式，以及渲染图编辑器。 |
| [GPU 驱动管线](gpu_driven_pipeline_CN.md) | 深入剖析延迟 GBuffer 管线，以及实验性的 meshlet / 可见性缓冲（visibility-buffer）路径。 |
| [脚本化渲染 Pass](scripted_render_passes_CN.md) | 使用 Lua（`setup` / `execute`）编写项目渲染图 Pass。 |
| [材质自定义节点](material_custom_nodes_CN.md) | 通过自定义节点与着色模型扩展材质图。 |
| [渲染超分辨率插件](render_upscaler_plugins_CN.md) | 超分辨率扩展点（例如 DLSS / Streamline 桥接）。 |
| [粒子系统](particle_system_CN.md) | GPU 计算粒子系统及其 CPU 回退方案。 |

## 玩法

| 页面 | 内容概要 |
| --- | --- |
| [玩法系统](gameplay_systems_CN.md) | 物理（Jolt）、3D 空间音频（miniaudio）、动画（ozz + 动画器图），以及游戏内 UI。 |
| [VR / XR](vr_xr_CN.md) | OpenXR 支持：立体渲染、场景驱动的 XR 相机、镜像视图，以及视图合成。 |
| [场景与组件](scene_and_components_CN.md) | 用于组合玩法对象的组件目录。 |

## 脚本与可扩展性

| 页面 | 内容概要 |
| --- | --- |
| [Lua 脚本](lua_scripting_CN.md) | 玩法脚本：生命周期、组件、输入、物理、音频、UI、动画、协程。 |
| [Lua API 设计](lua_api_design_CN.md) | 规范性的 Lua API 规约及其一致性规则。 |
| [脚本绑定代码生成](script_binding_codegen_CN.md) | Lua 绑定如何从带 `VBIND_*` 注解的头文件生成。 |
| [插件系统](plugins_CN.md) | 运行时可加载的原生 C++ 与 Lua 插件、托管目录，以及编辑器扩展 API。 |

## 平台与本地化

| 页面 | 内容概要 |
| --- | --- |
| [跨平台导出](cross_platform_export_CN.md) | 打包项目、可下载的导出模板，以及按平台导出。 |
| [国际化（i18n）](i18n_CN.md) | 翻译目录、本地化编辑器，以及添加语言。 |

## 构建与贡献

| 页面 | 内容概要 |
| --- | --- |
| [构建](BUILD_CN.md) | 桌面、WebAssembly 与 Android 的构建说明。*（待补充 —— 即将推出）* |
| [贡献](CONTRIBUTING_CN.md) | 代码风格、分支/PR 工作流，以及评审流程。*（待补充 —— 即将推出）* |

## 计划中的页面

以下系统在引擎中已经存在，但尚无专门页面。欢迎贡献。

- **运行时 MCP / 无头与离屏** —— 本地的运行时 MCP/RPC 端点、`--render-mode offscreen|none`，以及可视化抓帧 / 预览流。
- **高斯泼溅（Gaussian Splatting）** —— 导入、渲染、排序，以及 XR 泼溅路径。
- **光线追踪** —— 硬件光线追踪渲染器分级及示例。
- **编辑器指南** —— 面板、内容浏览器、撤销/重做历史，以及 gizmo 的深入讲解。
- **帧调试器与性能分析器** —— 使用编辑器内置的帧图调试器与 GPU/CPU 性能分析器。
- **任务系统** —— 基于 `vtask` 的任务调度器。

---

*内部 / 面向 AI 的文档（设计方案、重构路线图）位于 [`ai/`](../../ai/) 下，
而非此处 —— `doc/` 用于面向人类的文档。*
