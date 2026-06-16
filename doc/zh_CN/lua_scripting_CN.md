# Vultra Lua Scripting

[English](../lua_scripting.md) | **简体中文**

Vultra 的玩法脚本是由 `ScriptComponent::scriptUri` 引用的 Lua 文件。
它们仅在编辑器/运行时回放处于激活状态时运行。脚本通过资源系统加载，
因此请使用诸如 `res://scripts/player.lua` 这样的引擎 URI，而不是原始的
文件系统路径。

> API 作者注意：Lua 接口遵循
> [lua_api_design.md](lua_api_design_CN.md) 中的规范性规则（命名、属性与方法、
> 单位、错误/nil 约定、弃用）。新增或变更的绑定必须保持
> `tests/lua_api_conformance` 通过。

## Lifecycle

在脚本中定义以下任意函数：

```lua
function OnCreate(self)
end

function OnEnable(self)
end

function OnUpdate(self, dt)
end

function OnFixedUpdate(self, fixedDt)
end

function OnDisable(self)
end

function OnDestroy(self)
end
```

`self` 是拥有该 `ScriptComponent` 的实体。

- `OnCreate` 在回放开始或脚本被加载/重新加载时调用。
- `OnEnable` 在脚本以启用状态启动时于 `OnCreate` 之后调用，并在
  `ScriptComponent.enabled` 重新变为 true 时调用。
- `OnUpdate` 在回放未暂停时每渲染一帧调用一次。
- `OnFixedUpdate` 在每个物理固定步长中调用一次。
- `OnDisable` 在组件被禁用时调用，并且总是在 `OnDestroy` 之前调用。
  脚本拥有的协程在此停止。
- `OnDestroy` 在脚本实例被销毁时调用。

保证的顺序：`OnCreate -> OnEnable -> updates... -> OnDisable -> OnDestroy`。

实体句柄是轻量级的 C++ 引用。在跨帧或场景重新加载后使用缓存的句柄之前，
请始终检查 `entity.valid`。

### Collision And Trigger Callbacks

参与物理接触的任一实体上的脚本都会收到回调，这些回调在每次物理更新时
通过接触对差分（diffing）分发一次（`other` 是发生碰撞的实体）：

```lua
function OnCollisionEnter(self, other)
end

function OnCollisionStay(self, other)
end

function OnCollisionExit(self, other)
end

function OnTriggerEnter(self, other) -- either body has isSensor = true
end

function OnTriggerStay(self, other)
end

function OnTriggerExit(self, other)
end
```

通过 `Physics.contactEvents()` 进行轮询仍然有效，并且不受这些回调的影响。
在退出（exit）事件中，`other` 可能已经失效——在使用它之前请检查
`other.valid`。

### 动画事件回调（Animation Event Callback）

如果实体的动画状态机定义了关键帧事件（见 **Animation**），当播放越过某个事件的时间点时，
脚本的 `OnAnimationEvent(self, name)` 会在该帧被调用：

```lua
function OnAnimationEvent(self, name)
  if name == "footstep" then
    Audio.play(self) -- 播放脚步声
  end
end
```

事件在当前播放的状态上触发、随片段循环重复，并在状态切换时重置（进入某状态时不会“补发”
之前的事件）。

### Debug UI (ImGui)

在编辑器和开发构建中（当 ImGui 服务存在时），脚本可以通过 `ImGui` 表
绘制调试 UI。它保留上游 Dear ImGui 的 PascalCase 命名（这是
[lua_api_design.md](lua_api_design_CN.md) 中记录的例外情况），因此上游文档
可直接适用。在 ImGui 帧之外的调用会抛出错误；在已发布的无头（headless）
运行时中，`ImGui` 全局变量不存在——请用 `if ImGui then` 进行防护。

```lua
function OnUpdate(self, dt)
  if not ImGui then return end
  local visible = ImGui.Begin("Debug")
  if visible then
    ImGui.Text("entity: " .. self.name)
    if ImGui.Button("Reset") then
      self.transform.position = Vec3(0, 0, 0)
    end
    local changed, v = ImGui.SliderFloat("speed", Player.speed, 0, 10)
    if changed then Player.speed = v end
  end
  ImGui.End()
end
```

输出参数（out-parameters）会成为额外的返回值（`changed, newValue`）。例如，
`ImGui.InputFloat("speed", Player.speed)` 和 `ImGui.InputInt("count", n)`
返回 `changed, newValue`。枚举/标志值位于子表中：
`ImGui.WindowFlags.NoTitleBar`、`ImGui.Cond.Once`。该绑定由
dear_bindings 元数据（`tools/python/gen_imgui_lua.py`）生成。

### ImGui 扩展（imgui-ext）

若干 imgui-ext 控件以各自的表暴露，并保留其上游 PascalCase 命名（与 `ImGui` 一致）。它们仅在
编辑器 / 开发构建中存在（需要 ImGui 服务处于激活状态）。矩阵为 16 个数字的 Lua 数组（列主序）；
vec3 为 3 个数字的数组。

- **`ImGuizmo`** —— 3D 变换 gizmo：`Manipulate(view, projection, operation, mode, matrix[, snap])
  -> changed, newMatrix`，以及 `SetRect`、`SetDrawlist`、`SetOrthographic`、`Enable`、`IsOver`、
  `IsUsing`、`IsUsingAny`、`DecomposeMatrixToComponents` / `RecomposeMatrixFromComponents`。操作
  位于 `ImGuizmo.OPERATION`（`TRANSLATE`/`ROTATE`/`SCALE`/`UNIVERSAL`/…），模式位于 `ImGuizmo.MODE`
  （`LOCAL`/`WORLD`）。
- **`ImOGuizmo`** —— 朝向立方体：`SetRect`、`BeginFrame`、
  `DrawGizmo(view, projection[, pivotDistance]) -> interacted, newView`。
- **`ImPlot`** —— 绘图：用 `BeginPlot(title[, sizeX, sizeY, flags])` / `EndPlot()` 包裹，然后
  `PlotLine`/`PlotScatter`/`PlotBars(label, ys)` 或 `(label, xs, ys)`。辅助函数：`SetupAxes`、
  `SetupAxesLimits`、`SetupLegend`。枚举：`ImPlot.Axis`、`ImPlot.Flags`、`ImPlot.AxisFlags`、
  `ImPlot.Location`。
- **`ImGuiFileDialog`** —— 模态文件 / 文件夹选择器：`OpenDialog(key, title, filters[, path])`，
  随后每帧调用 `Display(key)`；当其返回 true 时检查 `IsOk()` 并读取 `GetFilePathName()` /
  `GetCurrentPath()` / `GetSelection()`，最后 `Close()`。
