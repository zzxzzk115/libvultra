# 插件系统

[English](../plugins.md) | **简体中文**

Vultra 支持运行时插件，可以在不重新编译引擎的情况下扩展引擎。一个插件可以是：

- 一个 **原生 C++ 共享库**（封装第三方 SDK、添加系统、注册 Lua 绑定），
- 一个 **Lua 脚本**（粘合逻辑、玩法辅助、编辑器扩展），或者
- **两者皆有** —— 原生侧注册一套 Lua API，Lua 侧在其之上构建。

各组成部分：

| 层 | 类型 | 职责 |
|-------|------|----------------|
| [`PluginManager`](../../source/vultra/include/vultra/core/plugin/plugin_manager.hpp) | core | 对原生库执行 dlopen/LoadLibrary 并调用其 `EnginePlugin` |
| [`PluginSystem`](../../source/vultra/include/vultra/function/plugin/plugin_system.hpp) | function 子系统 | 解析清单、加载原生 + Lua 插件、生命周期 |
| [`IPluginService`](../../source/vultra/include/vultra/function/services/plugin_service.hpp) | 服务 | `discover` / `contentRoots` / `loadPlugin` / `unloadPlugin` / `loadedPlugins` / `isLoaded`（目录范围的发现+启用是 CLI 通过 `--plugins-dir` 完成的工作，而非服务方法） |

## 插件布局

一个插件是一个包含 `vultra.plugin.vmanifest` 清单的目录：

```
my_plugin/
  vultra.plugin.vmanifest   -- manifest (JSON)
  README.md                 -- optional
  init.lua                  -- optional Lua entry
  my_plugin.dll             -- optional native library (.dll/.so/.dylib)
```

清单是一个 JSON 文档：

```json
{
  "schemaVersion": 1,
  "id": "com.example.my_plugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "minEngineVersion": "0.11.0",
  "author": "Jane Doe",
  "description": "What this plugin does.",
  "readme": "README.md",
  "repository": "https://github.com/example/my_plugin",
  "platforms": ["windows", "linux", "macos"],
  "loadPhase": "pre_render_device",
  "editorOnly": false,
  "editorOnlyFiles": ["editor/**", "panels/*.lua"],
  "config": [
    {
      "key": "sdkRoot",
      "label": "SDK Root",
      "type": "path",
      "env": "MY_PLUGIN_SDK_ROOT",
      "required": true
    }
  ],
  "native": "my_plugin",
  "entry": "init.lua"
}
```

`id` 是用于启用插件的唯一句柄。`platforms`（为空/缺省 = 全部）将加载限制到
`windows` / `linux` / `macos` / `wasm` / `android`。`native`（扩展名会自动追加）
和 `entry` 都是可选的。`schemaVersion` 是一种向前兼容约定：插件清单解析器
目前会忽略它（只有 catalog 和 `vultra.plugins.lock` 文件会读取它），因此请保留它
以备将来使用，但它如今没有任何效果。

`minEngineVersion` 是可选的：插件支持的最低引擎版本，以点分语义化
字符串表示（例如 `"0.11.0"`）。为空/缺省表示未声明最低版本（始终兼容）。编辑器
会将其与正在运行的引擎版本进行比对，以对安装/启用进行门控。

