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

`id` is the unique handle used to enable the plugin. `platforms` (empty/absent = all) gates loading
to `windows` / `linux` / `macos` / `wasm` / `android`. `native` (extension appended automatically)
and `entry` are both optional.

`editorOnly` and `editorOnlyFiles` keep editor-only code out of exported VPKs (see
[Editor extension API](#editor-extension-api)). Both are optional and only affect packaging — the
editor still discovers, enables, and loads such plugins/files normally.

Load order per plugin: **native first** (so its `install()` can register Lua glue), then the Lua
`entry` runs and its `on_install()` is called.

`loadPhase` is optional. The default is the normal plugin phase after `ScriptSystem`. A native plugin
can set `"loadPhase": "pre_render_device"` when it must install engine bridge hooks before the render
device is created. That early phase loads only the native library; the Lua `entry` still runs later in
the normal phase. Use this for render backend bridges such as a DLSS/Streamline plugin that needs
Vulkan hook ownership before instance/device/swapchain creation.

`config` is optional self-description for editor/project settings. Each item has a `key`, display
`label`, `type` (`string`, `path`, `bool`, `int`, `float`, `enum`), optional `default`, optional
`description`, optional `env`/`envVar`, and `required`. An `enum` parameter also declares
`"options": ["a", "b", ...]` and is drawn as a dropdown; the stored/exported value is the option
string. Project Settings -> Plugins draws these fields; edits stay pending until applied per
plugin (Apply persists into the `.vproject`/`.env` and Revert restores the last applied state).
Parameters are read when the plugin loads, so applying a restart-level plugin's config offers an
editor restart. Before loading a plugin, the engine writes any `env`-backed value into the current
process environment.

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

Managed (git/catalog) plugins live in an xmake-repo style store next to the project:

```
.vultra/plugins/
  .cache/<owner>-<repo>/        git clone cache, one per repository url
  catalogs/<owner>-<repo>.json  downloaded catalog cache
  <plugin-id>/<version>/        immutable, materialized plugin payload (no .git)
```

Each install checks the locked tag out in the cache and copies the payload into
`<plugin-id>/<version>/`; `vultra.plugins.lock` (next to the `.vproject`) records which version
directory is active per plugin, plus source metadata, manifest fingerprints, and file counts.
Rollback re-points the lock at an already-materialized sibling version. Missing version
directories are re-materialized from the lock at project load, at the locked ref. Zip and folder
imports install into `<asset-root>/plugins/` and are project content.

The managed store is mounted as the `plugins://` VFS scheme:
`plugins://<plugin-id>/<version>/<path>` maps onto `.vultra/plugins/<plugin-id>/<version>/<path>`,
so plugin files load through the normal asset service.

Installed plugins contribute render content automatically (even while disabled, so render graphs
keep authoring and loading; whether the plugin's native/Lua side runs is governed by enabling):

- **Scripted render passes** are discovered from `<plugin-root>/render/passes/*.lua`. The
  project's own passes win on type collisions.
- **Shader libraries**: precompiled `.vshlib` artifacts under `<plugin-root>/shaders/` register
  automatically as library `<plugin-id>` (the first one) and `<plugin-id>/<stem>`, so a plugin
  pass can use `shader = { library = "<plugin-id>", ... }`. Plugins ship compiled shader
  artifacts the same way they ship prebuilt native libraries; shader sources inside plugin
  folders are not compiled by the project importer.

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

## Plugin lifecycle v2

Native plugins (`EnginePlugin`) and Lua plugins both get a per-frame update and explicit ABI/
dependency handling:

- **Per-frame update.** Native: override `void update(EngineContext&, float dt)` (default no-op).
  Lua: define `function M.on_update(dt)` on the module table. `PluginSystem` ticks all native
  plugins then all Lua plugins each frame, in load order.
- **ABI version (native).** Export `VULTRA_PLUGIN_API unsigned int vultraPluginAbiVersion()`
  returning `vultra::kEnginePluginAbiVersion`. `PluginManager` refuses a plugin whose ABI does not
  match the engine's (clear "rebuild your plugin" error instead of a crash). A plugin without the
  symbol is treated as legacy v1.
- **Dependencies.** A manifest may declare `"dependencies": ["com.x.y", ...]`; `PluginSystem`
  topologically sorts the enabled set so dependencies install first. A missing/disabled dependency
  warns but fails only that plugin; cycles are broken with a warning. Ids are matched exactly
  (version-range matching is future work).

## Editor extension API

Plugins (and entity scripts) running in the editor can add UI through the `Editor` Lua table,
which exists only when an editor is present — guard with `if Editor then ... end`. Registrations
made during a plugin's `on_install` are tagged with the plugin id and torn down automatically on
unload.

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

`registerPanel`/`registerMenuItem` return `ok, err`. `Editor.unregister(id)` removes one
explicitly (also automatic on unload). `Editor.registerInspector` is reserved (returns `false`
until implemented). See [resources/plugins/editor_panel/](../resources/plugins/editor_panel/) — the
example project enables it, so launching the editor shows a "Lua Demo Panel" and a Tools menu item.
The panel body is drawn with the `ImGui.*` Lua bindings (see [lua_scripting.md](lua_scripting.md)).

### Keeping editor-only code out of exported packages

Editor extension code never runs in a runtime build (the `Editor` global is `nil` there), so the
VPK exporter can drop it instead of shipping dead bytes. Two optional manifest fields control this:

- **`"editorOnly": true`** — the whole plugin is editor-only. The editor still loads it, but it is
  excluded **entirely** from exported VPKs (no files, and no `plugin_dirs` entry in the package
  manifest). Use this for pure editor extensions like
  [resources/plugins/editor_panel/](../resources/plugins/editor_panel/).
- **`"editorOnlyFiles": ["editor/**", "panels/*.lua"]`** — for **mixed** plugins (editor UI +
  runtime logic). Each glob is matched against the plugin-relative path (forward slashes); matching
  files are dropped from the VPK while the rest of the plugin ships. `*` matches a run of non-`/`
  characters, `?` one such character, `**` any characters including `/`, and a bare directory name
  matches everything beneath it. Exclusion works for both local (`<asset-root>/plugins`) and managed
  (`.vultra/plugins`) installs.

Because the runtime never sees excluded files, guard any code that loads them (e.g. a
`require "editor.panels"`) behind `if Editor then ... end` so a runtime build never tries to load a
file that was dropped from the package.

## Limitations / future work

- Native plugins are desktop-only (dynamic loading); wasm/android plugins would need a different
  mechanism.
- No hot-reload yet; plugins load at startup / on demand and unload at shutdown.
- `Editor.registerInspector` (custom component drawers) and gizmo registration are not implemented
  yet; the panel/menu surface above is the current editor-extension API.
- Plugin dependency declarations match ids exactly; semver version ranges are future work.