- **`ImNodes`** —— 节点图编辑器：用 `BeginNodeEditor()`/`EndNodeEditor()` 包裹；每个节点用
  `BeginNode(id)`/`EndNode()`，配合 `BeginInputAttribute`/`BeginOutputAttribute` 引脚与
  `Link(id, startAttr, endAttr)`。用 `IsLinkCreated()`/`IsLinkDestroyed()` 查询交互（各自返回相关
  的 id）。

```lua
-- 编辑器面板里的一个小图表
if ImPlot.BeginPlot("Frame time (ms)", -1, 160) then
  ImPlot.SetupAxes("frame", "ms")
  ImPlot.PlotLine("dt", samples)   -- samples 是一个数字数组
  ImPlot.EndPlot()
end
```

### Coroutines

每个脚本都可以运行跨帧持续存在的协程：

```lua
function OnCreate(self)
  startCoroutine(function()
    print("first slice runs immediately")
    wait(1.5)            -- seconds
    print("1.5s later")
    waitFrames(10)       -- frames
    print("10 frames later")
  end)
end
```

- `startCoroutine(fn, ...)` 立即运行第一个切片并返回该协程；额外的参数
  会传递给 `fn`。
- `wait(seconds)` / `waitFrames(n)` 挂起当前协程。
- `stopAllCoroutines()` 取消该脚本的协程；它们也会在 `OnDisable` 和
  `OnDestroy` 时自动停止。

### 补间、定时器、时间线（Tween, Timer, Timeline）

逐帧驱动的游戏性工具（全局表，由脚本系统每帧推进一次）。与协程不同，它们是全局的、并非
按实体管理，因此请取消不再需要的句柄，并在回调中对涉及实体的操作做保护（实体可能已被销毁）。

```lua
-- 把任意表的数值字段补间到目标值（在 duration 秒内）。
local pos = self.transform.position
Tween.to(pos, { y = pos.y + 2 }, 0.5, {
  ease = Ease.cubicOut,
  onUpdate = function(t) self.transform.position = pos end, -- 每帧写回
  onComplete = function() print("done") end,
})

-- 一次性与重复定时器。
Timer.after(2.0, function() print("2s later") end)
local h = Timer.every(0.5, function() spawnEnemy() end) -- fn 返回 false 可停止
Timer.cancel(h)

-- 一个小型有序时间线（类似过场）。
Timeline.new()
  :call(function() openDoor() end)
  :wait(1.0)
  :call(function() spawnBoss() end)
  :start()
```

- `Tween.to(obj, toFields, duration, opts?)` -> 句柄。`opts = { ease, onUpdate(t),
  onComplete }`。`Tween.cancel(handle)`。
- `Timer.after(seconds, fn)` / `Timer.every(seconds, fn)`（fn 返回 `false` 可停止）-> 句柄。
  `Timer.cancel(handle)`。
- `Timeline.new():wait(s):call(fn):start()` -> 句柄（可链式调用）。
- `Ease`：`linear`、`quadIn/Out/InOut`、`cubicIn/Out/InOut`、`sineIn/Out/InOut`、
  `expoIn/Out`、`backOut`、`bounceOut`。

## Binding Parity For Engine Work

面向玩法的引擎工作应当留下一个可用的 Lua 接口。当某个系统、组件、服务
或编辑器中创作的行为与玩家相关时：

- 决定玩法脚本是否需要访问它；
- 检查附近现有的子系统功能是否缺失绑定；
- 在暴露 Lua 接口之前，先实现缺失的运行时服务或数据层行为；
- 绑定调用引擎服务/组件的薄包装层；
- 更新本文档以及生成游戏所使用的项目本地 `ai/` 笔记。

## Value Types

使用构造函数来创建向量值：

```lua
local a = Vec3(1, 2, 3)
local b = Vec3(4, 5, 6)
```

向量绑定暴露以下字段：

- `Vec2`：`x`、`y`
- `Vec3`：`x`、`y`、`z`
- `Vec4`：`x`、`y`、`z`、`w`

向量支持基本算术运算：

- `a + b`
- `a - b`
- `-a`
- `v * scalar`
- `scalar * v`
- `v / scalar`

数学辅助函数：

- `dot(a, b)`
- `lengthSquared(v)`

当你不需要精确的平方根时，使用 `lengthSquared` 进行距离检查：

```lua
local delta = pickup.transform.position - player.transform.position
if lengthSquared(delta) < 1.0 then
  pickup.visible = false
end
```

## World And Entities

`World` 表暴露基本的实体查找与生命周期：

```lua
local player = World.findByName("Player")
local pickups = World.findByNamePrefix("Pickup")
local spawned = World.create("Runtime Entity")
World.destroy(spawned)
```

可用函数：

- `World.create(name?)`
- `World.destroy(entity)`
- `World.count()`
- `World.findByName(name)`
- `World.findByNamePrefix(prefix)`
- `World.entities()`

实体属性（句柄上唯一的直接字段）：

- `entity.valid`
- `entity.id`
- `entity.name`
- `entity.active`
- `entity.visible`
- `entity.transform`

实体方法使用 `:` 语法：

```lua
entity:destroy()
entity:setParent(parent)

local parent = entity:parent()
local child = entity:firstChild()
local sibling = entity:nextSibling()
```

### Components

除了始终存在的 `transform` 之外，每一个组件都通过一套以 `Component.*`
标记为键的、单一的 Unity 风格通用 API 来访问，而不是逐组件的属性：

```lua
local body = self:addComponent(Component.RigidBody) -- adds if absent, returns the ref
local cam = self:getComponent(Component.Camera)     -- ref, or nil if absent
local had = self:removeComponent(Component.Light)   -- true if one was removed

if self:hasComponent(Component.Mesh) then
  self:getComponent(Component.Mesh):setMaterial(0, "res://materials/red.vmat.json")
end
```

- `entity:addComponent(Component.X)` 在组件不存在时添加它，并返回其引用。
- `entity:getComponent(Component.X)` 返回组件引用，当实体没有该组件时返回
  `nil`。
- `entity:removeComponent(Component.X)` 在移除了一个组件时返回 `true`。
- `entity:hasComponent(Component.X)` 返回一个布尔值。

返回的引用就是下文各节中记录的同样的有类型组件引用
（`RigidBody`、`Camera`、`Mesh`、`Animator`、……）；只有获取它们的方式
改变了。`getComponent` 返回 `nil` 是使用前惯用的存在性检查：

