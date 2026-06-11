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
