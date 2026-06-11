# Plugin Repository Manager Handoff

Date: 2026-06-11

## Done

- Added Project Settings -> Plugins import controls for:
  - Git URL
  - plugin catalog JSON URL/path
  - local ZIP
  - local folder
- Git URL imports clone/update under `.vultra/plugins/git/` and load the discovered plugin directly
  from that managed cache. They do not copy network-managed plugins into `<asset-root>/plugins`.
- `.vultra/plugins/git/` is only a cache. Lock refresh must not treat every cached checkout as an
  installed plugin; only ids already in `vultra.plugins.lock`, plus the plugin id currently being
  imported, are written back for managed git plugins.
- Catalog/Git import accepts an existing cache folder that contains `vultra.plugin.vmanifest` even
  if the folder is not a full git checkout. This lets previously downloaded/release-style plugin
  payloads be re-registered into `vultra.plugins.lock` without recloning.
- Importing a plugin only installs/registers it in `vultra.plugins.lock`. Imported plugins are forced
  to remain disabled in the project enabled list until the user explicitly enables them.
- Catalog imports first fetch a local/remote `plugins.json` and render it as a scrollable list. The
  user imports one selected plugin at a time; entries are installed into `.vultra/plugins/git/`.
- On Windows, remote catalog downloads use WinHTTP inside the editor; they do not require `curl.exe`
  on PATH. Non-Windows still needs a proper HTTP backend.
- Plugin import actions run on a background `std::future`; the Project Settings UI shows a top-level
  loading modal while clone/download/extract work is in flight, then closes it when the task finishes.
- During editor project configuration, `vultra.plugins.lock` is read before renderer/plugin startup.
  Missing git-managed caches are restored into `.vultra/plugins/git/` from each lock entry's source.
- Project Settings -> Plugins supports removing installed plugins. Local plugin removal deletes the
  folder under `<asset-root>/plugins`. Git/catalog-managed plugin removal only disables/removes the
  lock entry and keeps `.vultra/plugins/git` as a reusable cache; cache pruning should be a separate
  command.
- Disabling or removing a loaded plugin now runs the per-plugin unload chain: Lua `on_uninstall`,
  native `EnginePlugin::uninstall`, plugin destroy callback, then DLL unload. Local folder removal is
  skipped if native unload fails, so Windows DLL locks do not leave the project in a half-removed
  state.
- ZIP imports extract under `.vultra/plugins/zip-imports/` and install into `<asset-root>/plugins`.
- Folder imports install directly into `<asset-root>/plugins`.
- Imports refresh project-root `vultra.plugins.lock` with plugin id, name, version, source
  metadata, manifest fingerprint, file count, and byte size.
- `.gitignore` no longer ignores native payloads inside `resources/plugins`; plugin install folders
  under an asset root are treated as project-versioned plugin content. `.vultra/` remains ignored as
  local cache.
- Created and pushed private GitHub repositories:
  - `https://github.com/zzxzzk115/vultra-plugins`
  - `https://github.com/zzxzzk115/vultra-plugin-hello`
  - `https://github.com/zzxzzk115/vultra-plugin-streamline`
- Each repository has `main` and `v0.1.0`.

## Update 2026-06-11 (catalog v2, versioning, UI rework)

- `vultra-plugins` catalog is schema v2: each plugin carries a newest-first `versions[]` array
  (version, optional notes, git source with an immutable `vX.Y.Z` tag ref); the top-level
  `version`/`source` pair stays as the latest release for v1 readers.
- `vultra-plugin-streamline` is at v0.2.1: env vars are build-time only (SDK paths / DLL copy).
  At runtime the plugin resolves the Streamline bin folder from, in order: explicit env override,
  SDK root, the engine-provided plugin root, its own module directory. NOTE: the bundled
  `vultra_plugin_dlss.dll` in the repo is still the v0.1.0 build — it must be rebuilt on a machine
  with the Streamline SDK and re-pushed before zero-config runtime actually works.
- Engine: new optional plugin export `void vultraSetPluginRoot(const char*)`, called by
  `PluginManager::load` after `vultraCreatePlugin()` and before `install()` with the plugin's
  absolute root directory. No global state; old plugins simply lack the symbol.
