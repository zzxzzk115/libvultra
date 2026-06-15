# VR / XR (OpenXR)

[English](../vr_xr.md) | **简体中文**

Vultra 最初是为支持 VR 图形研究而构建的，OpenXR 是引擎的一等组成部分。本页介绍
当前 XR 支持的工作方式、如何启用它，以及场景、相机和渲染图如何驱动头显。

> 范围说明：本文档仅涵盖在已发布的 C++/Lua 源码中得到确认的行为。XR 是一个庞大
> 的子系统，部分领域仍在演进；关于有意省略的内容，请参阅
> [待扩展的领域](#待扩展的领域)。

## 概述

XR 在 **Vulkan 后端**上使用 **OpenXR** 配合 Vulkan 图形绑定
（`XR_USE_GRAPHICS_API_VULKAN`）实现。OpenXR 的 `XRDevice` 仅在以 `eXR` 特性标志
请求渲染设备时才会创建，并且它是从 Vulkan 后端的设备创建路径
（`source/vultra/src/core/rhi/backends/vk/vulkan_render_device.cpp`）中创建的。
OpenXR 实例、Vulkan 实例/设备、交换链以及每帧视图循环都是通过 Vulkan 代码路径
连接的。WebGPU 后端不支持 XR。

如果没有可用的 OpenXR 运行时/头显，`XRDevice` 会将自身报告为不可用，引擎会
**优雅地丢弃 `eXR` 特性标志**并以平面（非 XR）方式运行。因此即使在没有头显的
机器上，请求 XR 也是安全的。

默认的形态因子是采用 `PRIMARY_STEREO` 视图配置（两只眼睛）的头戴式显示器，默认的
环境混合模式为不透明（VR）。眼动追踪会被检测但属于可选项
（`XRDeviceProperties::supportEyeTracking`）。

关键源码位置：

- `source/vultra/include/vultra/function/openxr/` 和
  `source/vultra/src/function/openxr/` — `XRDevice`、`XRHeadset`、输入，以及
  `XRRuntimeSystem` 引擎子系统。
- `source/vultra/include/vultra/function/services/render_backend_service.hpp` —
  `IRenderBackendService` 的 XR API（`requestXRSession`、`isXREnabled`、
  `isXRMirrorEnabled`、`xrEyeViews()`，以及 `XREyeView` 结构体）。
- `source/vultra/src/function/rendering/backend/render_backend_system.cpp` —
  每帧的 XR 交换链获取、每眼视图组装以及镜像目标。

## 启用 XR

### 启动标志

运行时在 `source/vultra_app/src/launch_options.cpp` 中解析这些标志：

| 标志 | 效果 |
| --- | --- |
| `--xr` | 为本次会话请求 XR |
| `--no-xr` | 为本次会话禁用 XR |
| `--xr-mirror` | 启用桌面镜像视图（头显图像显示在显示器上） |
| `--no-xr-mirror` | 禁用镜像视图 |

示例（来自用法横幅）：

```
vultra [--no-xr] [--xr-mirror|--no-xr-mirror] --editor --project <project-dir>
```

这些标志会设置 `LaunchOptions::xr` 和 `LaunchOptions::xrMirror`。由于 XR 会优雅
回退，在未连接头显时请求 `--xr` 只会以平面方式运行。

### 配置开关

引擎配置暴露了一个 `XRConfig` 块
（`source/vultra/include/vultra/core/engine/engine_context.hpp`，
`config.render.xr`）：

| 字段 | 默认值 | 含义 |
| --- | --- | --- |
| `mirror` | `true` | 桌面镜像视图是否开启 |
| `autoStartSessionFromScene` | `true` | 让场景自动启动/停止 XR 会话（见下文） |
| `runtimeCameraOverride` | `false` | 无论场景相机如何都强制启动 XR 会话 |

### 镜像视图

当 XR 和镜像都启用时，引擎会维护每眼的**镜像目标**，它们复制头显的眼部图像，
以便能够显示在桌面上（例如在 ImGui 面板或编辑器中）。渲染后端通过
`IRenderBackendService::isXRMirrorEnabled()` 暴露镜像状态，并通过 `xrEyeViews()`
公开每个眼部视图的 `mirrorTarget`。示例中包含一个小的 `ExampleXrMirrorPanel`
（`examples/example_renderer.hpp`），它使用这些目标绘制实时的眼部图像。

## XR 相机（场景驱动）

XR 渲染由**场景的相机**驱动，而不是由全局开关驱动。该机制由一个组件加一个引擎
子系统构成：

- `XRViewComponent`
  （`source/vultra/include/vultra/function/world/components/xr_view_component.hpp`）
  附加到相机实体上，以将其标记为 XR 视图。字段：`enabled`、
  `trackingOrigin`（`eLocal` / `eStage`）、`stereoGraphMode`
  （`eSingleGraphStereo`）以及 `fallbackMono`。相机实体充当 XR 装置/跟踪原点；
  运行时的头部/眼部姿态会**相对于该实体的变换**应用。

- `XRRuntimeSystem`
  （`source/vultra/src/function/openxr/xr_runtime_system.cpp`）是一个
  `EngineSubsystem`，当设置了 `config.render.xr.autoStartSessionFromScene` 时，
  它每帧检查世界并调用 `IRenderBackendService::requestXRSession(...)`。当优先级
  最高的活动**主**相机携带一个已启用的 `XRViewComponent` 时（或者，如果没有主
  相机，当存在任何携带已启用 `XRViewComponent` 的活动相机时），它会请求一个会话。
  设置 `config.render.xr.runtimeCameraOverride` 会无条件强制启动会话。

每帧当 XR 处于活动状态时，渲染后端会获取头显交换链图像，并通过 `xrEyeViews()`
为每只眼睛发布一个 `XREyeView`。一个 `XREyeView` 携带每眼的 `view` / `projection`
/ `pose` 矩阵、FOV、IPD、头部和眼部位置/旋转、预测的显示时间、有效性/跟踪标志，
以及渲染目标（`target`、`stereoTarget`、`mirrorTarget`）。

相机系统（`source/vultra/src/function/camera/camera_system.cpp`）会消费这些眼部
视图：对于一个启用了 XR 的相机，它通过 `makeXREyeCamera(...)` 为每只眼睛克隆一次
基础 `RenderCamera`，将眼部姿态与装置的世界变换组合，并对结果进行标记
（`isXRView`、`viewIndex`、`viewCount = 2` 等）。眼部姿态仅在位置和朝向都有效时
（`xrPoseUsable`）才会被使用；`fallbackMono` 用于尚无可用姿态的情况。

## 立体渲染

头显交换链被创建为一个**立体（数组）目标**。`XRHeadset` 暴露一个
`StereoRenderTargetView`，它对同一交换链图像有三个视图：`stereo`（2 层数组）、
`left` 和 `right`
（`source/vultra/include/vultra/function/openxr/xr_headset.hpp`）。这支持一条
单图立体路径（`XRStereoGraphMode::eSingleGraphStereo`），其中一个渲染图渲染两只
眼睛——使用 Vulkan **多视图（multiview）**，使两只眼睛在一个 pass 中产生。OpenXR
三角形示例直接展示了这一约定：它构建了一条带 `setViewMask(0x3u)` 的多视图管线和
一条回退的单视图管线，并在绘制时根据 `ctx.view().enableMultiview` 在两者之间选择，
对立体情况以 `layers = 2, viewMask = 0x3` 进行渲染。

声明式渲染器通过常规的渲染图机制驱动每眼渲染；关于如何编写 `.vrg.json` 图，
请参阅 [`render_graphs.md`](render_graphs_CN.md)，关于 Lua `setup`/`execute`
pass，请参阅 [`scripted_render_passes.md`](scripted_render_passes_CN.md)。

### XR 脚本化 warp pass

`resources/render/passes/xr_custom_warp.lua` 是一个已发布的**脚本化项目渲染图
pass** 示例，用于 XR 视图合成（一个“自定义 WARP 后端”）。它的约定是：

- 输入：`source`（场景颜色）、`depth`（场景深度）
- 输出：`color`（warp 后的颜色，**alpha = 有效性**）

它是一个全屏 pass（没有几何着色器），因此它也能在内置几何 warp 不可用的地方运行，
并且它暴露从其着色器 `[properties]` 块反射出来的 `disparityScale` 和
`warpDirection` 参数。将它接入一个合成图中以替代内置 warp 节点，并将其输出送入一个
inpaint pass。关于它所使用的脚本化 pass API，请参阅
[`scripted_render_passes.md`](scripted_render_passes_CN.md)。

## 视图合成

视图合成（立体重投影）pass 随引擎源码一起发布，位于
`source/vultra/src/function/rendering/srp/builtin/passes/` 下：

- `geometry_warp_pass.cpp` — 一个 `GeometryWarp` pass，使用逐像素/网格的 mesh
  将源视图重投影到目标视图，将过度拉伸的图元分类为遮挡缺失（disocclusion）空洞。
- `pullpush_inpaint_pass.cpp` — 一个 `PullPushInpaint` pass，用于填补这些空洞，
  带有一个可选的深度感知模式。

它们共享的参数位于 `view_synthesis_settings.hpp`
（`ViewSynthesisSettings`：`sourceView`/`targetView`、`gridSize`、
`sideLenThreshold`、`useDepthAware`、`depthThreshold`）。正如该头文件所指出的，
这些 pass 被编写为通用的源到目标重投影——最常见的用途是从一只眼睛合成另一只立体
眼睛——而上面的脚本化 `xr_custom_warp.lua` 则是内置 warp 的项目编写版对应物。

## 示例

`examples/openxr/` 下有三个可运行的 OpenXR 示例（每个都是请求 `eXR` 渲染设备特性
标志的 `DemoAppHost` 应用）：

| 目录 | 目标 | 运行 |
| --- | --- | --- |
| `examples/openxr/triangle` | `example-openxr-triangle` | `xmake run example-openxr-triangle` |
| `examples/openxr/sponza` | `example-openxr-sponza` | `xmake run example-openxr-sponza` |
| `examples/openxr/gaussian_splatting` | `example-openxr-gaussian-splatting` | `xmake run example-openxr-gaussian-splatting` |

三角形示例是最小的参考：它展示了多视图与单视图管线的选择、一个打印当前 OpenXR
运行时名称/版本以及 XR 启用/镜像状态的 ImGui 面板，以及 `ExampleXrMirrorPanel`
镜像显示。Sponza 和 Gaussian-splatting 是场景内容示例。

如果没有可用的头显/运行时，由于上文所述的优雅 XR 回退，这些示例仍会启动并以平面
方式渲染。

## 待扩展的领域

以下内容是该子系统的一部分，但在此处有意不做详细记录，因为随着它持续演进，精确的
约定最好直接从源码中阅读：

- **XR 输入 / 动作**（`xr_input.cpp`、`xr_input_profile.cpp`、
  `xr_common_action.cpp`）——控制器/动作绑定。
- **眼动追踪**（`ext/xr_eyetracker.cpp`）。
- 完整的**视图合成渲染图连接**（`GeometryWarp` / `PullPushInpaint` / 脚本化 warp
  节点如何组合进一个 `.vrg.json` 合成图）。alpha 有效性约定在
  `xr_custom_warp.lua` 及其着色器中有所描述。
- **跟踪原点（`local` 与 `stage`）以及 stage/房间尺度**的具体细节。