```lua
local body = self:getComponent(Component.RigidBody)
if body then
  body:addForce(Vec3(10, 0, 0))
end
```

`Component` 是一个由有类型标记组成的全局枚举表：

- `Component.Transform`、`Component.RigidBody`、`Component.Camera`、
  `Component.Light`、`Component.Mesh`
- `Component.BoxShape`、`Component.SphereShape`、`Component.CapsuleShape`、
  `Component.CylinderShape`
- `Component.Animator`、`Component.AudioSource`、`Component.AudioListener`
- `Component.Environment`、`Component.ParticleEmitter`、
  `Component.ReflectionProbe`
- `Component.RectTransform`、`Component.UiButton`、`Component.UiToggle`、
  `Component.UiSlider`、`Component.UiProgressBar`

### WorldHelper

`WorldHelper` 是一个便捷构造函数的全局表，每个构造函数都会创建一个
已附加常用组件的实体，并返回这个新句柄：

```lua
local empty = WorldHelper.addEmpty("Spawn")
local cam = WorldHelper.addMainCamera("Main Camera") -- camera with primary = true
```

- `WorldHelper.addEmpty(name?)` —— 仅 transform
- `WorldHelper.addMainCamera(name?)` —— 带 `primary = true` 的相机
- `WorldHelper.addCamera(name?)`
- `WorldHelper.addLight(name?)`
- `WorldHelper.addMesh(name?)`

## Mesh Materials

网格脚本可以为网格槽位分配材质资源以及逐实体的属性覆盖，而无需编辑
共享的 `.vmat.json`：

```lua
local mesh = self:getComponent(Component.Mesh)
mesh:setMaterial(0, "res://materials/red.vmat.json")
mesh:setMaterialFloat(0, "roughness", 0.8)
mesh:setMaterialColor(0, "baseColor", Vec4(1, 0, 0, 1))
mesh:setMaterialTexture(0, "baseColorTexture", "res://textures/albedo.png")
mesh:clearMaterialProperty(0, "roughness")
mesh:clearMaterialProperties(0)
```

`setMaterial` 期望一个 `.vmat.json` URI。属性方法会写入槽位级别的
`MaterialPropertyBlock`，因此两个实体可以共享一个材质资源，同时使用
不同的运行时值。Lua 的写入影响当前实体/组件状态，不会改动材质资源；
Inspector 中创作的属性块是场景创作数据，并在场景保存时持久化。

## Transforms And Cameras

Transform 访问：

```lua
local p = self.transform.position
self.transform.position = Vec3(p.x, p.y + 1, p.z)
self.transform.scale = Vec3(1, 1, 1)
self.transform.rotation = Vec3(0, 45, 0) -- euler degrees
self.transform:translate(Vec3(0, 0, 1))
self.transform:lookAt(Vec3(0, 0, 0))
```

如果实体具有 `RectTransformComponent`，它会被视为 UI 实体。
`entity.transform.position`、`scale`、`rotation` 和 `translate` 会转发到
RectTransform 的像素字段，而不是隐藏的 3D `TransformComponent`。

## UI

运行时 UI V1 是屏幕空间的，使用相对于所属 Canvas 参考分辨率创作的像素。
RectTransform、布局间距、内边距、外边距、文本大小以及按钮命中矩形都是
在 Canvas 缩放之前的像素值。

RectTransform 访问（通过 `Component.RectTransform` 获取）：

```lua
local rect = self:getComponent(Component.RectTransform)
rect.anchoredPositionPx = Vec2(320, 180)
rect.sizeDeltaPx = Vec2(240, 64)
rect.rotation = 0
rect.scale = Vec2(1, 1)
```

按钮与指针状态：

```lua
if UI.isPointerOverUI() then
  local hovered = UI.hoveredEntity()
end

local button = self:getComponent(Component.UiButton)
if button and button.clickedThisFrame then
  print("Clicked")
end
```

基于信号（signal）的 UI 事件是推荐的创作风格。按钮引用上的 `onClick`
信号从命中目标开始向上冒泡，穿过其父级，直到所属的 Canvas：

```lua
function OnCreate(self)
  local button = self:getComponent(Component.UiButton)
  if button then
    button.onClick:connect(function(event)
      print("Clicked", event.target.name)
      event:stopPropagation()
    end)
  end
end
```

常见的 UI 控件以同样的方式暴露薄组件引用：

```lua
function OnCreate(self)
  local toggle = self:getComponent(Component.UiToggle)
  if toggle then
    toggle.checked = true
    toggle.onClick:connect(function(event)
      print("toggle", toggle.checked)
    end)
  end

  local slider = self:getComponent(Component.UiSlider)
  if slider then
    slider.minValue = 0
    slider.maxValue = 100
    slider.value = 50
  end

  local progress = self:getComponent(Component.UiProgressBar)
  if progress then
    progress.value = 0.5
  end
end
```

UI 信号从命中目标开始向上冒泡，穿过其父级，直到所属的 Canvas。
`event.target` 是最初被命中的实体，`event.currentTarget` 是当前正在运行
信号的实体，`event:stopPropagation()` 会阻止针对同一源事件的后续父级信号。

UI 函数：

- `UI.isPointerOverUI()`
- `UI.hoveredEntity()`
- `UI.pressedEntity()`
- `UI.raycast(screenPosition?)`
- `UI.events()`

UI 组件引用（通过 `Component.RectTransform`、`Component.UiButton`、
`Component.UiToggle`、`Component.UiSlider`、`Component.UiProgressBar` 获取）：

- `RectTransform`：`anchorMin`、`anchorMax`、`pivot`、`anchoredPositionPx`、
  `sizeDeltaPx`、`scale`、`rotation`
- `UiButton`：`interactable`、`hovered`、`pressed`、`clickedThisFrame`、`onClick`
- `UiToggle`：`interactable`、`checked`、`onClick`
- `UiSlider`：`interactable`、`value`、`minValue`、`maxValue`
- `UiProgressBar`：`value`、`minValue`、`maxValue`

UI 事件字段为 `type`、`target`、`currentTarget`、`canvas`、
`screenPosition`、`canvasPosition`、`localPosition`、`button`、`clickCount`
和 `handled`。

主相机查找：

```lua
local camera = Camera.findPrimary()
if camera.valid then
  camera.transform.position = Vec3(0, 6, 8)
  camera.transform:lookAt(self.transform.position)
end
```

`Camera` 引用（通过 `entity:getComponent(Component.Camera)` 获取）暴露：

```lua
local cam = camera:getComponent(Component.Camera)
if cam then
  cam.primary = true
  cam.fovY = 60
end
```