`editorOnly` 和 `editorOnlyFiles` 将仅限编辑器的代码排除在导出的 VPK 之外（参见
[编辑器扩展 API](#编辑器扩展-api)）。两者都是可选的，且只影响打包 —— 编辑器
仍会正常发现、启用并加载这类插件/文件。

每个插件的加载顺序：**原生优先**（这样它的 `install()` 可以注册 Lua 粘合代码），然后 Lua
`entry` 运行并调用其 `on_install()`。

`loadPhase` 是可选的。默认是在 `ScriptSystem` 之后的正常插件阶段。当一个原生插件
必须在渲染设备创建之前安装引擎桥接钩子时，它可以设置 `"loadPhase": "pre_render_device"`。该早期
阶段只加载原生库；Lua `entry` 仍会在稍后的正常阶段运行。对于渲染后端桥接（例如需要在
实例/设备/交换链创建之前获得 Vulkan 钩子所有权的 DLSS/Streamline 插件），请使用此选项。

`config` 是用于编辑器/项目设置的可选自描述。每一项都有一个 `key`、显示用的
`label`、`type`（`string`、`path`、`bool`、`int`、`float`、`enum`）、可选的 `default`、可选的
`description`、可选的 `env`/`envVar`、`required`，以及可选的 `secret`（一个提示，表明该值是
机器本地的或敏感的，应从 `.env` 文件中获取，而不是提交到
`.vproject` —— 参见下方的 `.env` 指南）。一个 `enum` 参数还会声明
`"options": ["a", "b", ...]` 并被绘制为下拉框；存储/导出的值是选项
字符串。Project Settings -> Plugins 会绘制这些字段；编辑会保持挂起状态，直到按每个
插件单独应用（Apply 会持久化到 `.vproject`/`.env`，Revert 会恢复到上一次应用的状态）。
参数在插件加载时被读取，因此应用一个需要重启级别的插件配置时会提示
编辑器重启。在加载插件之前，引擎会将任何由 `env` 支持的值写入当前
进程环境。

机器本地或秘密的值应存放在项目 `.vproject` 旁边的 `.env` 文件中，而不是
`.vproject` 中。应用会在插件发现之前加载该文件；`.env` 已被 gitignore。

```ini
MY_PLUGIN_SDK_ROOT=C:\SDKs\my-plugin-sdk
MY_PLUGIN_TOKEN="local-only-token"
```

## 启用插件

插件从 `<project>/<asset-root>/plugins/` 发现，并且**默认关闭**。在项目安装本地插件之前，
该文件夹无需存在；使用默认资产根目录时，本地导入会创建 `resources/plugins`。按
项目启用插件：

- **编辑器：** *Project Settings → Plugins* 列出每一个被发现的插件及其元数据和一个
  启用开关。切换会立即持久化到 `.vproject`（无需 Save）；启用一个
  普通插件还会立即加载它，而引擎会在下次启动时加载已启用的集合。

  当启用/禁用只在下次启动时生效时，清单可以声明 `"restartRequired": true`；
  `"loadPhase": "pre_render_device"` 隐含了它（那些钩子必须在
  渲染设备存在之前安装，因此会话中途加载就太晚了）。启用这样的插件会记录
  它、跳过即时加载，并提议重启编辑器。禁用同样属于
  重启级别：实时卸载会拆除设备之下的渲染钩子，因此编辑器只
  记录该禁用并提议重启；在该禁用 + 重启发生之前，更新或移除一个已加载的插件会被阻止。
  重启会以 `--editor --project <dir>`（其他命令行选项会被沿用）重新启动
  进程，而当打开一个其已启用插件需要早期加载的项目时，项目启动器会使用相同的交接方式
  —— 进程内启动器切换发生在渲染设备已经存在之后。
- **运行时播放器 / CLI：** `--plugins-dir <dir>` 会发现并启用该
  目录中的每一个插件（一种显式的选择性加入）。
- **编程方式：** `EngineContext::Config::plugin.directory` + `plugin.enabled`（id 列表），或
  `IPluginService::discover()` / `loadPlugin()`。

Plugins 页面会在后台自动获取官方插件目录
（`https://raw.githubusercontent.com/zzxzzk115/vultra-plugins/main/plugins.json`），
并将其渲染为一个可搜索的列表，支持按插件选择版本。安装、
更新和回滚的工作方式都相同：选择一个版本，编辑器会将该
发布标签检出到受管缓存中。当目录中携带更新版本时，已安装的插件会显示"有可用更新"通知。
编辑器还可以从 Git URL、自定义目录 JSON URL/路径、本地 `.zip` 或本地文件夹手动导入插件。

受管（git/catalog）插件存放在项目旁边的一个 xmake-repo 风格的存储中：

```
.vultra/plugins/
  .cache/<owner>-<repo>/        git clone cache, one per repository url
  catalogs/<owner>-<repo>.json  downloaded catalog cache
  <plugin-id>/<version>/        immutable, materialized plugin payload (no .git)
```

每次安装都会在缓存中检出锁定的标签，并将载荷复制到
`<plugin-id>/<version>/`；`vultra.plugins.lock`（在 `.vproject` 旁边）记录每个插件
当前激活的版本目录，以及来源元数据、清单指纹和文件计数。
回滚会将锁重新指向一个已经物化的同级版本。缺失的版本
目录会在项目加载时，按锁定的引用从锁中重新物化。Zip 和文件夹
导入会安装到 `<asset-root>/plugins/` 中并成为项目内容。

受管存储被挂载为 `plugins://` VFS 方案：
`plugins://<plugin-id>/<version>/<path>` 映射到 `.vultra/plugins/<plugin-id>/<version>/<path>`，
因此插件文件会通过正常的资产服务加载。

已安装的插件会自动贡献渲染内容（即使在禁用状态下也是如此，这样渲染图
仍可继续编写和加载；插件的原生/Lua 侧是否运行由启用状态控制）：

- **脚本化渲染通道**从 `<plugin-root>/render/passes/*.lua` 发现。
  在类型冲突时，项目自身的通道胜出。
- **着色器库**：`<plugin-root>/shaders/` 下预编译的 `.vshlib` 制品会自动
  注册为库 `<plugin-id>`（第一个）以及 `<plugin-id>/<stem>`，因此一个插件
  通道可以使用 `shader = { library = "<plugin-id>", ... }`。插件以与发布预编译原生库相同的方式
  发布编译好的着色器制品；插件文件夹内的着色器源文件不会被项目导入器编译。

`<asset-root>/plugins` 一旦存在便成为项目内容。诸如 `.dll`、`.so`、
`.dylib`、`.lib` 和 `.pdb` 之类的原生运行时文件被特意允许存放在那里，这样一个项目或插件仓库就可以
对其可安装载荷进行版本控制。`.vultra/plugins` 仍然是本地缓存并被忽略。

## Lua 插件契约

入口脚本返回一个模块表；如果存在以下函数，`PluginSystem` 会调用它们：

```lua
local M = {}
function M.on_install()   ... end   -- at load
function M.on_uninstall() ... end   -- at shutdown (reverse load order)
return M
```

插件共享引擎的 Lua 状态，因此它们可以注册供普通实体脚本随后调用的全局变量，
或者扩展编辑器。参见 [examples/plugins/hello/](../../examples/plugins/hello/)。

## 原生插件契约

一个原生库导出一个返回 [`EnginePlugin`](../../source/vultra/include/vultra/core/plugin/engine_plugin.hpp) 的工厂：

```cpp
#if defined(_WIN32)
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PLUGIN_EXPORT vultra::EnginePlugin* vultraCreatePlugin();
PLUGIN_EXPORT void                  vultraDestroyPlugin(vultra::EnginePlugin*);

// Optional: receives the absolute path of the plugin's own root directory (UTF-8), once per
// load, after vultraCreatePlugin() and before install(). Use it to locate bundled payloads
// (runtime DLLs, data files) that ship next to the manifest.
PLUGIN_EXPORT void                  vultraSetPluginRoot(const char* absolutePath);
```

`install(EngineContext&)` 是粘合点。服务注册表以服务*名称*为键，因此
`ctx.services.tryGet<T>()` 会跨 DLL 边界解析到宿主的实例。要暴露一套 Lua
API，请取得共享的 Lua 状态：

```cpp
auto* script = ctx.services.tryGet<vultra::IScriptService>();
sol::state_view lua(script->luaState());
lua["my_api"].get_or_create<sol::table>().set_function("hello", []{ return 42; });
```

原生插件应避免使用引擎的日志/全局状态（改用 stdio），因为单独构建的
模块拥有自己的副本。参见 [examples/plugins/native_math/](../../examples/plugins/native_math/) 了解一个
原生 + Lua 组合示例。

`PluginSystem` 在 `ScriptSystem` 之后被构造，因此在 install 时，Lua 状态以及资产/场景服务对插件都是
可用的。

## 试一试

示例项目（[example.vproject](../../example.vproject)）启用了 `editor_panel` 插件
（`com.vultra.examples.editor_panel`），它位于
[resources/plugins/editor_panel/](../../resources/plugins/editor_panel/)，因此启动编辑器会加载
它：

```
# editor: Project Settings -> Plugins toggles the per-project enabled set
xmake run vultra-app --editor --project example.vproject
# [PluginSystem] Plugin 'Editor Panel Example' (com.vultra.examples.editor_panel) installed.

# runtime CLI opt-in (loads every plugin in the dir):
xmake run vultra-runtime --plugins-dir examples/plugins --render-mode none
# [native_math] length3(3,4,12) = 13.0   (after building plugin-native-math)
```

## 插件生命周期 v2

原生插件（`EnginePlugin`）和 Lua 插件都会获得每帧更新以及显式的 ABI/
依赖处理：

- **每帧更新。** 原生：重写 `void update(EngineContext&, float dt)`（默认为空操作）。
  Lua：在模块表上定义 `function M.on_update(dt)`。`PluginSystem` 每帧先逐个驱动所有原生
  插件，然后逐个驱动所有 Lua 插件，按加载顺序进行。
- **ABI 版本（原生）。** 导出 `VULTRA_PLUGIN_API unsigned int vultraPluginAbiVersion()`，
  返回 `vultra::kEnginePluginAbiVersion`。`PluginManager` 会拒绝一个其 ABI 与
  引擎不匹配的插件（给出清晰的"重新构建你的插件"错误，而不是崩溃）。一个没有该
  符号的插件会被视为传统 v1。
- **依赖。** 清单可以声明 `"dependencies": ["com.x.y", ...]`；`PluginSystem`
  会对已启用的集合进行拓扑排序，使依赖项先安装。一个缺失/禁用的依赖项会
  发出警告，但只会让该插件失败；环会被打破并伴随警告。Id 会被精确匹配
  （版本范围匹配是未来的工作）。

## 编辑器扩展 API

在编辑器中运行的插件（以及实体脚本）可以通过 `Editor` Lua 表添加 UI，
该表只在存在编辑器时存在 —— 请用 `if Editor then ... end` 进行保护。在
插件的 `on_install` 期间所做的注册会被标记上插件 id，并在卸载时自动拆除。

```lua
function M.on_install()
  if not Editor then return end
  Editor.registerPanel{
    id = "com.example.myplugin.stats", title = "Stats", defaultOpen = true,
    onDraw = function()           -- runs inside the panel's ImGui Begin/End
      ImGui.Text("Hello from a plugin panel")
      if ImGui.Button("Do it") then --[[ ... ]] end
    end,
  }
  Editor.registerMenuItem{
    id = "com.example.myplugin.rescan", path = "My Plugin/Rescan",  -- nested under Tools
    onClick = function() --[[ ... ]] end,
    enabledWhen = function() return true end,                       -- optional
  }
end
```

`registerPanel`/`registerMenuItem` 返回 `ok, err`。`Editor.unregister(id)` 会显式移除
一个（也会在卸载时自动移除）。`Editor.registerInspector` 是保留的（在实现之前返回
`false`）。参见 [resources/plugins/editor_panel/](../../resources/plugins/editor_panel/) —— 该
示例项目启用了它，因此启动编辑器会显示一个 "Lua Demo Panel" 和一个 Tools 菜单项。
面板主体用 `ImGui.*` Lua 绑定绘制（参见 [lua_scripting.md](lua_scripting_CN.md)）。

### 将仅限编辑器的代码排除在导出包之外

编辑器扩展代码从不在运行时构建中运行（在那里 `Editor` 全局变量是 `nil`），因此
VPK 导出器可以丢弃它，而不是发布无用的字节。两个可选的清单字段控制这一点：

- **`"editorOnly": true`** —— 整个插件都是仅限编辑器的。编辑器仍会加载它，但它会被
  **完全**排除在导出的 VPK 之外（没有文件，且包清单中也没有 `plugin_dirs` 条目）。
  对于像 [resources/plugins/editor_panel/](../../resources/plugins/editor_panel/) 这样的纯编辑器扩展，请使用此项。
- **`"editorOnlyFiles": ["editor/**", "panels/*.lua"]`** —— 针对**混合**插件（编辑器 UI +
  运行时逻辑）。每个 glob 都会与插件相对路径（使用正斜杠）进行匹配；匹配的
  文件会从 VPK 中丢弃，而插件的其余部分照常发布。`*` 匹配一段非 `/`
  字符，`?` 匹配一个这样的字符，`**` 匹配包括 `/` 在内的任意字符，而一个纯目录名
  会匹配其下的一切。排除对本地（`<asset-root>/plugins`）和受管
  （`.vultra/plugins`）安装都有效。

由于运行时永远不会看到被排除的文件，请将任何加载它们的代码（例如一个
`require "editor.panels"`）保护在 `if Editor then ... end` 之后，这样运行时构建就永远不会试图加载一个
已从包中丢弃的文件。

## 局限 / 未来工作

- 原生插件仅限桌面（动态加载）；wasm/android 插件需要一种不同的
  机制。
- 暂无热重载；插件在启动时 / 按需加载，并在关闭时卸载。
- `Editor.registerInspector`（自定义组件绘制器）和 gizmo 注册尚未实现；
  上面的面板/菜单接口是当前的编辑器扩展 API。
- 插件依赖声明精确匹配 id；semver 版本范围是未来的工作。