- Editor plugin architecture (single responsibility, three layers):
  - `editor_app/plugin_repository.{hpp,cpp}` (`vultra_app::plugins`): stateless repository
    operations — catalog fetch/parse (schema v1 + v2), git/zip/folder imports (git accepts a tag
    `ref`, which is how install/update/rollback all work), lock management, remove, version compare.
  - `editor_app/editor_plugin_manager.{hpp,cpp}` (`EditorPluginManager`, owned by `EditorApp` as
    `m_PluginManager`): owns editor-side state and async orchestration — installed-manifest scan
    cache (never rescans while an import rewrites cache files; that race produced transient
    "invalid JSON / empty input" manifest warnings), catalog entries/status/in-flight fetch, the
    single in-flight import future, one-shot `takeFinishedImport()`. `update()` is polled each
    frame from `drawProjectSettingsPopup` (even while the dialog is hidden).
  - `settings_windows.cpp` page 4: pure rendering + forwarding; keeps only UI state (input
    buffers, dialog-open flags, version combo choices, search text, vproject form state).
  - The lock records the pinned ref and `restoreLockedGitPlugins` clones at that ref.
- Editor UI: the Plugins page auto-fetches the official catalog in the background on dialog open
  (with cached-copy fallback offline), renders it as a searchable table (name/description/version
  combo/action), shows Install/Installed/Update/Rollback per selected version, and flags installed
  plugins with an "update available" notice + one-click update. i18n strings added for
  en/zh-CN/ja/ko (the plugin page is now fully localized in all four).
- Enable/disable toggles persist into the `.vproject` immediately (surgical load-modify-save of
  `enabledPlugins` only), so a restart-required enable survives without pressing Save.
- Manifest gained `restartRequired: true`; `loadPhase: pre_render_device` implies it
  (`PluginManifest::needsRestartToApply()`). Enabling such a plugin skips the (too late) immediate
  load, shows a "Restart Required" prompt, and "Restart Now" relaunches the editor.
- Disable of a restart-level plugin is restart-level too: unloading mid-session crashed (live
  Vulkan hooks torn down under the device), so the toggle only records + persists the disable and
  prompts a restart (no prompt if it was enable-pending and never loaded). While such a plugin is
  loaded, Remove and catalog Update/Rollback are disabled with an explanatory note (DLL is locked
  and a live unload is unsafe); the path is disable -> restart -> then update/remove.
- Relaunch lives in `common/process_relaunch.{hpp,cpp}` (`relaunchIntoProject`): detached
  `cmd /c timeout … & start` helper re-runs the executable with the original options carried over
  but `--editor --project <dir>` forced to the current project (a launcher-started session has no
  `--project`, so replaying the raw command line would land back in the launcher and miss the
  pre-render-device window). The project launcher uses the same hand-off: opening a project whose
  enabled plugins `needsRestartToApply()` (`plugins::projectNeedsRelaunchForPlugins`) relaunches
  into it instead of transitioning in-process (the device already exists by then).
- Catalog downloads append a throwaway `nocache=<epoch>` query param so raw.githubusercontent.com's
  ~5-minute CDN cache never serves a stale catalog to Refresh.
- 2026-06-11 later: rebuilt `vultra_plugin_dlss.dll` locally (Streamline SDK release zip at
  `../streamline-sdk-v2.11.1`; temp `includes("../vultra-plugin-streamline/xmake.lua")` in the root
  xmake.lua + `VULTRA_DLSS_STREAMLINE_SDK_ROOT`; needs `XMAKE_GLOBALDIR=C:\Users\sc23kz\CodeRepository\xmake-global`).
  Both plugin repos cleaned of `.vimport` files and version-reset to a fresh v0.1.0 tag (old tags
  deleted); catalog reset to single 0.1.0 entries.
- `xmake build -y vultra-app` passed after the rework. libvultra changes are uncommitted pending
  user verification.

## Notes

- `vultra-plugin-streamline` currently includes native DLL payloads. GitHub accepted the push, but
  warned that `nvngx_dlss.dll` is larger than 50 MB. Before public distribution, decide between Git
  LFS, release assets, or a private binary delivery policy.
- The editor import UI is intentionally plain text input first. A later pass should add native file
  pickers and progress/reporting.
- `vultra-plugins/plugins.json` exists as the central catalog, and the editor can import one selected
  git-backed entry at a time from a raw/local catalog JSON. A later pass should add per-plugin
  version selection.

## Verified

- `xmake build -y vultra-app` passed.
- `xmake build -y vultra-app` passed again after adding per-plugin unload/DLL unlock.
- `xmake build -y vultra-app` passed after fixing catalog disabled-scope imbalance and managed-cache
  lock refresh.
- `xmake build -y vultra-app` passed after allowing non-git managed cache folders with manifests to
  be imported from the catalog.
- `xmake build -y vultra-app` passed after enforcing imported plugins default to disabled.
- `gh repo view` confirmed all three repositories exist and are private.