- `camera.valid`
- `camera.primary`
- `camera.projection`
- `camera.fovY`
- `camera.orthographicHeight`
- `camera.cullingMask`
- `camera.rendererKey`

层级掩码常量可通过 `Layer.Default`、`Layer.UI` 和 `Layer.All` 获取。

## Input

键盘和鼠标输入通过 `Input` 暴露。

```lua
if Input.isKeyHeld(KeyCode.W) then
  print("W is held")
end

if Input.isKeyPressed(KeyCode.Space) then
  print("Space pressed this frame")
end
```

键盘函数：

- `Input.isKeyHeld(key)`
- `Input.isKeyPressed(key)`
- `Input.isKeyReleased(key)`
- `Input.isKeyRepeated(key)`

鼠标函数：

- `Input.isMouseButtonHeld(button)`
- `Input.isMouseButtonPressed(button)`
- `Input.isMouseButtonReleased(button)`
- `Input.mouseButtonClicks(button)`
- `Input.mousePosition()`
- `Input.mousePositionFlipY()`
- `Input.mousePositionDelta()`
- `Input.mouseScrollDelta()`

请使用 `KeyCode` 和 `MouseCode` 枚举值，而不是整数字面量。

### 手柄（Gamepad）

第一个连接的手柄通过 `Input` 暴露（基于 SDL_Gamepad，采用 Xbox 风格命名）。

```lua
if Input.isGamepadConnected() then
  local lx = Input.gamepadAxis(GamepadAxis.LeftX)       -- 摇杆：[-1, 1]
  local lt = Input.gamepadAxis(GamepadAxis.LeftTrigger) -- 扳机：[0, 1]
  if Input.isGamepadButtonPressed(GamepadButton.South) then -- A / Cross
    Input.rumble(0.6, 0.6, 200) -- low、high 取值 [0,1]；duration 单位毫秒
  end
end
```

- `Input.isGamepadConnected()`
- `Input.isGamepadButtonHeld(button)` / `Input.isGamepadButtonPressed(button)` / `Input.isGamepadButtonReleased(button)`
- `Input.gamepadAxis(axis)`
- `Input.rumble(lowFrequency, highFrequency, durationMs)`

请使用 `GamepadButton`（`South`/`East`/`West`/`North`、`Start`、`Back`、`Guide`、
`LeftShoulder`/`RightShoulder`、`LeftStick`/`RightStick`、`DpadUp`/`DpadDown`/`DpadLeft`/
`DpadRight`）和 `GamepadAxis`（`LeftX`/`LeftY`/`RightX`/`RightY`、`LeftTrigger`/
`RightTrigger`）枚举值。

### 输入动作（项目级映射）

类似 Godot 的具名动作，让游戏逻辑绑定到“意图”（如 "Jump"）而非具体按键，并支持玩家重新
绑定。动作定义在项目文件 `res://input.actions.json` 中，于启动时加载。每个动作包含一个
`deadzone`（死区）和一组 `events`；只要任一 event 处于激活状态，该动作即被激活。

```json
{
  "actions": {
    "Jump":  { "deadzone": 0.5, "events": [ {"key":"Space"}, {"gamepadButton":"South"} ] },
    "MoveX": { "deadzone": 0.2, "events": [ {"gamepadAxis":"LeftX"}, {"key":"D","scale":1}, {"key":"A","scale":-1} ] }
  }
}
```

event 类型：`key`、`mouseButton`、`gamepadButton`、`gamepadAxis`。可选的 `scale` 设置该
event 模拟量贡献的符号/幅度（例如轴的“向左”键用 `-1`）。名称使用去掉前缀的枚举形式，与
`KeyCode`/`GamepadButton`/`GamepadAxis` 表一致（`Space`、`South`、`LeftX`）。

```lua
if Input.isActionPressed("Jump") then jump() end   -- 当前帧按下边沿
local move = Input.actionAxis("MoveX")              -- 带符号 [-1, 1]，已应用死区
```

- `Input.hasAction(name)`
- `Input.isActionHeld(name)`（当前按住）/ `Input.isActionPressed(name)` /
  `Input.isActionReleased(name)`（当前帧边沿，语义同 `isKeyHeld` 与 `isKeyPressed`）
- `Input.actionAxis(name)`（合并后的带符号模拟量）

运行时重绑定（仅内存；持久化请编辑 JSON）：

- `Input.clearActionEvents(name)`
- `Input.bindActionKey(name, key)` / `Input.bindActionMouseButton(name, button)`
- `Input.bindActionGamepadButton(name, button)` / `Input.bindActionGamepadAxis(name, axis, scale)`

完整示例 —— 挂到一个实体上；配合上面的动作映射，它会随 WASD 或左摇杆移动，按 Space /
South 键跳跃（带震动），并读取原始手柄输入：

```lua
function OnCreate(self)
  print("gamepad connected = " .. tostring(Input.isGamepadConnected()))
  -- 运行时重绑定：让 North/Y 键也能触发 Jump。
  Input.bindActionGamepadButton("Jump", GamepadButton.North)
end

function OnUpdate(self, dt)
  if Input.isActionPressed("Jump") then
    Input.rumble(0.6, 0.6, 200) -- 短促震动
  end

  -- 合并摇杆 + WASD 的模拟量移动。
  local moveX = Input.actionAxis("MoveX")
  local moveY = Input.actionAxis("MoveY")
  if math.abs(moveX) > 0.0 or math.abs(moveY) > 0.0 then
    local t = self.transform
    local pos = t.position
    pos.x = pos.x + moveX * dt * 4.0
    pos.z = pos.z + moveY * dt * 4.0
    t.position = pos
  end

  -- 原始设备层。
  if Input.isGamepadButtonPressed(GamepadButton.Start) then
    print("left trigger = " .. string.format("%.2f", Input.gamepadAxis(GamepadAxis.LeftTrigger)))
  end
end
```

## Physics

刚体通过实体上的 `RigidBody` 引用或全局的 `Physics` 表来控制。

```lua
function OnFixedUpdate(self, fixedDt)
  local body = self:getComponent(Component.RigidBody)
  if not body then
    return
  end

  body:addForce(Vec3(10, 0, 0))
  body:activate()
end
```

刚体属性：

- `rigidBody.valid`
- `rigidBody.linearVelocity`
- `rigidBody.angularVelocity`
- `rigidBody.motionType`
- `rigidBody.objectLayer`
- `rigidBody.isSensor`
- `rigidBody.motionQuality`
- `rigidBody.allowSleeping`
- `rigidBody.mass`
- `rigidBody.overrideMass`
- `rigidBody.friction`
- `rigidBody.restitution`
- `rigidBody.linearDamping`
- `rigidBody.angularDamping`
- `rigidBody.gravityFactor`
- `rigidBody.maxLinearVelocity`
- `rigidBody.maxAngularVelocity`

