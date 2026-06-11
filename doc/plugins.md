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
  "loadPhase": "pre_render_device",
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

`id` is the unique handle used to enable the plugin. `platforms` (empty/absent = all) gates loading
to `windows` / `linux` / `macos` / `wasm` / `android`. `native` (extension appended automatically)
and `entry` are both optional.

Load order per plugin: **native first** (so its `install()` can register Lua glue), then the Lua
`entry` runs and its `on_install()` is called.

`loadPhase` is optional. The default is the normal plugin phase after `ScriptSystem`. A native plugin
can set `"loadPhase": "pre_render_device"` when it must install engine bridge hooks before the render
device is created. That early phase loads only the native library; the Lua `entry` still runs later in
the normal phase. Use this for render backend bridges such as a DLSS/Streamline plugin that needs
Vulkan hook ownership before instance/device/swapchain creation.

`config` is optional self-description for editor/project settings. Each item has a `key`, display
`label`, `type` (`string`, `path`, `bool`, `int`, `float`), optional `default`, optional `description`,
optional `env`/`envVar`, and `required`. Project Settings -> Plugins draws these fields and can save
values into the `.vproject`; before loading a plugin, the engine writes any `env`-backed value into
the current process environment.

Machine-local or secret values should live in a `.env` file next to the project `.vproject` instead
of the `.vproject`. The app loads that file before plugin discovery; `.env` is gitignored.

```ini
MY_PLUGIN_SDK_ROOT=C:\SDKs\my-plugin-sdk
MY_PLUGIN_TOKEN="local-only-token"
```

## Enabling plugins

Plugins are discovered from `<project>/<asset-root>/plugins/` and are **off by default**. The folder
does not need to exist until a project installs local plugins; with the default asset root, local
imports create `resources/plugins`. Enable plugins per
project:

- **Editor:** *Project Settings → Plugins* lists every discovered plugin with its metadata and an
  enable toggle. Toggling persists into the `.vproject` immediately (no Save needed); enabling a
  normal plugin also loads it right away, and the engine loads the enabled set on the next launch.

  A manifest can declare `"restartRequired": true` when enable/disable only takes effect on the
  next launch; `"loadPhase": "pre_render_device"` implies it (those hooks must install before the
  render device exists, so a mid-session load would be too late). Enabling such a plugin records
  it, skips the immediate load, and offers to restart the editor. Disabling one is just as
  restart-level: a live unload would tear down render hooks under the device, so the editor only
  records the disable and offers the restart; updating or removing it while loaded is blocked
  until that disable + restart happened. The restart relaunches the
  process with `--editor --project <dir>` (other command-line options carried over), and the
  project launcher uses the same hand-off when opening a project whose enabled plugins need the
  early load -- an in-process launcher transition happens after the render device already exists.
- **Runtime player / CLI:** `--plugins-dir <dir>` discovers and enables every plugin in that
  directory (an explicit opt-in).
- **Programmatic:** `EngineContext::Config::plugin.directory` + `plugin.enabled` (list of ids), or
  `IPluginService::discover()` / `loadPlugin()`.

The Plugins page fetches the official plugin catalog
(`https://raw.githubusercontent.com/zzxzzk115/vultra-plugins/main/plugins.json`) automatically in
the background and renders it as a searchable list with per-plugin version selection. Installing,
updating, and rolling back all work the same way: pick a version, and the editor checks out that
release tag into the managed cache. Installed plugins show an "update available" notice when the
catalog carries a newer version. The editor can also import a plugin manually from a Git URL, a
custom catalog JSON URL/path, a local `.zip`, or a local folder.

Git and catalog imports keep clone/catalog caches under `.vultra/plugins/` and load the plugin from
that managed cache. Zip and folder imports install into `<asset-root>/plugins/`. The project writes
`vultra.plugins.lock` next to the `.vproject` with installed plugin versions, source metadata
(including the pinned git ref), manifest fingerprints, and file counts; missing managed caches are
restored from the lock at project load, at the locked ref.

`<asset-root>/plugins` is project content once it exists. Native runtime files such as `.dll`, `.so`,
`.dylib`, `.lib`, and `.pdb` are intentionally allowed there so a project or plugin repository can
version its installable payload. `.vultra/plugins` remains local cache and is ignored.

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

// Optional: receives the absolute path of the plugin's own root directory (UTF-8), once per
// load, after vultraCreatePlugin() and before install(). Use it to locate bundled payloads
// (runtime DLLs, data files) that ship next to the manifest.
PLUGIN_EXPORT void                  vultraSetPluginRoot(const char* absolutePath);
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
