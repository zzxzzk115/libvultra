# Plugin system

Vultra supports runtime plugins that extend the engine without rebuilding it. A plugin can be:

- a **native C++ shared library** (wrap a third-party SDK, add systems, register Lua bindings),
- a **Lua script** (glue, gameplay helpers, editor extensions), or
- **both** — the native side registers a Lua API and the Lua side builds on top of it.

The pieces:

| Layer | Type | Responsibility |
|-------|------|----------------|
| [`PluginManager`](../source/vultra/include/vultra/core/plugin/plugin_manager.hpp) | core | dlopen/LoadLibrary a native library and call its `EnginePlugin` |
| [`PluginSystem`](../source/vultra/include/vultra/function/plugin/plugin_system.hpp) | function subsystem | parse manifests, load native + Lua plugins, lifecycle |
| [`IPluginService`](../source/vultra/include/vultra/function/services/plugin_service.hpp) | service | `loadPlugin` / `loadPluginsFromDirectory` / `loadedPlugins` |

## Plugin layout

A plugin is a directory containing a `vultra.plugin.vmanifest` manifest:

```
my_plugin/
  vultra.plugin.vmanifest   -- manifest (JSON)
  README.md                 -- optional
  init.lua                  -- optional Lua entry
  my_plugin.dll             -- optional native library (.dll/.so/.dylib)
```

The manifest is a JSON document:

```json
{
  "schemaVersion": 1,
  "id": "com.example.my_plugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "author": "Jane Doe",
  "description": "What this plugin does.",
  "readme": "README.md",
  "repository": "https://github.com/example/my_plugin",
  "platforms": ["windows", "linux", "macos"],
  "native": "my_plugin",
  "entry": "init.lua"
}
```

`id` is the unique handle used to enable the plugin. `platforms` (empty/absent = all) gates loading
to `windows` / `linux` / `macos` / `wasm` / `android`. `native` (extension appended automatically)
and `entry` are both optional.

Load order per plugin: **native first** (so its `install()` can register Lua glue), then the Lua
`entry` runs and its `on_install()` is called.

## Enabling plugins

Plugins are discovered from `<project>/plugins/` and are **off by default**. Enable them per
project:

- **Editor:** *Project Settings → Plugins* lists every discovered plugin with its metadata and an
  enable toggle. Enabling loads it immediately and records the id in the `.vproject`
  (`enabled_plugins = "id1,id2"`); the engine loads the enabled set on the next launch.
- **Runtime player / CLI:** `--plugins-dir <dir>` discovers and enables every plugin in that
  directory (an explicit opt-in).
- **Programmatic:** `EngineContext::Config::plugin.directory` + `plugin.enabled` (list of ids), or
  `IPluginService::discover()` / `loadPlugin()`.

## Lua plugin contract

The entry script returns a module table; `PluginSystem` calls these if present:

```lua
local M = {}
function M.on_install()   ... end   -- at load
function M.on_uninstall() ... end   -- at shutdown (reverse load order)
return M
```

Plugins share the engine's Lua state, so they can register globals that ordinary entity scripts
then call, or extend the editor. See [examples/plugins/hello/](../examples/plugins/hello/).

## Native plugin contract

A native library exports a factory returning an [`EnginePlugin`](../source/vultra/include/vultra/core/plugin/engine_plugin.hpp):

```cpp
#if defined(_WIN32)
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PLUGIN_EXPORT vultra::EnginePlugin* vultraCreatePlugin();
PLUGIN_EXPORT void                  vultraDestroyPlugin(vultra::EnginePlugin*);
```

`install(EngineContext&)` is the glue point. The service registry is keyed by service *name*, so
`ctx.services.tryGet<T>()` resolves to the host's instance across the DLL boundary. To expose a Lua
API, grab the shared Lua state:

```cpp
auto* script = ctx.services.tryGet<vultra::IScriptService>();
sol::state_view lua(script->luaState());
lua["my_api"].get_or_create<sol::table>().set_function("hello", []{ return 42; });
```

Native plugins should avoid the engine's logging/global state (use stdio) since a separately built
module has its own copy. See [examples/plugins/native_math/](../examples/plugins/native_math/) for a
native + Lua combo.

`PluginSystem` is emplaced after `ScriptSystem` so the Lua state and the asset/scene services are
available to plugins at install time.

## Try it

The example project ([example.vproject](../example.vproject)) enables the `hello` plugin in its
[plugins/](../plugins/) folder, so launching the editor loads it:

```
# editor: Project Settings -> Plugins toggles the per-project enabled set
xmake run vultra-app --editor --project example.vproject
# [PluginSystem] Plugin 'Hello' (com.vultra.examples.hello) installed.

# runtime CLI opt-in (loads every plugin in the dir):
xmake run vultra-runtime --plugins-dir examples/plugins --render-mode none
# [native_math] length3(3,4,12) = 13.0   (after building plugin-native-math)
```

## Limitations / future work

- Native plugins are desktop-only (dynamic loading); wasm/android plugins would need a different
  mechanism.
- No hot-reload yet; plugins load at startup / on demand and unload at shutdown.
- A richer editor-extension API (panels, commands, gizmos registered from Lua) can build on the
  shared Lua state.
