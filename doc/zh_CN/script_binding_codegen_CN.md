# Script Binding Codegen — 创作标准

[English](../script_binding_codegen.md) | **简体中文**

这是在 libvultra 中添加或更改 Lua 脚本绑定的**规范性指南**。
绑定通过一条两阶段 IR 流水线从 `VBIND_*` 注解**生成**；你几乎
从不手写注册代码。在改动 `source/vultra/.../scripting/bindings/` 下的
任何内容之前，请先阅读本文档。

配套文档：[lua_api_design.md](lua_api_design_CN.md) 是规范性的 Lua API
*表面*规范（命名、属性 vs 方法、错误模式）。本文档是
*codegen 机制*——如何表达一个绑定以使生成器发出它。

---

## 1. 架构（运行的是什么）

```
annotated C++ headers  ──libclang──►  bindings.ir.json  ──backends──►  script_<area>_binding.gen.cpp
(VBIND_* / shim decls)   stage 1        (checked-in IR)     stage 2     tools/lua-stubs/vultra.lua (LuaLS stub)
```

- **阶段 1** `tools/python/extract_bindings.py` 解析头文件（以
  `-DVULTRA_BINDGEN` 编译，使 `VBIND_*` 变为 `[[clang::annotate]]`），生成一份
  已签入的 IR：`tools/bindings/ir/bindings.ir.json`（schema：
  `tools/bindings/ir/bindings.ir.schema.json`）。
- **阶段 2** `tools/python/gen_lua.py` 运行各后端（`tools/python/bindgen/
  backends/`）：`lua_sol2` → `.gen.cpp`；`lua_typestub` → LuaLS stub 的生成
  部分。
- **IR 是单一的真相来源**。stub 是一个后端输出，因此它
  永不可能偏离。未来的 Python/C# 后端读取同一份 IR。
- 一切都已**签入**（IR + `.gen.cpp` + stub），因此构建从不需要
  Python。`xmake build` 通过 `lua-codegen` 依赖自动重新生成
  （尽力而为，打戳保护）；`xmake codegen` 按需运行它。

**以区域为中心。** 一个*区域（area）* = 一个 `registerScript<Area>Bindings(sol::state&,
ScriptContext&)` + 一个 `script_<area>_binding.gen.cpp`。一个区域聚合
所有以 `area=` 标记的内容：命名空间表（模块）、usertype、值 struct、
枚举、原始钩子。注册器名称 + 头文件是既有的手写
名称，因此生成文件是**即插即用**的（`script_binding.cpp` 保持不变）。

**硬性规则**
- **绝不手工编辑 `*.gen.cpp`**——它会被覆盖。请改注解。
- 引擎 API 保持 **Lua 无关**——`core/`/`function/` 服务接口上
  不出现 `sol::` 类型。sol 只存在于垫片/`.gen.cpp` 代码中。
- **注册绝不可解引用服务**——一致性测试
  针对一个全空的 `ScriptContext` 进行注册。服务访问发生在
  lambda 主体内部，受保护。

---

## 2. 决策指南——用哪个注解？

```
Adding a Lua function / type. Is it…

a namespace function (Input.isKeyHeld, Physics.raycast)?
├─ a clean 1:1 forward to a service method?      → VBIND_MODULE + VBIND_FN  (serviceForward — zero hand code)
└─ irregular (options table, builds a Lua table, ├─ → VBIND_FN(body=shim) + a hand-written shim fn
   multi-return, UUID-or-uri, service fallback)?

an entity-backed type (Transform, RigidBody, Animator, a component)?
├─ pure-data component (only fields)?            → VBIND_USERTYPE + VBIND_FIELD on the component struct (requireComponentRef — zero hand code)
└─ has methods / irregular props?                → VBIND_USERTYPE + VBIND_PROPERTY (shim get/set) + VBIND_FN(usertype=) methods

a plain value struct returned to Lua (RaycastHit)? → VBIND_STRUCT (+ allFields)
a C++ enum exposed as a table (KeyCode)?           → VBIND_ENUM
irreducibly sol2 (operators, signal machinery,     → VBIND_RAW on void f(sol::state&, ScriptContext&)
  constant tables, ad-hoc Lua tables)?
```

**优先选择零手写代码路径**（serviceForward、组件 `VBIND_FIELD`）。
仅当胶水代码确实无法成为一次干净的 C++ 转发时才动用垫片。仅
对有状态的机制或不可化简的 sol2 惯用法才动用 `VBIND_RAW`。

---

## 3. 注解参考

所有宏都在 `vultra/core/base/script_annotations.hpp` 中。它们在普通构建中
展开为空。

