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

A plugin is a directory containing a `plugin.lua` manifest:

```
my_plugin/
  plugin.lua      -- manifest (returns a table)
  init.lua        -- optional Lua entry
  my_plugin.dll   -- optional native library (.dll/.so/.dylib)
```

The manifest returns a table:

```lua
return {
  name   = "my_plugin",       -- optional, defaults to the folder name
  native = "my_plugin",       -- optional; platform extension is appended automatically
  entry  = "init.lua",        -- optional Lua entry script
}
```

Load order per plugin: **native first** (so its `install()` can register Lua glue), then the Lua
`entry` runs and its `on_install()` is called.

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

## Loading plugins

- **Auto-scan a directory** at startup: set `EngineContext::Config::plugin.directory`. Each
  immediate subdirectory with a `plugin.lua` is loaded.
- **From the launcher CLI**: `--plugins-dir <dir>` (wired into the runtime player and the editor).
  e.g. `vultra-runtime --vpk game.vpk --plugins-dir plugins`.
- **Programmatically**: `services.require<IPluginService>().loadPlugin(path)` or
  `loadPluginsFromDirectory(dir)`.

`PluginSystem` is emplaced after `ScriptSystem` so the Lua state and the asset/scene services are
available to plugins at install time.

## Try it

```
xmake run vultra-runtime --plugins-dir examples/plugins --render-mode none
# [PluginSystem] Plugin 'hello' installed.
# [native_math] length3(3,4,12) = 13.0   (after building plugin-native-math)
```

## Limitations / future work

- Native plugins are desktop-only (dynamic loading); wasm/android plugins would need a different
  mechanism.
- No hot-reload yet; plugins load at startup / on demand and unload at shutdown.
- A richer editor-extension API (panels, commands, gizmos registered from Lua) can build on the
  shared Lua state.
