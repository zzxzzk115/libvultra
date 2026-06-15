# Vultra Gameplay Systems

[English](../gameplay_systems.md) | **简体中文**

本指南整合了四个运行时玩法子系统：**物理**、**音频**、**动画**和**游戏内 UI**。
每个子系统都作为引擎子系统运行，并由实体组件驱动。

- 脚本生命周期、回调以及完整的 Lua 接口位于
  [Lua scripting](lua_scripting_CN.md)。
- 组件字段、序列化以及编辑器检查器位于
  [Scene & components](scene_and_components_CN.md)。

这四个系统都遵循播放状态（`setPlaybackState`），因此它们仅在编辑器/运行时播放处于激活
状态时才进行模拟。

---

## Physics

**基于：** [Jolt Physics](https://github.com/jrouwe/JoltPhysics)，由
`PhysicsSystem`（`source/vultra/include/vultra/function/physics/physics_system.hpp`）封装。
该系统管理 Jolt 全局对象，为每个符合条件的实体构建一个 Jolt 刚体，并以**固定时间步长**
（默认 `1/60 s`）推进模拟。它使用计时服务的固定步数进行子步进以追赶已流逝的时间，并带有
一个上限为 `m_FallbackMaxSubSteps`（8）的回退累加器。暂停时支持单步执行
（`requestSingleStep`）。

### Components

一个物理刚体需要一个 `RigidBodyComponent` 加上同一实体上的一个碰撞器形状：

- **`RigidBodyComponent`** — `motionType`（0 静态 / 1 运动学 / 2 动态）、
  `objectLayer`、`isSensor`、`motionQuality`（0 离散 / 1 线性扫掠）、
  `allowSleeping`、`friction`、`restitution`、`linearDamping`、`angularDamping`、
  `gravityFactor`、`linearVelocity`、`angularVelocity`、`mass` /
  `overrideMass`、`maxLinearVelocity`、`maxAngularVelocity`。
- 碰撞器形状（每个刚体一个）：
  - **`BoxShapeComponent`** — `halfExtents`（vec3）。
  - **`SphereShapeComponent`** — `radius`。
  - **`CapsuleShapeComponent`** — `halfHeightOfCylinder`、`radius`。
  - **`CylinderShapeComponent`** — `halfHeight`、`radius`。
  - **`MeshShapeComponent`** — 从实体的 `MeshComponent` 烘焙出的碰撞器；
    `convex == false` 是静态三角网格（关卡几何体，不适用于动态刚体），
    `convex == true` 是可用于动态刚体的凸包。

### Character controller

`CharacterControllerComponent` 是一个由 Jolt 的 `CharacterVirtual` 支撑的运动学控制器
（碰撞滑动、坡度限制、台阶攀爬、地面检测）——与刚体不同。控制器位置是胶囊体的脚部。
玩法通过 `inputMove`（期望的水平速度）和 `jumpRequested` / `jumpSpeed` 来驱动它；
系统每步写回 `velocity` 和 `grounded`。调参字段：`radius`、`height`、
`maxSlopeAngleDegrees`、`stepHeight`、`gravityFactor`、`mass`、`objectLayer`。

### Queries

`PhysicsSystem` 暴露窄相位查询（真实的 Jolt 几何体，而非 AABB）：
`raycast` / `raycastAll`、`sphereCast` 以及 `overlapSphere` / `overlapBox` /
`overlapCapsule`。它们都接受一个 `PhysicsQueryFilter`（仅激活的对象 + 一个针对
`objectLayer` 索引的 32 位层掩码）。

### Collision and trigger callbacks

每次物理更新后，系统会对接触对进行差异比较，并向任一实体上的脚本派发
**真实的 Lua 回调**：`OnCollisionEnter/Stay/Exit` 和 `OnTriggerEnter/Stay/Exit`。
当任一刚体具有 `isSensor = true` 时会触发触发器回调。在退出事件中，`other` 实体
可能已经失效——请使用 `other.valid` 进行保护。仍然可以通过
`Physics.contactEvents()` / `consumeContactEvents()` 进行轮询。

### Important: drive movement with velocity, not force

对于玩法中的角色/物体移动，**请设置 `rigidBody.linearVelocity`**，
而不是调用 `addForce` / `addImpulse`。这一点在
`syncDynamicBodiesToWorld`（`source/vultra/src/function/physics/physics_system.cpp`）
中得到验证：每一步，对于动态刚体，系统都会**将 Jolt 刚体的速度读回组件**
（`rb->linearVelocity = GetLinearVelocity(...)`）。组件值仅在创建时以及对于运动学刚体
才会被推送到刚体上，因此你每帧写入的速度才是 Jolt 模拟所使用的权威值，而前几帧累积的
力/冲量实际上会被这次读回所覆盖。`addForce` / `addImpulse` 仍然存在，用于瞬时冲量，
但它们不是表达持续移动的可靠方式。

> Lua API：参见 [Lua scripting](lua_scripting_CN.md) 的 **Physics** 部分 ——
> `entity.rigidBody`（`linearVelocity`、`addForce`、`addImpulse`、...）以及
> 全局 `Physics` 表（`raycast`、`overlapSphere`、层碰撞、...）。

---

## Audio

**基于：** [miniaudio](https://miniaud.io/)，由 `AudioSystem`
（`source/vultra/include/vultra/function/audio/audio_system.hpp`）封装。混音使用
`ma_engine`（WASAPI/CoreAudio/ALSA/AAudio；在 wasm 上使用 Web Audio）。音频片段是烘焙后的
`vaudio` 资产，通过资产服务加载并注册到 miniaudio 的资源管理器。如果没有可用的音频设备
（无头/CI），系统将以无音频模式运行，此时每次调用都是安全的空操作，且 `backendReady()`
返回 `false`。

### Spatialization

`AudioSource` / `AudioListener` 的姿态在 `onPreRender` 中同步，即在世界系统刷新
`TransformComponent::worldMatrix` 之后，从而提供**带距离衰减的 3D 空间音频**。激活的
监听器是任何具有 `primary = true` 的 `AudioListenerComponent` 的实体（第一个生效）；
如果没有监听器，引擎会在原点保持一个默认姿态。实际上监听器通常跟随激活的相机。

### Components

- **`AudioSourceComponent`** — `clip`（一个 `vaudio` 资产的 UUID）、`volume`、
  `pitch`、`loop`、`playOnStart`、`playing`、`spatial`（当为 `false` 时，片段作为
  普通的 2D/UI 声音播放），以及 3D 衰减参数 `minDistance`、`maxDistance`、`rolloff`。
  组件存储期望的状态；运行时声音实例存在于 `AudioSystem` 中，它每帧对其进行协调
  （包括停止已销毁实体的声音）。
- **`AudioListenerComponent`** — `primary`（bool）。

### Behaviors

- **一次性播放：** `playOneShot`（2D）和 `playOneShotAt`（在世界位置进行空间化），
  带可选的 volume/pitch；返回一个 `SoundId`。
- **音乐：** `playMusic`（默认循环，带可选的淡入）/ `stopMusic(fadeOutMs)`。
- **按 `SoundId` 控制实例：** `stop`（带淡出）、`pause`、`resume`、
  `setVolume`、`setPitch`、`setLooping`、`isPlaying`。
- **实体驱动的播放：** 使用实体的 `AudioSourceComponent`，按 `entt::entity` 进行
  `play` / `pause` / `stop`。
- **全局：** `setMasterVolume` / `masterVolume`。

> Lua API：参见 [Lua scripting](lua_scripting_CN.md) 的 **Audio** 部分 —— 全局
> `Audio` 表（`playOneShot`、`playOneShotAt`、`playMusic`、实例控制、主音量）
> 以及 `entity.audioSource` / `entity.audioListener`。

---

## Animation

**基于：** [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) 运行时
（`ozz::animation::Skeleton` / `Animation`），由 `AnimationSystem`
（`source/vultra/include/vultra/function/animation/animation_system.hpp`）封装。
骨骼和动画是烘焙后的 `vasset` 资产，通过资产服务解析并按 UUID 缓存。蒙皮调色板供给
蒙皮网格渲染。

### Component

**`AnimatorComponent`** 以两种模式之一驱动骨骼动画：

- **`mode 0`（单一片段）：** 直接播放 `animation` 片段。字段：
  `playOnStart`、`playing`、`loop`、`speed`、`time`。
- **`mode 1`（图）：** 运行位于 `graph`（一个 `res://...vanimgraph.json`）的动画器图
  —— 一个状态机。

`skeleton` 是可选的；若未设置，则默认为实体（或某个后代）的蒙皮网格自带的骨骼。

### Animator state machine

图格式位于
`source/vultra/include/vultra/function/animation/animator_graph.hpp`，并且有一个
基于节点的动画器图编辑器。一个图包含：

- **Parameters** —— 类型为 `eFloat`、`eBool` 或 `eTrigger`（一次性，在某个转换消耗它
  之后自动重置）的参数，每个都有一个默认值。
- **States** —— 每个状态命名一个片段 `animation`，带有 `speed`、`loop` 以及外出的
  `transitions`。
- **Transitions** —— 一个目标状态、一组 `conditions`（以 AND 连接；通过
  greater/less/equal/notEqual/true/false/trigger 比较一个参数）、一个交叉淡入
  `duration`，以及可选的 `hasExitTime` / `exitTime`（归一化的源片段位置）。一个图还有
  从任何状态求值的 `anyTransitions`（"Any State"）。

每个实体的控制器运行时跟踪当前/目标状态、混合时间以及实时参数值；`controllerState` /
`playbackState` 暴露它们。系统提供 `setFloat` / `setBool` / `setTrigger` / `getFloat` /
`getBool` 在运行时驱动参数。

### Not exposed yet

该运行时是单骨骼片段播放，带状态机交叉淡入混合。**Avatar 蒙版（逐骨骼混合蒙版）和跨骨骼
重定向尚未实现** —— 图混合是同一骨骼上两个片段之间的全局交叉淡入。

> Lua API：参见 [Lua scripting](lua_scripting_CN.md) 的 **Animation** 部分 ——
> `entity.animator` 以及全局 `Animation` 表（播放控制加上控制器参数的设置器/获取器
> 以及 `Animation.currentState`）。

---

## In-game UI

**基于：** `UiSystem`
（`source/vultra/include/vultra/function/ui/ui_system.hpp`），其组件位于
`source/vultra/include/vultra/function/world/components/ui_components.hpp`。这是
**运行时的、世界内的游戏 UI** —— 与 Dear ImGui 不同，Vultra 仅将 ImGui 用于编辑器和
调试叠加层。系统每帧从画布/RectTransform 层级重建已解析的矩形并运行指针输入。

### Canvas and layout

- **`CanvasComponent`** — `enabled`、`sortOrder`、`referenceResolutionPx`、
  `scaleMode`（0 恒定像素大小，1 随屏幕缩放）、
  `renderMode`（0 屏幕叠加，1 世界空间 —— 画布在 3D 中跟随实体变换）以及
  `pixelsPerUnit`（仅世界空间）。所有子 UI 的尺寸都是相对于参考分辨率的像素，
  按 `scaleMode` 进行缩放。
- **`RectTransformComponent`** — `anchorMin` / `anchorMax`（锚点矩形）、
  `pivot`、`anchoredPositionPx`、`sizeDeltaPx`、`rotation`、`scale`。

### Widgets

- **`UiPanelComponent`** —— 实心圆角面板：`color`、`borderRadiusPx`。
- **`UiImageComponent`** — `texture`（UUID）、`tint`、`fitMode`
  （0 拉伸 / 1 contain / 2 cover）。
- **`UiTextComponent`** — `text`、`color`、`fontSizePx`、`horizontalAlign`
  （左/中/右）、`verticalAlign`（上/中/下），以及一个可选的
  `font`（项目字体 UUID 或一个内置字体；为空 = 默认内置）。
- **`UiButtonComponent`** — `interactable`、`targetGraphic`、
  `normalColor` / `hoveredColor` / `pressedColor`，加上由系统更新的
  `hovered` / `pressed` / `clicked` 状态。
- **`UiToggleComponent`** — `interactable`、`checked`、`offColor` / `onColor` /
  `checkColor`。
- **`UiSliderComponent`** — `interactable`、`value`、`minValue`、`maxValue`、
  `trackColor` / `fillColor` / `handleColor`。
- **`UiProgressBarComponent`** — `value`、`minValue`、`maxValue`、
  `trackColor` / `fillColor`（仅显示，不可交互）。
- **`UiLayoutComponent`** —— 子元素的自动布局：`kind`
  （0 无 / 1 水平 / 2 垂直 / 3 网格）、`paddingPx`、`marginPx`、
  `spacingPx`、`cellSizePx`。

### Pointer input

`UiSystem` 每帧将指针对已解析的矩形进行射线检测，并跟踪悬停和按下的实体。
`pointerOverUi()`、`hoveredEntity()`、`pressedEntity()`、`buttonClicked()`、
`raycast(screenPx)` 和 `raycastCanvas(canvas, canvasPx)` 暴露这些结果。它发出
脚本可以订阅的悬停/按下/点击指针事件（`eventsThisFrame`）。对于嵌入式视口
（编辑器的 Game View），`setInputViewport` 会覆盖鼠标来源。

> Lua API：参见 [Lua scripting](lua_scripting_CN.md) 的 **UI** 部分 —— 全局
> `UI` 表（`isPointerOverUI`、`hoveredEntity`、`raycast`、`events`）、
> `self.rectTransform`，以及每个控件的引用（`uiButton`、`uiToggle`、
> `uiSlider`、`uiProgressBar`），带有基于信号的事件，例如
> `onClick:connect(...)`。