| 宏 | 作用于 | 用途 |
|---|---|---|
| `VBIND_MODULE(name=, area=, service=)` | 一个 class/struct（一个服务接口，或一个标记 struct） | 在 `area` 中声明一个 `name` 命名空间表；`service=` 是被空检查的 `ScriptContext` 成员 |
| `VBIND_FN(name=, body=shim, self=, service=, null=, deprecated=, module=, usertype=)` | 一个 `VBIND_MODULE` 类的方法，**或**一个自由垫片函数 | 一个命名空间函数，或当给出 `usertype=` 时为一个 usertype 方法（由 `usertype=` 而非 `self=` 选择方法形式；`self=` 是可选的句柄提示）。服务方法上不带 `body=shim` → **serviceForward**（生成完整主体）。`body=shim`/自由函数 → 转发到一个手写垫片 |
| `VBIND_USERTYPE(name=, handle=, area=, component=, accessor=, postRegister=)` | 一个 struct/class（一个组件 struct，或一个标记 struct） | 实体引用 usertype。`handle=` 引用 struct；`component=` → 自动 `valid` + `requireComponentRef`；`accessor=` → `entity.<accessor>`/`entity:has<Name>()`；`postRegister=` → 注册后运行 `fn(usertype&, ctx)` |
| `VBIND_FIELD(name=, readonly, deprecated=)` | 一个 `VBIND_USERTYPE`/`VBIND_STRUCT` 的数据成员 | 一个属性；在组件 usertype 上生成器发出 `requireComponentRef` get/set |
| `VBIND_PROPERTY(usertype=, name=, set=)` | 一个 getter 垫片函数 | 一个由手写 getter/`set=` setter 垫片支撑的 usertype 属性 |
| `VBIND_STRUCT(name=, module=, allFields)` | 一个值 struct | 将其绑定为一个 sol2 usertype；`allFields` 绑定每个公有字段 |
| `VBIND_ENUM(name=, stripE, module=)` | 一个 C++ enum | 绑定一个枚举表；`stripE` 去掉前导的 `e`（`eSpace`→`Space`） |
| `VBIND_RAW(area=)` | `void f(sol::state&, ScriptContext&)` | 逃生舱口；区域注册器调用它；主体手写 |

**垫片约定。** 一个垫片是一个 `namespace vultra` 自由函数，其**第一个
参数为 `ScriptContext&`**；其余为面向脚本的参数（对于
usertype 方法，第一个脚本参数是 `handle`）。生成器原样转发
`ctx` + 参数；垫片负责所有胶水代码（含服务空检查）。

**类型标记 / 编组**（serviceForward + 字段，自动处理）：
`bool int uint uint64 float double string`、`vec2/3/4` ↔ `ScriptVecN`、
`uuid`（CoreUUID ↔ string）、`entity`（entt::entity ↔ ScriptEntity；解包
`.value` 并添加一个 `isValid` 保护）、`enum`（↔ int）。垫片参数以
其原样 C++ 类型传递（因此 `sol::optional<...>`、`sol::table`、`sol::this_state`
可用）。

---

## 4. 配方

### 命名空间函数，serviceForward（无手写代码）
注解服务接口；生成器发出整个主体。
```cpp
class VBIND_MODULE(name = Input, service = inputService) IInputService {
    VBIND_FN() virtual bool isKeyHeld(KeyCode key) const = 0;   // Input.isKeyHeld(int)->bool
    VBIND_FN(null = 1.0f) virtual float timeScale() const = 0;  // null override; bool/0/0.f inferred otherwise
};
```

### 命名空间函数，垫片（不规则胶水）
`script_<area>_shim.hpp`（在清单中）+ `script_<area>_shim.cpp`（主体）：
```cpp
struct VBIND_MODULE(name = Asset, area = asset, service = assetService) AssetModule {};
VBIND_FN(module = Asset, name = loadText, body = shim)
ScriptTextAssetResult assetLoadText(ScriptContext& ctx, const std::string& uri);
```

### 带方法/属性的实体引用 usertype（垫片支撑）
```cpp
struct VBIND_USERTYPE(name = RigidBody, handle = ScriptRigidBodyRef, area = physics,
                      component = RigidBodyComponent, accessor = rigidBody) RigidBodyUsertype {};
VBIND_PROPERTY(usertype = RigidBody, name = mass, set = rbSetMass)
float rbGetMass(ScriptContext& ctx, const ScriptRigidBodyRef& self);
void  rbSetMass(ScriptContext& ctx, const ScriptRigidBodyRef& self, float value);
VBIND_FN(usertype = RigidBody, name = addForce, body = shim)
bool rbAddForce(ScriptContext& ctx, const ScriptRigidBodyRef& self, const ScriptVec3& force);
```