刚体方法：

- `rigidBody:activate()`
- `rigidBody:addForce(force)`
- `rigidBody:addImpulse(impulse)`

Physics 表：

- `Physics.enabled()`
- `Physics.setEnabled(enabled)`
- `Physics.bodyCount()`
- `Physics.hasBody(entity)`
- `Physics.fixedTimeStep()`
- `Physics.setFixedTimeStep(seconds)`
- `Physics.addForce(entity, force)`
- `Physics.addImpulse(entity, impulse)`
- `Physics.addTorque(entity, torque)`
- `Physics.addAngularImpulse(entity, impulse)`
- `Physics.setPosition(entity, position, activate?)`
- `Physics.setRotation(entity, eulerDegrees, activate?)`
- `Physics.gravity()` / `Physics.setGravity(vec3)`
- `Physics.setLayerCollision(layerA, layerB, enabled)` / `Physics.layerCollision(layerA, layerB)`
- `Physics.raycast(origin, direction, maxDistance, activeOnly?, layerMask?)`
- `Physics.raycastAll(origin, direction, maxDistance, activeOnly?, layerMask?)` — 命中数组，由近到远
- `Physics.sphereCast(origin, direction, radius, maxDistance, activeOnly?, layerMask?)` — 扫掠球体
- `Physics.overlapSphere(center, radius, activeOnly?, layerMask?)`
- `Physics.overlapBox(center, halfExtents, activeOnly?, layerMask?)`
- `Physics.overlapCapsule(center, halfHeight, radius, activeOnly?, layerMask?)`
- `Physics.contactPairs(activeOnly?)`
- `Physics.contactEvents()` — 排空自上次调用以来的接触/触发事件

查询现在使用真实的 Jolt 窄相位（narrow-phase）几何（而不是 AABB 近似），
并返回精确的命中点/法线。`layerMask` 是一个在逻辑碰撞层索引
（`RigidBodyComponent.objectLayer`）上的 32 位掩码：第 `i` 位被置位
表示"包含第 `i` 层上的物体"。用它来限定射击/查询的范围（例如忽略玩家层）。
`Physics.sphereCast` 返回一个表 `{hit, entity, point, normal,
distance, fraction, startPenetrating}`。

### Character controller

FPS/TPS 移动使用 `CharacterControllerComponent`（Jolt 的 `CharacterVirtual`）：
碰撞-滑动（collide-and-slide）、坡度限制、楼梯踏步以及地面检测。通过
`Character` 表来驱动它：

```lua
function OnFixedUpdate(self, fixedDt)
  Character.move(self, Vec3(Input.axisX() * 4.0, 0, Input.axisZ() * 4.0))
  if Input.isKeyPressed("space") and Character.isGrounded(self) then
    Character.jump(self, 6.0)
  end
end
```

- `Character.has(entity)`
- `Character.move(entity, horizontalVelocity)` — 期望的水平 (x,z) 速度
- `Character.jump(entity, speed?)` — 请求跳跃（当 `speed` 省略/为 0 时使用组件的 `jumpSpeed`）
- `Character.isGrounded(entity)`
- `Character.velocity(entity)` / `Character.groundNormal(entity)`
- `Character.setPosition(entity, position)` — 瞬移（teleport）

`Physics.overlapSphere` 返回一个 Lua 数组，包含其形状与查询球体重叠的
活动刚体实体。它当前支持球体（sphere）、盒体（box）和胶囊体（capsule）
形状组件。`activeOnly` 默认为 `true`。

`Physics.raycast` 返回一个 `PhysicsRaycastHit`：

- `hit`
- `entity`
- `point`
- `normal`
- `fraction`
- `distance`

`Physics.contactPairs` 返回带有 `a` 和 `b` 实体字段的 `PhysicsContactPair`
值。这些是来自物理系统的玩法接触快照。

`Physics.contactEvents()` 返回并清空自上次调用以来捕获的离散接触/触发
事件——一个 `{a, b, type, isSensor}` 数组，其中 `type` 为
`"enter"`（新接触）或 `"exit"`（接触结束）。当 `isSensor` 为 true 时，该
事件是一个触发体积（trigger volume）的进入/退出（其中一个物体的
`rigidBody.isSensor = true`）。每帧轮询它用于拾取物、伤害区域以及命中检测。
示例：

```lua
function OnUpdate(self, dt)
  for _, e in ipairs(Physics.contactEvents()) do
    if e.isSensor and e.type == "enter" then
      -- something entered a trigger volume (e.a / e.b are the entities)
    end
  end
end
```

对于动态刚体，优先使用 `OnFixedUpdate` 加力或冲量。避免在动态物体上
每渲染一帧就写入 `Transform.position`，除非该实体有意是运动学的
（kinematic）或在物理模拟之外脚本化控制的。

### 约束 / 关节（Constraints / joints）

用 Jolt 约束连接两个刚体（铰链 hinge、固定 fixed、距离 distance、滑动 slider、点 point、
锥形 cone）。第二个物体传 `nil` 表示锚定到世界。`point`/`axis` 为创建时的世界空间坐标；
角度为度，距离为米。每个 `add*` 返回一个不透明的约束 id（失败返回 `0`），用于
`removeConstraint` / 电机。

```lua
-- 一扇铰链门，锚定到世界，并用电机驱动开门。
local hinge = Physics.addHingeConstraint(door, nil, Vec3(0, 1, -1), Vec3(0, 1, 0), -110, 0)
Physics.setConstraintMotor(hinge, true, 90, 200) -- 目标 90 度/秒，最大力矩 200

-- 绳索：让两个物体保持在 [0.5, 4] 米之间。
local rope = Physics.addDistanceConstraint(bob, anchor, 0.5, 4.0)

-- 断开关节。
Physics.removeConstraint(rope)
```

- `Physics.addFixedConstraint(bodyA, bodyB?)`
- `Physics.addPointConstraint(bodyA, bodyB?, point)`
- `Physics.addDistanceConstraint(bodyA, bodyB?, minDistance, maxDistance)`
- `Physics.addHingeConstraint(bodyA, bodyB?, point, axis, minAngleDegrees, maxAngleDegrees)`
- `Physics.addSliderConstraint(bodyA, bodyB?, point, axis, minDistance, maxDistance)`
- `Physics.addConeConstraint(bodyA, bodyB?, point, twistAxis, halfAngleDegrees)`
- `Physics.removeConstraint(id)` / `Physics.isConstraintValid(id)`
- `Physics.setConstraintMotor(id, enabled, targetVelocity, maxForce)` — 铰链电机为角速度
  （度/秒），滑动电机为线速度（米/秒）

