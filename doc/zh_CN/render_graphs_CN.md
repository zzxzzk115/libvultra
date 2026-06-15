# Render Graphs

[English](../render_graphs.md) | **简体中文**

Vultra 采用 SRP 风格（Scriptable Render Pipeline，可编程渲染管线）的渲染器架构：每个
相机都由一个 *renderer* 渲染，其核心是一个在 `.vrg.json` 文件中编写的声明式
**render graph（渲染图）**。本文档从高层概述这些部分如何协同工作，以及如何编写图文件。

本页所补充的深入文档请参见：

- [`gpu_driven_pipeline.md`](gpu_driven_pipeline_CN.md) —— GPU 驱动的网格/meshlet
  管线，以及内置 pass 所驱动的 frame-graph 内部机制。
- [`scripted_render_passes.md`](scripted_render_passes_CN.md) —— 用 Lua 编写 pass 和
  shader 库（`.vrp.lua`、`.vshaderlib.lua`）。

## Renderer architecture（渲染器架构）

渲染由相机驱动。每个 `CameraComponent` 都携带一个 `rendererKey` 字符串，
用于选择由哪个已注册的 renderer 来 cook 该相机的视图。运行时
在启动时注册一小组 renderer，例如在
`source/vultra_app/src/main.cpp` 中：

```cpp
renderService->registerRenderer(createRef<UniversalRenderer>());      // "universal"
renderService->registerRenderer(createRef<UniversalRtRenderer>());    // "universal_rt"
renderService->registerRenderer(
    createRef<DeclarativeRenderer>("builtin://render/ui_editor.vrg.json", "editor-ui2d"));
```

大多数 renderer 都是对 `DeclarativeRenderer`
（`source/vultra/src/function/rendering/srp/declarative_renderer.cpp`）的薄封装。一个
`DeclarativeRenderer` 通过一个图 URI 加上一个 renderer key 构造，例如：

```cpp
m_GraphRenderer = createScope<DeclarativeRenderer>(graphUri, "universal");
```

在构建时它会：

1. 加载并解析 `.vrg.json` 渲染图。
2. 将每个图节点解析为一个具体的 **builtin pass adapter（内置 pass 适配器）**（按
   节点的 `type` 匹配），把节点 `inputs` 连接到上游节点的输出。
3. 将解析后的 pass cook 成一个 **frame graph（帧图）**，声明每个 pass 读写的 GPU 资源
   （纹理/缓冲区），并让 frame graph 对它们进行调度、aliasing 与设置 barrier。

因此 `.vrg.json` 是 *被编写* 的图；frame graph 是某一帧 *被解析、已调度* 的图。
这些 pass 内部实际的网格提交、剔除与 GBuffer 填充属于 GPU 驱动管线 —— 参见
[`gpu_driven_pipeline.md`](gpu_driven_pipeline_CN.md)。本页保持在图这一层级。

## Renderer tiers（渲染器层级）

在 `builtin/render/` 下有三个显式的内置渲染图层级：

| File | Tier | Notes |
| --- | --- | --- |
| `universal.vrg.json` | High-end | 完整延迟路径：深度预 pass、GBuffer、阴影、SSAO/SSR、延迟光照、bloom、色调映射、FXAA、后处理效果。计划进行 `universal_highend` 重命名，但**尚未应用** —— 文件与 key 仍为 `universal`。 |
| `universal_compat.vrg.json` | Compatibility | 面向受限后端（如 WebGPU、Android）的精简的近似前向路径（`CompatibilityBaseColor`）。 |
| `universal_rt.vrg.json` | Ray tracing | `RayTracingPrimary` + 色调映射 + UI 叠加层。 |

### How a tier is selected（如何选择层级）

`UniversalRenderer`（`source/vultra/src/function/rendering/srp/builtin/universal_renderer.cpp`）
在 `init()` 中在 high-end 图与 compatibility 图之间进行选择：

```cpp
const bool useCompatibilityFeature =
    kForceCompatibilityFeature                       // true on Android
    || forceCompatibilityByCli                       // --render-profile=compat
    || backendApi == rhi::RenderBackendApi::eWebGPU; // WebGPU backend

const char* graphUri = useCompatibilityFeature ? "builtin://render/universal_compat.vrg.json"
                                               : "builtin://render/universal.vrg.json";
```

- **`rendererKey`（每相机）** 选择 *哪个 renderer*：`"universal"`、
  `"universal_rt"`、`"editor-ui2d"` 等。没有 key 的相机默认为
  `"universal"`。
- **`--render-profile=<token>`** 在 `universal` renderer 上强制使用某个层级。可接受的
  token 为 `default | universal | compat | compatibility`（参见
  `demo_app_host.cpp` 中的 `parseRenderProfileToken` 以及
  `source/vultra_app/src/launch_options.cpp` 中的 `--render-profile` 参数）。`compat`/`compatibility` 映射到
  `RenderProfile::eCompatibility`，它会强制使用 compatibility 图。
- 光线追踪通过一个独立的 renderer（`universal_rt`）到达，而不是通过 profile
  token —— 将相机的 `rendererKey` 设为 `"universal_rt"`。

## The `.vrg.json` format（`.vrg.json` 格式）

一个渲染图是 JSON，包含三个顶层小节：`resources`、`passes` 以及一个
仅供编辑器使用的 `meta` 块。下面是来自
`builtin/render/universal.vrg.json` 的一段真实（精简后的）摘录：

