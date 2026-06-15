# Render Upscaler Plugins

[English](../render_upscaler_plugins.md) | **简体中文**

引擎拥有一个通用的 upscaler 桥接层。厂商 SDK 代码、DLL、头文件、库、签名以及
特定功能的调用都存放在一个外部原生插件中。

该桥接层分为三层：

- `IRenderBackendExtensionService`：一个早期原生插件可以拥有 Vulkan hook 元数据，并在
  device、swapchain、acquire、present 和 idle 周围接收后端生命周期回调。
- `IRenderUpscalerService`：插件注册一个 `IUpscalerProvider`；游戏/编辑器代码可以选择一个
  provider 并设置 enabled/mode。
- `ExternalUpscaler`：一个渲染图节点，它对原生 command/texture 资源拍快照，调用当前
  活动的 provider，然后清除 frame-graph 命令状态，以便后续 pass 重新绑定其管线与
  描述符。
- 当一个活动的 provider 在真正的 upscaling 模式下被启用时，renderer 会向 provider 询问
  最优渲染范围（extent），并以该更低分辨率构建场景 pass。Upscaler 输出会回到
  当前 target/backbuffer 的范围，因此 upscale 之后的 pass 应连线在 upscaler 之后。

对于需要在 `RenderBackendSystem` 创建渲染设备之前注册的原生插件，请在插件 manifest 中
使用 `"loadPhase": "pre_render_device"`：

```json
{
  "id": "com.example.streamline",
  "loadPhase": "pre_render_device",
  "config": [
    {
      "key": "streamlineSdkRoot",
      "label": "Streamline SDK Root",
      "type": "path",
      "env": "VULTRA_DLSS_STREAMLINE_SDK_ROOT",
      "required": true,
      "description": "Local folder containing Streamline bin/include/lib files. Keep this outside version control."
    },
    {
      "key": "streamlineBin",
      "label": "Streamline Binary Folder",
      "type": "path",
      "env": "VULTRA_DLSS_STREAMLINE_BIN"
    }
  ],
  "native": "vultra_plugin_streamline",
  "entry": "init.lua"
}
```

早期阶段仅限原生代码。Lua 胶水代码会在 `ScriptSystem` 之后通过正常的插件路径稍后运行。
插件可以放在 `<project>/resources/plugins/<plugin-name>/` 下；SDK 本身可以放在机器上的
任意位置，并通过上面由 env 支持的 config 字段来引用。

对于本地 DLSS 测试，将 `resources/plugins/DLSS/` 放入项目，将原生
`vultra_plugin_dlss.dll` 与 NVIDIA Streamline 负载保持在本地，并将机器特定的 SDK 路径放入
`<project>/.env`：

```ini
VULTRA_DLSS_STREAMLINE_SDK_ROOT=C:\Users\Administrator\Downloads\streamline-sdk-v2.11.1
VULTRA_DLSS_STREAMLINE_BIN=C:\Users\Administrator\Downloads\streamline-sdk-v2.11.1\bin\x64
VULTRA_DLSS_PROJECT_ID=7f3a9c21-84bd-46e2-91af-c5d7382b0e64
VULTRA_DLSS_ENABLED=true
VULTRA_DLSS_MODE=performance
```

不要提交 `.env`、真实的 DLSS DLL 或 Streamline SDK 文件夹。

`VULTRA_DLSS_APPLICATION_ID` 是 NVIDIA 分配的数字 id，用于生产环境的 DLSS/NGX。对于本地
Project ID 开发模式请将其留空，但不要把随机值当作生产 id 使用。
`VULTRA_DLSS_PROJECT_ID` 可以是一个稳定的本地 GUID，在验证桥接层时使用。

Lua 控制面：

```lua
local providers = Upscaler.providers()
local active = Upscaler.active()
Upscaler.setActive("noop")
Upscaler.setEnabled(true)
Upscaler.setMode("quality")
local status = Upscaler.status()
```

Streamline 插件也可以在启动时通过由 env 支持的
manifest 设置自行激活：`VULTRA_DLSS_ENABLED=true` 与 `VULTRA_DLSS_MODE=performance`。
可接受的模式有 `ultra_quality`、`quality`、`balanced`、`performance`、
`ultra_performance`、`dlaa` 和 `off`。

Lua 永远不会接收 Vulkan 句柄、纹理、命令缓冲区或裸指针。

示例 `examples/plugins/noop_upscaler` 在没有厂商 SDK 的情况下验证桥接层。它注册一个
名为 `noop` 的 provider，接收 `evaluate()` 调用，并返回 `false`，从而让引擎执行
fallback blit。

对于一个 Streamline/DLSS SR 插件，应将以下职责保留在插件中：

- 从插件发行包加载 `sl.interposer.dll` 或其他 Streamline 二进制文件，
- 执行 Streamline 的 signature/init/shutdown，
- 在使用手动 hook 时填充通用的 Vulkan hook 表，
- 将 `UpscalerFrameToken`、`UpscalerConstants`、`NativeTextureResource` 和 `UpscalerResourceTag`
  映射为 Streamline 的 frame token、constants 与 resource tag，