若实体带有 `RigidBodyComponent`，物体会按需创建。当被引用的任一物体被销毁时，约束会自动
移除。布娃娃（ragdoll）由胶囊体刚体 + 锥形（摆动-扭转）约束构成。

## Navigation（Recast/Detour 导航）

从关卡几何烘焙导航网格（navmesh）并驱动代理沿路径移动。带 `MeshComponent` +
`TransformComponent` 的导入网格会被烘焙；内置图元会被跳过。

```lua
function OnCreate(self)
  Nav.bake()                      -- 从世界几何烘焙导航网格
  Nav.setDebugDrawEnabled(true)   -- 在编辑器中可视化导航网格与路径
end

function OnUpdate(self, dt)
  if Input.isMouseButtonPressed(MouseCode.Left) then
    Nav.setAgentDestination(self, Vec3(10, 0, 4)) -- 代理走过去，绕开障碍
  end
end
```

为实体添加 `NavAgentComponent`（`entity:addComponent(Component.NavAgent)`）使其成为代理；
若同时带有 `CharacterControllerComponent`，代理会通过角色控制器移动，否则直接移动其
transform。

- `Nav.bake()` — （重新）构建导航网格；成功返回 `true`
- `Nav.isBaked()`
- `Nav.findPath(start, end)` — 返回 `Vec3` 路点数组（不可达时为空）
- `Nav.nearestPoint(point)` — 导航网格上最近的点
- `Nav.setAgentDestination(entity, target)` / `Nav.stopAgent(entity)` / `Nav.agentHasPath(entity)`
- `Nav.setDebugDrawEnabled(enabled)` / `Nav.debugDrawEnabled()`

`NavAgent` 字段：`radius`、`height`、`speed`、`stoppingDistance`、`targetPosition`、
`hasTarget`、`moving`（只读）。

完整示例 —— 挂到一个实体上（最好带 `CharacterControllerComponent`）。它在启动时烘焙导航
网格、把自己变成代理、绘制导航网格，并在左键点击时走向目标：

```lua
function OnCreate(self)
  if not self:hasComponent(Component.NavAgent) then
    self:addComponent(Component.NavAgent)
  end
  if not Nav.bake() then
    print("navmesh bake failed (no imported geometry?)")
  end
  Nav.setDebugDrawEnabled(true) -- 在编辑器中显示导航网格与路径
end

function OnUpdate(self, dt)
  if Input.isMouseButtonPressed(MouseCode.Left) then
    Nav.setAgentDestination(self, Vec3(4.0, 0.0, 0.0))
  end
  if Input.isKeyPressed(KeyCode.Escape) then
    Nav.stopAgent(self)
  end
  if Input.isKeyPressed(KeyCode.P) then
    local path = Nav.findPath(self.transform.position, Vec3(4.0, 0.0, 0.0))
    print("path has " .. #path .. " waypoints")
  end
end
```

## Roll-A-Ball Example

这个示例使用 WASD 来推动一个动态小球，让游戏相机跟随它，并将附近的
拾取物实体标记为已收集。拾取物预期以 `Pickup` 前缀命名。

```lua
RollABall = {
  score = 0,
  won = false,
  moveForce = 45.0,
  pickupRadius = 1.25,
}

local function movementInput()
  local x = 0.0
  local z = 0.0

  if Input.isKeyHeld(KeyCode.A) then x = x - 1.0 end
  if Input.isKeyHeld(KeyCode.D) then x = x + 1.0 end
  if Input.isKeyHeld(KeyCode.W) then z = z - 1.0 end
  if Input.isKeyHeld(KeyCode.S) then z = z + 1.0 end

  return Vec3(x, 0.0, z)
end

function OnCreate(self)
  RollABall.score = 0
  RollABall.won = false

  local body = self:getComponent(Component.RigidBody)
  if body then
    body.mass = 1.0
    body.overrideMass = true
    body.friction = 0.25
    body.restitution = 1.0
    body.gravityFactor = 1.0
  end

  print("[RollABall] started")
end

function OnFixedUpdate(self, fixedDt)
  local body = self:getComponent(Component.RigidBody)
  if not body then
    return
  end

  local input = movementInput()
  body:addForce(Vec3(input.x * RollABall.moveForce, 0.0, input.z * RollABall.moveForce))
  body:activate()
end

function OnUpdate(self, dt)
  local playerPos = self.transform.position

  local camera = Camera.findPrimary()
  if camera.valid then
    camera.transform.position = Vec3(playerPos.x, playerPos.y + 6.0, playerPos.z + 7.0)
    camera.transform:lookAt(playerPos)
  end

  local pickups = Physics.overlapSphere(playerPos, RollABall.pickupRadius)
  local remaining = 0
  for _, pickup in ipairs(pickups) do
    if pickup.valid and pickup.active and pickup.name:sub(1, 6) == "Pickup" then
      pickup.active = false
      pickup.visible = false
      RollABall.score = RollABall.score + 1
      print("[RollABall] pickup " .. tostring(RollABall.score))
    end
  end

  for _, pickup in ipairs(World.findByNamePrefix("Pickup")) do
    if pickup.valid and pickup.active then
      remaining = remaining + 1
    end
  end

  if remaining == 0 and not RollABall.won then
    RollABall.won = true
    print("[Lua] you win")
  end
end
```

如果可视网格是一个导入的场景资源，请将玩法状态保留在父实体上，并将
导入的网格作为子实体放置在它下面：

- 父实体：`RigidBodyComponent`、`SphereShapeComponent`、`ScriptComponent`。
- 子实体：导入的可视场景/网格。

这能让物理、脚本和保存行为独立于外部资源场景根节点。

## Assets And Scenes

资源 API：

- `Asset.resolveUri(uri)`
- `Asset.loadText(uri)`
- `Asset.loadMesh(uri)`
- `Asset.loadTexture(uri)`
- `Asset.loadGaussianSplat(uri)`
- `Asset.memoryStats()`

`Asset.loadText` 返回一个带有 `ok`、`text` 和 `error` 的结果。网格、纹理
和高斯泼溅（Gaussian splat）的加载返回一个带有 `valid`、`ready`、`uuid`、
`state` 和 `gpuIndex` 的 `AssetHandle`。