### 纯数据组件（无手写代码）
在组件 struct 本身上——字段通过 `requireComponentRef` 转发：
```cpp
struct VBIND_USERTYPE(name = Light, handle = ScriptLightRef, accessor = light) LightComponent {
    VBIND_FIELD() float intensity {1.0f};
    VBIND_FIELD(deprecated = oldName) glm::vec3 color {1.0f};
};
```
组件 usertype 默认归属 `components` 区域，并自动获得 `entity.light` /
`entity:hasLight()`。

### 值 struct / 枚举
```cpp
struct VBIND_STRUCT(name = PhysicsRaycastHit, module = Physics, allFields) ScriptPhysicsRaycastHit { ... };
enum class VBIND_ENUM(name = KeyCode, stripE, module = Input) KeyCode : uint16_t { eUnknown, eA, ... };
```

### 原始钩子（不可化简）
```cpp
VBIND_RAW(area = math) void mathRegisterTypes(sol::state& lua, ScriptContext& ctx);
```

---

## 5. 分步骤：添加一个绑定

1. **注解。** 要么注解一个干净的服务接口（serviceForward），要么
   为成形的 API 编写 `script_<area>_shim.hpp`（声明）+ `script_<area>_shim.cpp`
   （主体）。复用既有的 `script_<area>_binding.hpp`
   注册器/头文件名，使 `.gen.cpp` 成为即插即用。
2. 在 `tools/bindings/headers.json` 中**注册该头文件**。
3. **构建**（`xmake build <target>`）——`lua-codegen` 依赖重新生成 IR
   + `.gen.cpp` + stub。（或 `xmake codegen`。）
4. **如果你替换了一个手写的 `script_<area>_binding.cpp`，请删除它**（生成的
   `.gen.cpp` 现在定义了同一个 `registerScript<Area>Bindings`）。
5. **运行一致性测试：** `xmake run test-lua-api-conformance`。它必须通过（PASS）。
6. **燃尽 exceptions：** 对于新 stub 现在覆盖的任何 `stub.missing:*`，测试会
   报告 `stale baseline entry`——从
   `tests/lua_api_conformance/exceptions.lua` 中删除那些行。基线只能缩减。

---

## 6. 约定（标准）

- **命名**（提取时强制；另见 lua_api_design.md §1）：模块 /
  usertype / 枚举为 PascalCase；命名空间函数为 camelCase；usertype 成员
  与 struct 字段为 camelCase（实例成员不被一致性枚举，因此
  单位后缀 / 弃用别名在那里是允许的）。
- **每个注册器一个区域。** 多个命名空间可共享一个区域（例如
  `Camera`/`Render`/`RenderBackend` 都在 `render` 中）。命名空间甚至可
  跨区域拆分（`getOrCreateTable` 进行合并）。
- **即插即用注册器。** `script_binding.cpp` 调用 `registerScript<Area>Bindings`
  + 在 `registerScriptDeprecations` 之后的排序；不要重排序。生成的
  注册器复用那些精确的符号/头文件名。
- **重命名 / 弃用：** `VBIND_FIELD(name=new, deprecated=old)`（以及
  `VBIND_FN`/`VBIND_PROPERTY` 上对应的选项）发出 warn-once 别名。
- **确定性：** 生成器是改动才写入（write-if-changed）；重新运行必须是无操作。
  CI 运行 `xmake codegen && git diff --exit-code` 以捕获未重新生成的输出。

## 7. 什么保持手写（出于设计，而非缺口）

- **引擎机制，而非 API 表面：** 协程运行时
  （`script_coroutine_binding.cpp`）和弃用别名注册表
  （`script_deprecations.cpp`，必须最后运行）。
- **ImGui：** 其自有的生成器（`tools/python/gen_imgui_lua.py`），从
  dear_bindings 元数据生成。
- **`VBIND_RAW` 背后：** 有状态的信号 connect/dispatch（UI）、sol2
  `meta_function` 运算符（Math）、常量表（`Layer`）。这些是
  *实现*，而非逐绑定的垫片。

## 8. 验证清单（每次绑定变更）

- [ ] `xmake codegen` 重新生成，且无手工 `.gen.cpp` 编辑。
- [ ] `xmake run test-lua-api-conformance` → PASS；`exceptions.lua` 只缩减。
- [ ] 没有新的手写注册器（使用了流水线）。
- [ ] 重新运行 codegen 是无操作（`git diff` 干净）。
- [ ] 引擎服务接口没有新增 `sol::` 依赖。
