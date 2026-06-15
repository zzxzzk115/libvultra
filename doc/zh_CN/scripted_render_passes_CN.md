# Scripted Render Passes (Lua `setup` + `execute`)

[English](../scripted_render_passes.md) | **简体中文**

声明式渲染器支持两类由项目编写的渲染图（render-graph）pass：

| 类型 | Lua 接口 | Execute 主体 |
| --- | --- | --- |
| **声明式**（带 `shader` 表的 `RenderGraphPass`） | 仅声明 shader 与 I/O | 引擎固定（`FullscreenPassRuntime` / `ComputePassRuntime`） |
| **脚本式**（带 `setup` + `execute` 的 `RenderGraphPass`） | 直接驱动 FrameGraph 构建器与命令记录器 | **由 Lua 编写** |

脚本式 pass 是一个完整的 SRP 扩展点：Lua 声明 FrameGraph 的输入与
输出、创建临时目标（transient target）、读取引擎/blackboard 资源、查询
GPU 场景、**按名称**从 shader 库中选择 shader，并自行记录
draw 或 dispatch。

## 编写（Authoring）

脚本式 pass 文件可放在项目下的任意位置（例如
`resources/render/passes/*.lua`），并 `return` 一个包含
`setup` 和 `execute` 函数的 `RenderGraphPass` 表：

```lua
local state = {}   -- shared upvalue: carries values from setup to execute

return RenderGraphPass {
    type    = "MyPass",
    inputs  = { "source" },
    outputs = { "color" },
    params  = {
        { name = "strength", type = "float", default = 0.5 },
    },

    setup = function(ctx)
        local src = ctx:getInput("source")
        ctx:read(src, { set = 3, binding = 0, stage = "fragment" })
        local out = ctx:createColorTexture { name = "MyPass Color", inherit = src }
        ctx:writeColor(out)
        ctx:setOutput("color", out)
        ctx:useGraphicsShader {
            vertexLibrary = "builtin", vertex = "builtin/general/fullscreen_triangle.vert",
            fragmentLibrary = "project", fragment = "project/fullscreen/my_pass.frag",
        }
        state.strength = ctx:paramFloat("strength", 0.5)
    end,

    execute = function(rc)
        if not rc:bindPipeline() then return end
        rc:bindDescriptorSets()
        rc:pushConstants("fragment", { strength = state.strength })
        rc:beginRendering(); rc:drawFullscreen(); rc:endRendering()
    end,
}
```

然后在渲染图（`.vrg.json`）中通过 `type` 引用它：

```json
{ "id": "MyPass", "type": "MyPass", "inputs": { "source": "SsrComposite.color" } }
```

完整的可运行示例请参见 [`resources/render/passes/pixelate.lua`](../../resources/render/passes/pixelate.lua)，
它复用了现有的 `pixelate.frag` shader。该示例
还展示了可选的 `menuPath` 字段，它会把该 pass 放入编辑器的
add-pass 菜单中（例如 `"Post Processing/Pixelate"`）。

## 来自 shader 反射的节点参数

pass 的 shader `[properties]`（name、type、default、`range(...)`、`enum(...)`）会
**自动暴露为渲染图节点参数**，因此编辑器会显示它们并将
覆盖值写入 `.vrg.json`；`pushConstants` 随后按名称把它们读回。

- **声明式项目 pass**（带 `shader` 表）：从片段/计算 shader
  自动反射。
- **脚本式 pass**：在顶层添加一个可选的 `shader = { fragmentLibrary=, fragment= }`
  （或 `compute=`）提示。真正的 shader 仍在
  `setup` 中选择；该提示仅用于驱动反射。显式的 `params = {...}`
  块会覆盖或扩展反射得到的集合。

## `setup(ctx)` API —— FrameGraph 构建上下文

`setup` 在图构建期间运行。`ctx` 对象仅在该调用期间
有效；不要把 FrameGraph 句柄保存到全局变量中（句柄带有 generation
标记，使用过期句柄会引发 Lua 错误）。

