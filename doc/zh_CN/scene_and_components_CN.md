# 场景与组件

[English](../scene_and_components.md) | **简体中文**

Vultra 将关卡中的一切组织为一个**场景**：一棵由**实体**组成的树，每个实体
携带若干**组件**。本文档涵盖 ECS、`.vscn` 文件格式、组件如何通过反射注册，
以及完整的组件目录。

另见：[Lua 脚本](lua_scripting_CN.md)，用于在运行时驱动实体/组件；以及
[粒子系统](particle_system_CN.md)，用于了解粒子后端。

## ECS 概览

世界构建于 [EnTT](https://github.com/skypjack/entt) 之上。运行时类型为
`vultra::World`（`source/vultra/include/vultra/function/world/world.hpp`），
它封装了单个 `entt::registry`。**实体**是一个轻量句柄
（`entt::entity`）；其数据存放于附加到它的**组件**中。

`World` 持有层级不变量。父/子链接存储在一个侵入式的
`HierarchyComponent`（`parent`、`firstChild`、`nextSibling`、
`prevSibling`、`childCount`）中，结构性编辑都经过 `World`：

- `createEntity()` / `destroyEntity(e)`
- `createChild(parent)` / `destroyRecursive(root)`
- `setParent`、`removeParent`、`insertBefore`、`insertAfter`
- 迭代：`firstChild(e)`、`nextSibling(e)`、`parent(e)`

`WorldSystem`（一个 `EngineSubsystem`，`source/vultra/.../world/world_system.cpp`）
在初始化时创建主世界，将其作为 `IWorldService` 提供，并通过 `onPreRender()`
每帧**触发**一次。它在那里的职责是 `updateWorldTransforms()`：
它从根开始深度优先遍历层级，对于任何 `TransformComponent` 为 `dirty`
（或其父级发生变化）的实体，重新计算
`worldMatrix = parentWorld * T * R * S`。局部矩阵由
`position`、`rotation`（四元数）和 `scale` 构建。`worldMatrix`/`dirty` 是
运行时缓存，不被序列化。

游戏/渲染行为存在于其他遍历注册表视图的子系统中
（物理、音频、动画、渲染、脚本）。组件是纯数据；
系统提供逻辑。

## `.vscn` 场景格式

场景序列化为 `.vscn`，一种人类可读的类 INI/TOML 文本文件。它被
有意设计为易于 diff 且对版本控制友好：稳定的排序、每行一个字段、
无二进制块。文件由 `VscnWriter` 生成，由
`VscnReader` 解析（`source/vultra/src/function/scene/`）。

```ini
[vscn]
version = 1
root    = 0

[node id=1 name="Camera" parent=0 uuid="0a1b2c3d-...."]
NameComponent/name = "Camera"
TransformComponent/position = (0, 1.5, 6)
TransformComponent/rotation = (0, 0, 0, 1)
CameraComponent/primary = true
CameraComponent/fovY = 60

[node id=2 name="Sun" parent=0 uuid="9f8e7d6c-...."]
NameComponent/name = "Sun"
TransformComponent/rotation = (-0.27, 0.27, 0.07, 0.92)
LightComponent/kind = 0
LightComponent/intensity = 8
```

### 结构

- **`[vscn]` 头部** — `version`（当前为 `1`）和 `root`。`root = 0` 表示
  该文件使用一个合成根：每个顶层 `[node]` 都是一个真正的场景根，
  其 `parent` 为 `0`（虚拟根）。`root = 1` 表示节点 id `1` 即文档根本身。
- **`[node ...]` 行** — 每个实体一行，带有属性：
  - `id` — 一个文件本地整数，仅用于在本文件内表达父子关系。
  - `name` — 显示名称（可选）。
  - `parent` — 父节点的 `id`，根节点为 `0`。
  - `uuid` — 实体稳定的 `IDComponent` UUID（跨保存持久的标识，
    被预制体/链接引用）。
  - `prefab` — 预制体实例的可选预制体源 URI。
- **`Component/field = value` 行** — 上方节点的组件数据。
  键为 `<ComponentType>/<fieldName>`；值由字段类型决定：
  - 字符串：带引号，例如 `"Camera"`
  - 布尔值：`true` / `false`（也接受 `1` / `0`）
  - 数字：`60`、`0.1`
  - 向量/四元数：带括号的元组，`(x, y, z)` / `(x, y, z, w)`
  - UUID：带引号的 UUID 字符串

值通过反射按名称映射回组件字段，因此只要注册了旧别名，
读取器就能容忍字段重命名（例如
`CameraComponent/fovYDegrees` 仍会加载到 `fovY` 上）。

## 组件反射

反射是序列化、检查器和添加组件菜单背后的唯一机制。`registerSceneMeta()`
（`source/vultra/src/function/scene/scene_reflection.cpp`）将每个
可序列化组件和字段注册到 EnTT 的元系统中：

```cpp
entt::meta_factory<TransformComponent>()
    .type("TransformComponent"_hs)
    .data<&TransformComponent::position>("position"_hs)
    .data<&TransformComponent::rotation>("rotation"_hs)
    .data<&TransformComponent::scale>("scale"_hs);
```

由于 `.vscn` 键正好是 `TypeName/fieldName`，同一张注册
表驱动写入（枚举字段）、读取（将键解析为字段并
解析其值），以及编辑器的检查器/添加组件 UI。因此添加一个新的
可序列化字段只需一次编辑：在组件
结构体上声明它，并在此处添加一行 `.data<...>(...)`。辅助元类型（`glm::vec3`、
`glm::quat`、`CoreUUID`、材质属性/覆盖结构体）也被注册，
以便嵌套值能够往返。

## 组件目录

下面的每个组件都是位于
`source/vultra/include/vultra/function/world/components/` 下的纯结构体。标记为
`VBIND_USERTYPE` 的组件也暴露给 Lua（见 [Lua 脚本](lua_scripting_CN.md)）。

### 核心

- **NameComponent** — 显示名称。字段：`name`。
- **IDComponent** — 稳定的每实体 UUID。字段：`uuid`。
- **EntityStatusComponent** — 编辑器/运行时标志：`active`、`visible`、
  `locked`、`selectable`。
- **LayerComponent** — 渲染/剔除层位掩码。字段：`mask`。
- **TransformComponent** — 局部 `position`、`rotation`（四元数）、`scale`；外加一个
  非序列化的 `worldMatrix`/`dirty` 缓存。
- **HierarchyComponent** — 侵入式父/子树链接；由 `World` 管理，
  不直接编辑。

### 渲染

- **CameraComponent** — `primary`、`projection`（0 透视 / 1 正交）、
  `fovY`、`orthographicHeight`、`zNear`、`zFar`、`clearMode`、`clearColor`、
  `priority`、`cullingMask`、`rendererKey`。
- **LightComponent** — `kind`（0 平行光，1 点光，2 聚光，3 区域光）、`color`、
  `intensity`、`range`、`radius`、`width`/`height`（区域光）、`innerConeDegrees`/
  `outerConeDegrees`（聚光）、`castsShadow`、`twoSided`。
- **MeshComponent** — `mesh`（UUID）、`builtinGeometry`（UINT32_MAX = 导入的
  网格，否则 0 四边形 / 1 立方体 / 2 球体 / 3 胶囊体），以及 `materialOverrides`
  （每槽位材质/图 + 属性块）。
- **EnvironmentComponent** — 场景天空/环境光/IBL：`active`、`skybox`、
  `ambientColor`、`ambientIntensity`、`enableIBL`、`iblColor`、`iblIntensity`。
- **ReflectionProbeComponent** — `active`、`enableIBL`、`environmentMap`、
  `shape`（0 盒体 / 1 球体）、`boxSize`、`radius`、`blendDistance`、`intensity`、
  `priority`、`parallaxCorrection`。
- **ParticleEmitterComponent** — GPU 或 CPU 公告板粒子：`playing`、
  `worldSpace`、`gpu`、`maxParticles`、`emissionRate`、生命周期/生成/速度/
  重力，以及起始/结束的尺寸和颜色。见 [粒子系统](particle_system_CN.md)。
- **GaussianSplatComponent** — 引用一个 3D 高斯泼溅资产。字段：
  `gaussianSplat`。

### 物理

- **RigidBodyComponent** — Jolt 支持的刚体：`motionType`（0 静态 / 1
  运动学 / 2 动态）、`objectLayer`、`isSensor`、`motionQuality`、
  `allowSleeping`、`friction`、`restitution`、`linearDamping`、`angularDamping`、
  `gravityFactor`、`linearVelocity`、`angularVelocity`、`mass`/`overrideMass`、
  `maxLinearVelocity`、`maxAngularVelocity`。（通过 `linearVelocity` 移动刚体。）
- **BoxShapeComponent** — `halfExtents`。
- **SphereShapeComponent** — `radius`。
- **CapsuleShapeComponent** — `halfHeightOfCylinder`、`radius`。
- **CylinderShapeComponent** — `halfHeight`、`radius`。
- **MeshShapeComponent** — 由实体网格生成的碰撞体；`convex`（false =
  静态三角形网格，true = 可用于动态刚体的凸包）。
- **CharacterControllerComponent** — 运动学角色：`radius`、`height`、
  `maxSlopeAngleDegrees`、`stepHeight`、`gravityFactor`、`mass`、`jumpSpeed`、
  `objectLayer`，外加运行时 `inputMove`、`jumpRequested`、`velocity`、
  `grounded`。

### 音频

- **AudioSourceComponent** — 播放已烘焙的片段：`clip`、`volume`、`pitch`、
  `loop`、`playOnStart`、`playing`、`spatial`、`minDistance`、`maxDistance`、
  `rolloff`。
- **AudioListenerComponent** — 标记监听者位姿。字段：`primary`。

### 动画

- **AnimatorComponent** — 骨骼动画：`mode`（0 单一片段 / 1 动画器
  图）、`skeleton`、`animation`、`playOnStart`、`playing`、`loop`、`speed`、
  `time`、`graph`（图模式的 `.vanimgraph.json` URI）。

### UI

UI 实体存在于一个画布之下，并使用 `RectTransformComponent` 进行布局。

- **CanvasComponent** — `enabled`、`sortOrder`、`referenceResolutionPx`、
  `scaleMode`、`renderMode`（0 屏幕叠加 / 1 世界空间）、`pixelsPerUnit`。
- **RectTransformComponent** — `anchorMin`/`anchorMax`、`pivot`、
  `anchoredPositionPx`、`sizeDeltaPx`、`rotation`、`scale`。
- **UiPanelComponent** — `enabled`、`color`、`borderRadiusPx`。
- **UiImageComponent** — `enabled`、`texture`、`tint`、`fitMode`。
- **UiTextComponent** — `enabled`、`text`、`color`、`fontSizePx`、
  `horizontalAlign`、`verticalAlign`、`font`。
- **UiButtonComponent** — `enabled`、`interactable`、`targetGraphic`，以及
  常态/悬停/按下颜色（外加运行时 hovered/pressed/clicked 标志）。
- **UiToggleComponent** — `enabled`、`interactable`、`checked`，关闭/开启/勾选
  颜色。
- **UiSliderComponent** — `enabled`、`interactable`、`value`、`minValue`、
  `maxValue`，轨道/填充/手柄颜色。
- **UiProgressBarComponent** — `enabled`、`value`、`minValue`、`maxValue`、
  轨道/填充颜色。
- **UiLayoutComponent** — 自动布局：`enabled`、`kind`（0 无 / 1 水平 /
  2 垂直 / 3 网格）、`paddingPx`、`marginPx`、`spacingPx`、`cellSizePx`。

### 脚本与杂项

- **ScriptComponent** — 附加一个 Lua 脚本：`scriptUri`（引擎 URI）、
  `enabled`。见 [Lua 脚本](lua_scripting_CN.md)。
- **XRViewComponent** — XR/立体视图配置：`enabled`、`trackingOrigin`、
  `stereoGraphMode`、`fallbackMono`。

> 以下为运行时/内部组件，不属于上面的反射目录：
> `PrefabInstanceComponent`（预制体链接）和 `SkinPaletteComponent`（计算得到的
> 蒙皮矩阵）。

## 编写

### 在编辑器中

从场景层级面板创建实体（右键 -> 添加实体 / 子项），
然后通过检查器的 **Add Component** 菜单附加组件。该菜单和
检查器都由反射表生成，因此任何在 `registerSceneMeta()` 中
注册的组件都会自动出现。在检查器中编辑字段会
直接写到组件上；保存场景时会通过同一套反射序列化
到 `.vscn`。

### 从 Lua

在运行时，使用 `World` API 生成和销毁实体：

```lua
local e = World.create("Runtime Entity")
-- attach/configure components via their accessors, then:
World.destroy(e)
```

以 `VBIND_USERTYPE` 暴露的组件（相机、光照、环境、音频、
粒子发射器、反射探针、物理形状等）可通过
它们的脚本访问器访问。见 [Lua 脚本](lua_scripting_CN.md) 以及规范性的
[Lua API 设计](lua_api_design_CN.md)，了解命名、单位和约定。