```json
{
  "resources": [
    { "name": "backbuffer" }
  ],
  "passes": [
    { "enabled": true, "id": "DirectDepthPre", "type": "DirectDepthPre" },
    {
      "enabled": true,
      "id": "DirectGBuffer",
      "type": "DirectGBuffer",
      "inputs": { "depth": "DirectDepthPre.depth" }
    },
    {
      "enabled": true,
      "id": "DeferredLighting",
      "type": "DeferredLighting",
      "inputs": {
        "color":  "DirectGBuffer.color",
        "depth":  "DirectGBuffer.depth",
        "normal": "DirectGBuffer.normal",
        "shadowMap": "ShadowMap.shadowMap"
      },
      "params": { "ambientIntensity": 1.0, "shadowStrength": 0.85 }
    },
    {
      "enabled": true,
      "id": "FinalComposition",
      "type": "FinalComposition",
      "inputs":  { "source": "UiOverlay.color" },
      "outputs": { "target": "backbuffer" }
    }
  ],
  "version": 3
}
```

字段：

- **`version`** —— 图 schema 版本（当前为 `3`）。
- **`resources`** —— 图导入/导出的、具名的外部资源。
  `backbuffer` 是最终 pass 写入的 swapchain 目标。
- **`passes`** —— 节点列表。每个节点包含：
  - `id` —— 唯一的节点实例名（用于引用其输出）。
  - `type` —— 要实例化的 builtin（或脚本化）pass 适配器。
    `DeclarativeRenderer` 将 `type` 匹配到一个已注册的 pass。
  - `enabled` —— 该节点是否被 cook 进 frame graph。
  - `inputs` —— 一个 `slotName -> "<NodeId>.<output>"` 的映射。每个值引用
    另一个节点的输出 slot，从而构成依赖边（例如
    `"DirectGBuffer.depth"` 读取 `DirectGBuffer` 节点的 `depth` 输出）。
  - `outputs` —— 可选映射，将某个节点输出绑定到一个具名的图资源（上面只有
    `FinalComposition` 将 `target` 绑定到 `backbuffer`）。
  - `params` —— 每个 pass 的可调项（布尔、数值、枚举）。它们与
    renderer UI 面板中暴露的设置相同。
- **`meta.editor.nodes`** —— 仅供编辑器使用的画布布局（每个节点的 `pos`）。它对
  渲染没有任何影响，并由编辑器重新生成。

光线追踪图额外为每个节点使用一个 `viewMode` 字段（`"inherit"`），
而 compatibility 图只保留少数几个节点 —— 两者都是很好的最小化
参考。

## Project file types（项目文件类型）

一个项目可以贡献四种渲染相关的文件类型：

| Extension | Purpose | See |
| --- | --- | --- |
| `.vrg.json` | 声明式渲染图（节点、资源、params、编辑器布局）。 | 本文档 |
| `.vrp.lua` | Lua 编写的渲染管线 / pass 定义（`setup`/`execute`）。 | [`scripted_render_passes.md`](scripted_render_passes_CN.md) |
| `.vshaderlib.lua` | 项目 shader 库声明与 shader glob。 | [`scripted_render_passes.md`](scripted_render_passes_CN.md) |
| `.vmatgraph.json` | 编译为生成的 shader 源码的材质图。 | [`material_custom_nodes.md`](material_custom_nodes_CN.md) |

项目自带自己的图（例如 `res://render/default.vrg.json`），它可以被热
重载，并以与内置图相同的方式被选择。

## The Render Graph editor（Render Graph 编辑器）

编辑器提供了一个专用的 Render Graph 窗口
（`source/vultra_app/src/editor_app/ui/windows/render_graph_window.cpp`），构建于
`imnodes` 之上。它提供：

- **实时运行时预览** —— 一个 16:9 的预览，在你编辑时渲染当前场景的图，
  使参数改动立即可见。
- **运行时图检视** —— 它回读已解析的运行时 frame graph
  （节点、边、kind），让你看到声明式图实际 cook 成了什么。
- **资源缩略图** —— frame-graph 纹理（GBuffer 目标、
  中间渲染目标）的小型预览会被缓存并按资源显示。
- **节点放置与连线** —— 节点可以被放置在画布上、slot 到
  slot 地连接，并就地编辑其 `params`；布局会被持久化进
  `meta.editor.nodes`。

编辑会被保存回 `.vrg.json`，并且管线可以通过
`renderService->reloadRenderPipeline(uri, rendererKey)` 原地重载（也通过
运行时 MCP `reload_pipeline` 工具暴露）。

## Extending the renderer（扩展渲染器）

项目以两种互补的方式扩展或替换渲染：

1. **声明式** —— 编辑 `.vrg.json`：切换 `enabled`、重新调节 `params`、
   重新连线 `inputs`，或将某个节点的 `type` 换成不同的 pass。由于图是
   数据，无需重新编译。
2. **通过 Lua pass** —— 在 `.vrp.lua` 中编写自定义的 `setup`/`execute` pass，并
   从图节点的 `type` 引用它们，shader 库则在
   `.vshaderlib.lua` 中声明。参见 [`scripted_render_passes.md`](scripted_render_passes_CN.md)。

插件也可以贡献 pass 和 shader（以及 upscaler），项目随后将其连线
进自己的图中 —— 参见 [`plugins.md`](plugins_CN.md) 与
[`render_upscaler_plugins.md`](render_upscaler_plugins_CN.md)。