| 方法 | 描述 |
| --- | --- |
| `ctx:getInput(slot)` → handle | 解析在 `.vrg.json` 中连接的图输入 |
| `ctx:setOutput(slot, handle)` | 发布一个图输出 |
| `ctx:getResource(name)` → handle\|nil | 按名称读取已发布的引擎资源（例如 `"GBufferColor"`、`"DepthTexture"`） |
| `ctx:setResource(name, handle)` | 按名称发布一个引擎资源 |
| `ctx:createUpscalerOutput { name=, color=, depth=, motion=, exposure=, outputWidth=, outputHeight= }` | 声明一个通用的外部 upscaler 输出。`color`、`depth` 和 `motion` 是常规输入；`exposure` 是可选的，通常应省略以使用 provider 的自动曝光。若省略 `outputWidth/outputHeight`，输出默认采用当前视图目标/backbuffer 的尺寸。Lua 仅提供图句柄；引擎负责构建原生资源标签与命令上下文。 |
| `ctx:createColorTexture { name=, format=, inherit=, storage= }` → handle | 分配一个临时目标（`format`：`rgba16f`/`rgba8`/`rgba32f`；`inherit` 复制某个句柄的描述符；`storage=true` 为计算写入添加 storage 用途） |
| `ctx:read(handle, { set=, binding=, stage=, depth= })` | 声明一个采样读取（`stage`：`fragment`/`compute`） |
| `ctx:writeColor(handle [, index [, clear]])` | 声明一个颜色附件写入 |
| `ctx:writeStorage(handle, { set=, binding=, stage= })` | 声明一个 storage-image 写入 |
| `ctx:useGraphicsShader { vertexLibrary=, vertex=, fragmentLibrary=, fragment= }` | 选择图形 shader |
| `ctx:useComputeShader { library=, compute= }` | 选择一个计算 shader |
| `ctx:paramFloat/Int/Bool/String(name, default)` | 读取一个图参数（在 `params` 中声明） |
| `ctx:sceneDrawCount()` / `ctx:hasGaussianSplats()` / `ctx:isGpuDriven()` | 查询 GPU 场景 |

## `execute(rc)` API —— 命令记录器

`execute` 在命令记录期间运行。

| 方法 | 描述 |
| --- | --- |
| `rc:bindPipeline()` → bool | 为 setup 中选择的 shader 构建/绑定管线；若不可用则返回 `false` |
| `rc:bindDescriptorSets()` | 绑定 FrameGraph 根据已声明的读/写准备好的描述符集 |
| `rc:pushConstants(stage, { name = value, ... })` | 按 shader 反射出的参数名打包 push constants（`stage`：`fragment`/`compute`/`vertex`） |
| `rc:beginRendering()` / `rc:drawFullscreen()` / `rc:endRendering()` | 记录一次全屏三角形 draw |
| `rc:dispatch(x, y, z)` | 记录一次计算 dispatch |
| `rc:evaluateUpscaler()` | 为 `ctx:createUpscalerOutput` 声明的输出评估当前激活的 `IRenderUpscalerService` provider；当 provider 被禁用或不可用时回退为线性 blit。 |
| `rc:dispatchByOutputSize()` | 为每个输出 texel block 分派一个 workgroup（使用绑定的计算管线的 local size 以及最后一次 `createColorTexture` 的尺寸） |

## 注意事项与限制（v1）

- **线程：** 脚本式 `execute` 运行在 render-script 线程（即加载该
  pass 的线程）上。debug 断言会强制保证这一不变量。
- **错误：** `setup`/`execute` 作为受保护函数调用。setup 出错会
  使该 pass 没有记录任何工作；execute 出错则跳过记录。两者
  都会按 pass 记录一次日志。
- **图形管线状态** 目前固定为全屏三角形
  配置（无 depth/blend/cull）。自定义几何体 draw（meshlet/GPU-scene
  indirect）与 FrameGraph **buffer** I/O 是计划中的后续工作；计算 pass
  已经可以通过 storage image 和 dispatch 覆盖任意 buffer 工作。
- **Upscaler 调用是 bridge-safe 的：** 脚本式 pass 可以请求一次 upscaler
  评估，但原生纹理标签与命令缓冲句柄由引擎组装。厂商特定的代码保留在当前激活的原生 provider 中。