场景 API：

- `Scene.load(uri)`
- `Scene.instantiate(uri)`
- `Scene.instantiateChild(uri, parent, clearWorld)`
- `Scene.saveWorld(uri)`
- `Scene.saveEntity(uri, root)`
- `Scene.dontDestroyOnLoad(entity)` —— 在下次场景加载 / `Scene.instantiate(..., clearWorld=true)`
  时保留 `entity`（及其子树）；它会被提升为根并存活下来
- `Scene.isPersistent(entity)`

## Save / Persistence（存档 / 持久化）

`Save` 是一个带类型的键/值存储，外加具名的、基于文件的存档槽（slot）——用于玩家进度、
设置、解锁等。KV 存储位于内存中，并且**在场景加载/重载之间保持存在**（它是引擎服务，而非
世界数据）；`Save.save(slot)` / `Save.load(slot)` 将其持久化到磁盘并从磁盘恢复（位于操作系统
的 app-data 目录，或 `VULTRA_SAVE_DIR`）。

```lua
-- 写入进度，然后持久化到 slot1。
Save.set("level", 3)
Save.set("playerName", "Ada")
Save.set("musicOn", true)
Save.save("slot1")

-- 之后（即使重载了场景）：恢复并读回。
if Save.load("slot1") then
  local level = Save.get("level", 1)       -- 3（缺失时返回默认值 1）
  local name  = Save.get("playerName", "?")
end
```

- `Save.set(key, value)` —— `value` 为 number、boolean 或 string
- `Save.get(key, default?)` —— 返回存储的值（带类型）或 `default` / `nil`
- `Save.has(key)`、`Save.remove(key)`、`Save.clear()`
- `Save.save(slot)` / `Save.load(slot)` -> boolean
- `Save.hasSlot(slot)`、`Save.deleteSlot(slot)`、`Save.listSlots()` -> 存档槽名数组

存档槽名会被规整为安全的文件名。对于复杂的表，请存储 JSON 字符串（在 Lua 中编码）。

## Script And Render Utilities

Script API：

- `Script.reloadAll()`
- `Script.reloadEntity(entity)`
- `Script.hasInstance(entity)`
- `Script.destroyInstance(entity)`
- `Script.setPlaybackState(playing, paused)`
- `Script.isPlaybackPlaying()`
- `Script.isPlaybackPaused()`
- `Script.runString(code)`

Time API：

- `Time.deltaTime()`
- `Time.fixedDeltaTime()`
- `Time.unscaledDeltaTime()`
- `Time.smoothedDeltaTime()`
- `Time.totalTime()`
- `Time.unscaledTotalTime()`
- `Time.averageFrameTime()`
- `Time.framesPerSecond()`
- `Time.timeScale()`
- `Time.fixedAlpha()`
- `Time.fixedStepsThisFrame()`
- `Time.frameIndex()`
- `Time.maxDeltaTime()`
- `Time.setTimeScale(scale)`
- `Time.setFixedDeltaTime(dt)`
- `Time.setMaxDeltaTime(dt)`
- `Time.setMaxFixedStepsPerFrame(maxSteps)`
- `Time.setDeltaSmoothingFactor(factor)`

Render API：

- `Render.resize(width, height)`
- `Render.setProfilerEnabled(enabled)`
- `Render.isProfilerEnabled()`
- `Render.profilerHistorySize()`
- `Render.captureFrame()`
- `Render.gaussianSplatSettings()`
- `Render.setGaussianSplatSettings(settings)`
- `Render.gaussianSplatFrameStats()`
- `RenderBackend.isXREnabled()`
- `RenderBackend.isXRMirrorEnabled()`
- `RenderBackend.isExitRequested()`

Upscaler API：

- `Upscaler.providers()`
- `Upscaler.active()`
- `Upscaler.setActive(name)`
- `Upscaler.setEnabled(enabled)`
- `Upscaler.setMode(mode)`，其中 mode 为 `off`、`quality`、`balanced`、`performance`、`ultra_performance` 或 `dlaa`
- `Upscaler.status()`

这些仅是高层控制。Lua 脚本无法访问原生纹理、命令缓冲区、Vulkan
句柄或提供方拥有的 SDK 对象。

## Audio

音频播放通过全局 `Audio` 表驱动。剪辑（clip）通过注册表 UUID 字符串或
`res://` URI 来寻址；向任何接受剪辑的调用传入同一个字符串。声音句柄
函数接受由 `play*` 调用返回的 `SoundId`。当没有可用的音频后端时，这些
调用是安全的空操作（`backendReady()` 报告 `false`）。

```lua
function OnCreate(self)
  Audio.preloadClip("res://audio/coin.wav")
end

function OnUpdate(self, dt)
  if Input.isKeyPressed(KeyCode.Space) then
    Audio.playOneShot("res://audio/coin.wav", 0.8) -- volume, optional pitch
  end
end
```

一次性音效（one-shot）与音乐：

- `Audio.preloadClip(clip)` -- 预热缓存；成功时返回 `true`
- `Audio.playOneShot(clip, volume?, pitch?)` -- 触发即忘的 2D 音效，返回一个 `SoundId`
- `Audio.playOneShotAt(clip, position, volume?, pitch?)` -- 在世界位置上的空间化一次性音效
- `Audio.playMusic(clip, options?)` -- 默认循环；`options` 是一个包含 `volume`、`pitch`、`loop`、`fadeInMs` 的表
- `Audio.stopMusic(fadeOutMs?)`

声音句柄控制（通过 `SoundId`）：

- `Audio.stopSound(id, fadeOutMs?)`
- `Audio.pauseSound(id)` / `Audio.resumeSound(id)`
- `Audio.setVolume(id, volume)` / `Audio.setPitch(id, pitch)` / `Audio.setLooping(id, loop)`
- `Audio.isPlaying(id)`

实体驱动的播放（使用实体的 `AudioSourceComponent`）：

- `Audio.play(entity, restart?)`
- `Audio.pause(entity)`
- `Audio.stop(entity)`

全局混音器（mixer）：

- `Audio.setMasterVolume(volume)` / `Audio.masterVolume()`
- `Audio.backendReady()`

组件可通过 `entity:getComponent(Component.AudioSource)` 和
`entity:getComponent(Component.AudioListener)` 访问：

```lua
local source = self:getComponent(Component.AudioSource)
if source then
  source.clip = "res://audio/engine.wav"
  source.volume = 0.5
  source.loop = true
  source.spatial = true
  Audio.play(self)
end
```