- 在 Vulkan 设备创建之前，通过
  `IRenderBackendExtension::collectVulkanDeviceRequirements` 暴露
  `slGetFeatureRequirements(...).vkDeviceExtensions`，
- 调用 DLSS 的 option/evaluate API。

插件也可以提供渲染图 pass 资产。一个 Streamline 插件应在其自己的 `render/passes/` 文件夹下
提供一个 Lua pass，例如 `DLSSUpscale`。该 pass 调用通用的脚本化 pass API：

```lua
local out = ctx:createUpscalerOutput {
  color = ctx:getInput("color"),
  depth = ctx:getInput("depth"),
  motion = ctx:getInput("motion"),
}
ctx:setOutput("color", out)
```

并在 `execute` 中记录 `rc:evaluateUpscaler()`。由于 Lua 只看到
FrameGraph 句柄，该 pass 保持可移植；引擎组装原生纹理 tag 并调用活动的 provider。项目可以
安装该插件并将该 pass 连线进自己的 `.vrg.json` 图，而不必依赖引擎
内置节点。`createUpscalerOutput` 默认采用当前视图 target 的范围，因此输入的
color/depth/motion 可以是更低分辨率，而输出会回到 target/backbuffer 分辨率。
Exposure 不是必需的 V1 图输入；DLSS 应使用 provider 的 auto exposure，除非后续插件
有意将手动 exposure 暴露为高层设置或可选资源。

Motion vectors（运动矢量）：

内置渲染图提供一个 `MotionVectors` pass。它读取场景深度，并以 `RG16F` 像素空间运动矢量
格式输出一个 `motion` 资源。第一版实现通过将当前裁剪空间重投影到上一帧裁剪空间来记录
相机运动。这足以为针对静态场景和相机移动的早期 DLSS SR 测试提供一个合法的运动矢量输入。

逐物体速度尚未包含在这第一个 pass 中。动态网格/物体运动将需要后续的
几何/GBuffer 扩展，使用
`RenderInstance::previousWorldMatrix` 写入前一帧与当前帧的物体运动。

Troubleshooting（故障排查）：

- 带有 Win32 错误码 126 的 `failed to load native library` 通常意味着插件 DLL 存在，但它的
  某个依赖 DLL 缺失。检查插件文件夹或配置的 Streamline bin 文件夹
  是否包含 `sl.interposer.dll`、`sl.common.dll`、`sl.dlss.dll` 和 `nvngx_dlss.dll`。
- `hook is activated without device being created` 表示 Streamline 在它得知设备之前就看到了
  Vulkan hook 流量。插件必须在 Vulkan 设备创建之后调用 `slSetVulkanInfo()`，包括
  通过 OpenXR 创建的设备路径。
- `DLSSContext is not available` 来自 NGX，即 NVIDIA 更底层的 AI 功能运行时。在那一点上，
  Streamline 桥接层是存活的，但 NGX 拒绝创建 DLSS 功能上下文。检查 RTX
  硬件、当前的 NVIDIA 驱动、匹配的 SDK DLL、用于本地测试的 Project ID，以及用于生产测试的
  NVIDIA Application ID。对于 Vulkan，还要验证设备启用了 Streamline 所需的
  扩展，例如 `VK_NVX_binary_import` 和 `VK_NVX_image_view_handle`。
- 启用 upscaler 之后出现的 `slEvaluateFeature(DLSS)` 失败，通常意味着帧数据不完整：
  验证 `ExternalUpscaler` 拥有 color、output color、depth 与运动矢量资源，且它们具有非零的
  原生 Vulkan image/image-view 句柄。

Streamline 2.11 的 Vulkan common plugin 仍可能为 `CmdBindPipeline`、
`CmdBindDescriptorSets` 和 `BeginCommandBuffer` 打印不支持的警告，因为其 plugin-manager hook 映射
省略了这些 ID。DLSS 插件通过从 `sl.common.dll` 填充
`VulkanHookTable` 中的通用命令状态回调 slot 来弥补，且 `VulkanCommandBuffer` 会在原生 Vulkan 命令
之后调用这些回调。这使得命令缓冲区状态恢复保留在插件中，而引擎只携带裸 Vulkan 句柄
和函数指针。

DLSS 插件必须在 Vulkan instance/device 及其他后端组件被销毁之前调用 `slShutdown()`。
项目本地的 DLSS 插件在原生插件卸载期间调用 `slShutdown()`，然后注销其
后端 extension/provider。原生插件 DLL 句柄会在更晚、即子系统关闭之后才被释放，因此
插件可以在渲染后端仍然存活时关闭 Streamline，而不会过早卸载代码。

不要将 Streamline 的 `bin/`、`lib/`、`include/`、`source/`、`external/` 或 `shaders/` vendor 进引擎
核心。
