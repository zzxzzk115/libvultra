# Vultra Lua API 设计规范

[English](../lua_api_design.md) | **简体中文**

本文档具有**规范性（normative）**。每一个并入引擎的面向 Lua 的绑定都
必须遵循这些规则。一致性由 `tests/lua_api_conformance` 机械化强制执行
（参见[强制执行](#enforcement)）；既有的违例记录在其 `exceptions.lua`
燃尽（burn-down）清单中，且该清单只能缩减、不能增长。

面向用户的脚本编写指南，参见 [lua_scripting.md](lua_scripting_CN.md)。
催生本规范的基础路线图，参见
`ai/workspace/foundation-roadmap.md`。

## 1. 命名

| 表面（Surface） | 约定 | 示例 |
|---|---|---|
| 全局命名空间表 | `PascalCase` | `Input`, `Physics`, `World`, `Time` |
| 类型（usertype） | `PascalCase` | `Vec3`, `Entity`, `RigidBody` |
| 枚举表及其成员 | `PascalCase` / `PascalCase` | `KeyCode.Space`, `ForceMode.Impulse` |
| 方法、自由函数 | `camelCase`，动词在前 | `setParent`, `playOneShot`, `raycast` |
| 属性 | `camelCase`，名词 | `position`, `linearVelocity`, `deltaTime` |
| 事件/信号属性 | `onPascalCaseEvent` | `onClick`, `onPointerEnter` |

附加规则：

- **构造函数**即可调用的类型表：`Vec3(1, 2, 3)`。小写的自由
  构造函数（`vec3(...)`）是已弃用的别名（参见
  [弃用](#7-deprecation-policy)）。
- **枚举绝不通过签名泄露原始整数**。函数接受/返回
  一个枚举表成员、一个字符串标记（有文档记录的集合），或一个有类型的值——
  绝不"传 2 表示 impulse"。
- **布尔查询**以 `isX` / `hasX` / `canX` 为前缀：`isKeyHeld`、
  `hasComponent`、`canJump`。当裸名词布尔*属性*读起来自然地表示状态时是
  允许的：`entity.valid`、`animator.playing`。
- 属性上不使用 `get` / `set` 前缀（参见第 2 节）。`setX(...)` 方法
  形式仅保留给那些除值之外还接受参数、或具有值得标记的
  副作用的操作（`setLayerCollision(a, b, enabled)`）。
- **对称对例外**：当存在带相同参数的对应 `setX` 时，*带参查找*
  保留 `get` 前缀——
  `Animation.getFloat(entity, name)` / `Animation.setFloat(entity, name, v)`
  （Unity 风格）。一致性测试只标记没有 `setX`
  同胞的 `getX`；不要把此例外用于无参访问器。
- **C++ 与 Lua 名称匹配。** Lua 表面镜像 C++ 标识符——
  当某个名称需要为脚本人体工学而修改时，C++ 侧也要一并重命名
  （服务方法、组件字段），保持全引擎统一的词汇表。
  生成器的 `name=` 覆盖是无法调和的情形下的最后手段。
  组件字段重命名会在 `scene_reflection.cpp` 中保留一个旧版 `.vscn` 读取别名
  （双重 `.data<>()` 注册），以便既有场景
  能加载；写入器只输出新键。

## 2. 属性还是方法

- 无参、廉价、无副作用的读取（及其配对的写入）是
  **属性**：`Time.deltaTime`、`transform.position`、`Physics.enabled`、
  `rigidBody.mass`。读取与写入使用字段语法，绝不用 `getX()`/`setX()`。
- 任何接受参数、执行非平凡工作、或具有除存储值之外
  副作用的，都是**方法**，以祈使动词在前命名：
  `Input.isKeyDown(KeyCode.W)`、`world:findByName(name)`、
  `rigidBody:addImpulse(v)`。
- 实例上的方法使用 `:`（self）调用形式；命名空间表函数使用
  `.` 调用形式（`Physics.raycast(...)`，而非 `Physics:raycast(...)`）。

### 实体组件访问

实体句柄仅将始终存在的字段作为直接属性暴露——
`entity.valid`、`entity.id`、`entity.name`、`entity.active`、`entity.visible`、
`entity.transform`——外加层级方法（`destroy`、`parent`、
`firstChild`、`nextSibling`、`setParent`）。**其他**每一个组件都通过
一套以 `Component` 枚举标记为键的通用 Unity 风格方法集来访问，
绝不通过逐组件的属性：

- `entity:addComponent(Component.X)` —— 若不存在则添加，并返回该组件引用。
- `entity:getComponent(Component.X)` —— 返回该引用，若不存在则返回 `nil`
  （第 4 节"未找到返回 `nil`"规则）。
- `entity:removeComponent(Component.X)` —— 若移除了一个则返回 `true`。
- `entity:hasComponent(Component.X)` —— 布尔值。

这些是 camelCase 方法（第 1 节）；`Component` 是一个 PascalCase 枚举表，
其成员是 PascalCase 标记（`Component.RigidBody`、`Component.Camera`、
……），并且是唯一跨边界传递的东西——原始整数绝不泄露
（第 1 节，枚举规则）。返回的引用就是别处记录的同样的有类型
组件引用 usertype；只有访问路径是通用的。逐组件的访问器属性
（`entity.rigidBody`）、逐组件的 `has<Name>()` 方法，以及
`World.add*/remove*` 组件家族**不**属于该表面。

## 3. 单位

- **默认值不带后缀**：距离以米计，时间以秒计，角度以
  **度**计（面向设计师的约定，符合 Unity/Godot 的预期）。
- 任何非默认单位**必须**加后缀：`Px`（UI 像素）、`Ms`
  （毫秒）、`Radians`。示例：`anchoredPositionPx`、`fadeOutMs`。
- 规范的欧拉旋转属性是 `rotation`（Vec3，度）。旧版的
  `rotationEuler` / `rotationDegrees` 对已弃用。仅在出现真实需要时
  才添加 `rotationRadians`。

## 4. 错误与 nil

每一种失败类别恰好有一种风格；模块不得混用：

- **"未找到"查询返回 `nil`**——绝不返回 `false`，绝不报错：
  `World.findByName(name) -> Entity|nil`、`Camera.findPrimary() -> Camera|nil`。
- **程序员错误抛出异常**（参数类型/数量错误）——sol2 的默认
  行为；不要捕获并返回。
- **运行时可失败的操作返回 `ok, err`**：
  `local ok, err = Scene.load(uri)`。`err` 是人类可读的字符串。
- 句柄/引用类型暴露一个 `valid` 属性；调用一个无效
  句柄会抛出异常，而非静默地无操作。

## 5. 选项表模式

参数多于 3 个、或有 2 个及以上可选参数的函数，采用
`(required..., opts)` 形式，其中 `opts` 是一个有文档记录的键的表：

```lua
Audio.playMusic(clip, { volume = 0.8, loop = true, fadeInMs = 250 })
```

不要堆出长的位置可选尾部（`f(a, b, nil, nil, 0.5)` 是规范
违例）。

## 6. 事件与信号

- 规范形式是一个支持 `:connect(fn)`（多订阅者）的 `onX` 信号属性
  ——`button.onClick:connect(handler)`。
- 事件回调接收单个事件对象（`event.target`、
  `event:stopPropagation()`），而非位置参数列表。
- 禁止为同一事件设置重复别名（例如在 `onClick` 旁边放 `clicked`）；
  既有的此类别名已弃用。

## 7. 弃用策略

在 1.0 之前，允许重命名，但绝不静默：

1. 新名称落地；旧名称作为别名保留**一个发布版本**。
2. 别名每会话记录一次性警告：
   `[Lua] 'vec3' is deprecated, use 'Vec3'`（单一共享的垫片机制——
   一旦 `script_deprecations.cpp` 存在即参见之；不要逐模块手写
   警告）。
3. 别名在下一个发布版本中被删除。
4. 仓库内的所有 Lua（示例、模板、内置脚本、文档样例）在与重命名
   **相同的 PR** 中迁移。别名仅为外部项目而存在。

## 8. 覆盖规则（绑定对等）

一个面向玩法的引擎特性，在其 Lua 表面满足以下条件之前都不算"完成"：

- 遵循本规范进行绑定，
- 出现在 LuaLS stub 中（`tools/lua-stubs/vultra.lua`），
- 在 `doc/lua_scripting.md` 中有文档记录，
- 在 `tests/lua_api_conformance` 中通过（绿色）。

一个添加面向玩法的组件或系统却不具备这些的 PR 评审不通过。
仅供编辑器使用或引擎内部的系统（作业系统、GPU 资源、着色器
服务）按设计豁免；一致性测试的根表清单是
权威范围。

## 9. 有文档记录的例外

- **`ImGui.*`**（当其落地时，第 4 阶段）：保留上游 Dear ImGui 的 PascalCase
  函数名（`ImGui.Begin`、`ImGui.Button`），以便上游文档与社区
  知识直接迁移。一致性测试将 `ImGui`
  表列入白名单。
- Lua 标准库全局变量显然不在范围内。

## Enforcement

`tests/lua_api_conformance` 启动一个注册了完整绑定
集的无头 Lua 状态，然后：

1. 枚举活动的 API 表面（根表、成员、usertype），
2. 断言上述命名规则，
3. 将该表面与 `tools/lua-stubs/vultra.lua` 交叉检查
   （一个没有活动对应物的 stub 符号是**硬失败**——陈旧的
   stub 绝不可接受；一个在 stub 中缺失的活动符号必须
   列在 `exceptions.lua` 中），
4. 要求每一处违例要么被修复，要么存在于已签入的
   `exceptions.lua` 基线中。该基线只能缩减。

运行方式：

```
xmake build test-lua-api-conformance
xmake run test-lua-api-conformance
```

在有意修复违例后重新生成燃尽基线
（该文件必须是 ASCII/UTF-8——PowerShell 的 `>` 写入 UTF-16，而 Lua
无法解析）：

```powershell
& <target-dir>\test-lua-api-conformance.exe --dump |
    Out-File -Encoding ascii tests\lua_api_conformance\exceptions.lua
```

## Binding generator

> **要添加或更改绑定？请阅读 [script_binding_codegen.md](script_binding_codegen_CN.md)** ——
> 那是创作标准（注解词汇、决策指南、配方、
> 分步骤流程，以及每个新绑定必须遵循的约定）。下面的摘要
> 是其原理（rationale）；那篇文档才是操作指南（how-to）。

绑定通过一条两阶段、基于 IR、多语言的流水线从带注解的 C++ 生成。
注解是语言中立的 `VBIND_*` 标记
（参见 `vultra/core/base/script_annotations.hpp`）；一个 libclang 前端将
它们提取到一份已签入的 IR 中，各语言后端再从该 IR 发出绑定：

```
# stage 1: annotated headers -> tools/bindings/ir/bindings.ir.json
python tools/python/extract_bindings.py
# stage 2: IR -> sol2 .gen.cpp + the generated LuaLS stub sections
python tools/python/gen_lua.py
```

两者都通过 `lua-codegen` xmake 目标自动运行（尽力而为，
打戳保护）。IR——而非任何单个生成文件——是单一的真相
来源；LuaLS stub 本身就是一个后端输出，因此它不可能偏离被绑定的
表面。未来的 Python / C# 后端读取同一份 IR，而无需重新运行
C++ 提取。前端在提取时强制执行上述命名规则。

一个**区域（area）** = 一个 `registerScript<Area>Bindings` + 一个 `.gen.cpp`（即既有的
手写注册器，因此生成文件是即插即用的）。区域聚合
所有以 `area=` 标记的内容：命名空间表、usertype、struct、enum、原始
钩子（raw hook）。注解词汇表：

* **模块（Module）**（`VBIND_MODULE` + `VBIND_FN`）——命名空间表。两种主体类型：
  `serviceForward`（直接注解一个服务接口；生成器发出
  整个主体 `ctx.<svc>->method(args)`，带服务空检查 + 对实体参数的 `isValid`
  保护——无需手写代码）以及 `shimCall`
  （在一个 `namespace vultra` 自由函数上用 `VBIND_FN(... body = shim)`，该函数第一个参数为
  `ScriptContext&`；生成器进行转发，手写的垫片主体负责
  不规则胶水代码：选项表、表构建、多返回值、UUID-or-uri）。
* **Usertype**（`VBIND_USERTYPE` + `VBIND_PROPERTY` getter/setter +
  `VBIND_FN(usertype=...)` 方法）——实体引用句柄 usertype。`component=`
  自动发出 `valid`；`postRegister=` 在注册后运行一个钩子（例如
  `Entity` 上生成的组件访问器）。
* **枚举（Enum）**（`VBIND_ENUM`）——枚举值直接读取，绝不手工逐一列出。
* **值 struct**（`VBIND_STRUCT` + `VBIND_FIELD`，或用 `allFields` 绑定每个
  公有字段）——取代旧版 `VLUA_CLASS`/`VLUA_FIELD` 组件模式。
* **原始钩子**（在一个 `void f(sol::state&, ScriptContext&)` 上用 `VBIND_RAW`）——
  用于无法声明式表达的、不可化简的纯 sol2 注册的
  逃生舱口：常量表（`Layer`）、`meta_function` 运算符（`Math`
  Vec 类型），以及信号 connect/dispatch 机制（`UI`）。区域注册器
  调用该钩子；主体是手写的。**只手写主体，绝不手写表面。**

重命名使用 `VBIND_FIELD(name = newName, deprecated = oldName)`（或
`VBIND_FN`/`VBIND_PROPERTY` 上对应的选项），它同时发出 warn-once 别名。

**状态：** 一切都通过这一条流水线运行。每一个 API 表面
子系统都已迁移——Input、Time、Script、Scene、Asset、Upscaler、Audio、
Render、Animation、Transform、Entity、World、Physics、Math、UI、Editor——以及
纯数据**组件**（Camera/Light/shapes/audio/probe/particle）也是如此：它们的
字段绑定（在有类型的引用句柄上的 `requireComponentRef` get/set）
无需垫片即可生成。组件不作为逐实体的属性暴露；它们通过通用的
`entity:getComponent/addComponent/removeComponent/hasComponent(Component.X)`
API（参见上文"实体组件访问"）来访问，以 `Component` 枚举标记为键。旧版的
`gen_lua_bindings.py` 已退役；提取器同时读取 `VBIND_*` 和
组件既有的 `VLUA_CLASS`/`VLUA_FIELD`（将 `ref=`->`handle=` 映射）。只有
协程运行时 + 弃用别名注册表（引擎机制，而非 API
表面）仍为手写。ImGui 由其自有的 `gen_imgui_lua.py`
从 dear_bindings 元数据生成（不走 VBIND_* IR 流水线）。
一致性测试 + `exceptions.lua` 燃尽在每一步证明了对等
（303 -> 15 入基线；剩余的是 `Layer`/`UI` 原始钩子表面，未来一个
针对原始钩子的 stub 后端可以覆盖它们）。

## Review checklist

任何绑定变更的评审者（人类或 AI）核验：

- [ ] 名称遵循第 1 节（PascalCase 类型/表/枚举，camelCase 成员，
      `isX/hasX/canX` 布尔，`onX` 信号）。
- [ ] 无参的廉价访问器是属性，而非 `getX()/setX()`。
- [ ] 单位：米/秒/度不带后缀；其余一切都加后缀。
- [ ] 失败风格符合第 4 节，且在模块内保持一致。
- [ ] >3 个参数或 2 个及以上可选参数使用选项表。
- [ ] 无新的重复别名；重命名走弃用垫片。
- [ ] stub（`tools/lua-stubs/vultra.lua`）已更新；`doc/lua_scripting.md`
      已更新；一致性测试为绿色且未增长 `exceptions.lua`。