`AudioSource` 属性：`valid`、`clip`、`volume`、`pitch`、`loop`、
`playOnStart`、`playing`、`spatial`、`minDistance`、`maxDistance`、`rolloff`。
`AudioListener` 属性：`valid`、`primary`。

## Animation

动画播放器（Animator）的播放可以通过 Animator 组件引用
（`entity:getComponent(Component.Animator)`）或全局 `Animation` 表来控制。

```lua
local actor = World.findByName("Actor")
if actor.valid then
  local animator = actor:getComponent(Component.Animator)
  if animator then
    animator:play(true)
    animator.speed = 1.0
    animator.loop = true

    local state = animator:state()
    print("animation time " .. tostring(state.time))
  end
end
```

Animator 组件属性：

- `animator.valid`
- `animator.skeleton`
- `animator.animation`
- `animator.playing`
- `animator.loop`
- `animator.speed`
- `animator.time`

Animator 方法：

- `animator:play(restart?)`
- `animator:pause()`
- `animator:stop()`
- `animator:setNormalizedTime(normalizedTime)`
- `animator:state()`

全局动画 API：

- `Animation.play(entity, restart?)`
- `Animation.pause(entity)`
- `Animation.stop(entity)`
- `Animation.setAnimation(entity, animationUuid, restart?)`
- `Animation.setTime(entity, seconds)`
- `Animation.setNormalizedTime(entity, normalizedTime)`
- `Animation.setSpeed(entity, speed)`
- `Animation.setLoop(entity, loop)`
- `Animation.state(entity)`
- `Animation.jointCount(skeletonUuid)`
- `Animation.duration(animationUuid)`

动画控制器参数（用于具有控制器数据的实体）：

- `Animation.setFloat(entity, name, value)` / `Animation.getFloat(entity, name)`
- `Animation.setBool(entity, name, value)` / `Animation.getBool(entity, name)`
- `Animation.setTrigger(entity, name)`
- `Animation.currentState(entity)` -- 返回 `{valid, currentState, nextState,
  transitioning, transitionProgress, normalizedTime}`

`Animation.state` 返回一个 `AnimatorPlaybackState`：

- `valid`
- `playing`
- `loop`
- `speed`
- `time`
- `duration`
- `normalizedTime`
- `skeleton`
- `animation`

### 关键帧事件（Keyframe events）

动画状态机的状态可以携带**事件**——位于片段某个归一化时间点的具名标记。当播放越过某个
事件时，动画系统会调用实体脚本的 `OnAnimationEvent(self, name)`（见 **动画事件回调**）。
在 `.vanimgraph.json` 中按状态编写：

```json
{
  "states": [
    {
      "name": "Walk",
      "animation": "<clip-uuid>",
      "loop": true,
      "events": [
        { "name": "footstepLeft",  "normalizedTime": 0.25 },
        { "name": "footstepRight", "normalizedTime": 0.75 }
      ],
      "transitions": []
    }
  ],
  "version": 1
}
```

`normalizedTime` 取值 `[0,1]`（片段内）。事件在激活状态上触发、每次循环重复，并按状态隔离。
（单片段动画——`mode 0`——没有状态，因此事件需要状态机模式。）

### 混合树（Blend trees）

状态可以是 **1D 混合树**，而非单个片段：它按一个浮点参数（例如用 `speed` 在 idle/walk/run
之间）混合多个片段。在 `.vanimgraph.json` 的状态上编写；各片段以共享、相位对齐的 ratio
采样，并由参数值两侧相邻的两个 entry 进行混合。用 `Animation.setFloat(entity, "speed", v)`
驱动参数。

```json
{
  "states": [
    {
      "name": "Locomotion",
      "loop": true,
      "blendTree": {
        "parameter": "speed",
        "entries": [
          { "animation": "<idle-uuid>", "threshold": 0.0 },
          { "animation": "<walk-uuid>", "threshold": 1.0 },
          { "animation": "<run-uuid>",  "threshold": 4.0 }
        ]
      }
    }
  ],
  "version": 1
}
```

当存在 `blendTree.entries` 时，它会覆盖该状态的单个 `animation`。混合树状态上事件仍会触发。

### 根运动（Root motion）

设置 `AnimatorComponent.applyRootMotion = true`（状态机模式），让当前播放片段的根关节驱动
**实体 transform**（水平移动），而不是让网格在原地滑动。显示的骨架会保持在实体中心，由
transform 吸收每帧的根位移（按实体朝向旋转）。在交叉淡入淡出期间以及循环回绕帧会被跳过，
并假定关节 0 为骨架根。

## Common Pitfalls

- 对绑定方法使用 `:`，例如 `entity:destroy()` 和
  `entity:getComponent(Component.RigidBody):addForce(force)`。
- 在使用通过名称找到的句柄之前，检查 `entity.valid` 和特定组件的
  `valid` 字段。
- 通过通用的 `entity:getComponent(Component.X)`（不存在时返回 `nil`）、
  `entity:addComponent(Component.X)`、`entity:removeComponent(Component.X)`
  以及 `entity:hasComponent(Component.X)` 来访问 `transform` 之外的每一个
  组件。旧的逐组件属性（`entity.rigidBody`、`entity.camera`、……）、
  `entity:hasRigidBody()` 风格的方法，以及 `World.add*/remove*` 家族都已
  移除；将新值绑定到一个局部变量并在使用前进行 nil 检查。
- 使用 `OnFixedUpdate` 来施加物理力。使用 `OnUpdate` 来进行输入采样、
  相机跟随、得分检查以及非物理动画。
- 对于接触驱动的逻辑，优先使用 `OnCollisionEnter/Stay/Exit` 和
  `OnTriggerEnter/Stay/Exit` 回调（参见 "Collision And Trigger Callbacks"）。
  对于轮询式的查询，也可以使用 `Physics.overlapSphere`、`Physics.overlapBox`、
  `Physics.contactPairs` 和 `Physics.contactEvents`。
- 除了单剪辑播放之外，`Animation` 表还通过参数驱动动画控制器的状态机：
  `Animation.setFloat`、`Animation.setBool`、`Animation.setTrigger`、
  `Animation.getFloat`、`Animation.getBool` 以及
  `Animation.currentState(entity)`（返回当前控制器状态、过渡进度和归一化
  时间）。这些要求实体具有已创作的控制器数据；遮罩（mask）和重定向
  （retargeting）尚未暴露。
- 导入的可视资源可能具有非常小或非常大的源单位。在实例化后请在 Game
  View 中验证缩放。
- GLB 导入中嵌入的纹理必须通过渲染出的材质输出进行验证，而不仅仅依靠
  注册表/导入成功。
