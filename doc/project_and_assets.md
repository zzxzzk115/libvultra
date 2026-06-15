# Project layout, the asset pipeline, and the virtual file system

**English** | [简体中文](zh_CN/project_and_assets_CN.md)

This document explains how a Vultra project is laid out on disk, how source assets become
cooked runtime data, and how everything is addressed at runtime through the virtual file
system (VFS). It is the companion to the rendering and scene docs:
[Architecture](architecture.md), [Cross-platform export](cross_platform_export.md),
[Plugins](plugins.md), and [Lua scripting](lua_scripting.md).

## 1. Project files

A project is a directory containing one `.vproject` file plus an asset root (the
`resources/` folder by default). Project metadata files are small INI/TOML-like text:
a `[section]` header (ignored by the parser), `key = value` lines, `#` comments, and
optional double-quoted values.

### `.vproject`

The project descriptor, parsed by `loadVProject` in
[`source/vultra_app/src/vproject.cpp`](../source/vultra_app/src/vproject.cpp). Recognized keys:

| Key | Meaning |
|-----|---------|
| `version` | Format version marker (currently `1`); written on save, otherwise ignored on load. |
| `name` | Project / executable name. Defaults to the project directory name. |
| `asset_root` | Asset root folder relative to the project dir. Defaults to `resources`. |
| `default_scene` | `res://` uri of the scene opened by default. Falls back to the package `entry_scene` or the first `.vscn` under `scenes/` if missing. |
| `editing_rendergraph` | `res://` uri of the render graph used while editing. Defaults to `res://render/default.vrg.json`. |
| `build_scene.N` | `res://` uri of build scene at index `N`. |
| `build_scene_alias.N` | Optional player-defined alias for that scene (the canonical name is always the uri filename stem). |
| `build_scene_enabled.N` | Whether the scene is included in builds (`true`/`false`/`1`/`0`/`yes`/`no`/`on`/`off`). |
| `enabled_plugins` | Comma-separated list of plugin ids enabled for this project (plugins are off by default). |
| `plugin_config.<id>.<key>` | Per-plugin config value declared by a plugin manifest. |

The legacy `build_scene_name.N` key is migrated into `build_scene_alias.N` on load.

### `.env`

Machine-local environment variables, loaded by `loadProjectEnvFile`. Lines are
`KEY=value` (an optional `export ` prefix is stripped, values may be quoted). Each entry
is set into the process environment so secrets and machine-specific paths stay out of the
committed `.vproject`. This file is intended to be gitignored.

### `.vimport`

A per-source sidecar that records how a source asset was imported. It is text with
`[vimport]`, `[source]`, `[output]`, and `[params]` sections, for example:

```ini
[vimport]
version=1
importer="scene_manifest"
uid="ab252d9dc9df553c37dfc89a0cedc8bd"

[source]
file="scenes/test.vmanifest"

[output]
file="imported/scene_manifest/scenes/test.vmanifest"
```

The importer also maintains an import database (see
[`vasset_import_database.hpp`](../external/vasset/source/libvasset/include/vasset/vasset_import_database.hpp))
keyed by source path, tracking the importer, the cooked `output`, and `sourceHash` /
`dependencyHash` / `paramsHash` values used to decide when a reimport is needed.

### Other project files

- `.vscn` — scenes. See [Scenes and components](scene_and_components.md).
- `.vrg.json` — render graphs. See [Render graphs](render_graphs.md).
- `.vmanifest` (`vultra.package.vmanifest`) — the package manifest written next to the
  asset root, carrying `name`, `entry_scene`, the build-scene table, and `plugin_dirs`
  (the `res://` plugin directories bundled into a build).
- `resources.vpk` — the cooked runtime bundle (see below).

## 2. The asset pipeline (vasset)

Asset handling lives in the bundled [vasset](../external/vasset) library. The flow is:

```
source asset  ->  import  ->  UUID-based registry + cooked output  ->  pack  ->  resources.vpk
```

**Import.** Source files (`.gltf`/`.glb`/`.fbx`/`.obj`, `.png`/`.jpg`/`.hdr`/`.ktx2`/`.dds`,
`.ply`/`.spz`/`.splat`, audio, etc.) are scanned and converted into engine-native cooked
forms under the `imported/` folder. Each asset is assigned a stable UUID recorded in the
asset registry (`registryFile`); a `.vimport` sidecar and the import database track the
source-to-output mapping and content hashes so unchanged assets are skipped on the next pass.

**Asset types.** The registry classifies assets via `VAssetType`
([`vasset_type.hpp`](../external/vasset/source/libvasset/include/vasset/vasset_type.hpp)):
textures (including KTX2/Basis and DDS), materials, meshes, skeletons, animations,
Gaussian splats, scenes, scene manifests, Lua scripts and scriptable objects, render /
material / animator graph JSON, shader libraries, prefabs, audio, and fonts. Meshes,
textures, skeletons, and animations are cooked binaries resolved from their `imported/`
path; text-like assets (scenes, graphs, materials, scripts) resolve from their source path.

**Pack.** Cooking gathers the imported outputs (plus the registry and package manifest)
into `resources.vpk`, a VPK archive. When `loadFromVPK` is enabled the VPK's embedded
registry becomes the source of truth for UUID-to-path resolution.

### CLI and scripts

The `vultra asset` command (backed by the vasset CLI in
[`tool_cli.cpp`](../external/vasset/source/libvasset/src/tool_cli.cpp)) drives the pipeline:

```
vultra asset import <asset-root> [--reimport]
vultra asset pack   <asset-root> <out.vpk> [--zstd N] [--include ...] [--root ...]
vultra asset cook   <asset-root> <out.vpk> [--reimport] [--zstd N] ...
```

`import` cooks sources into `imported/`; `pack` bundles an already-imported tree into a
VPK; `cook` does import + pack in a single pass (no double scan). Convenience wrappers live
in [`scripts/`](../scripts): `import.ps1`/`.sh`, `pack.ps1`/`.sh`, and `cook.ps1`/`.sh`.
The pack/cook scripts default to `--zstd 6`. The scripts locate the `vultra` executable via
the `VULTRA` env var or the standard `build/.../vultra-app/` output.

## 3. The virtual file system (vfilesystem)

At runtime, assets are addressed by URI through a mounted VFS
([vfilesystem](../external/vasset/external/vfilesystem)), configured in
[`asset_system.cpp`](../source/vultra/src/function/asset/asset_system.cpp). Three schemes
are registered:

| Scheme | Backed by | Use |
|--------|-----------|-----|
| `res://` | the project asset root or a mounted `resources.vpk` | project / package assets |
| `builtin://` | the embedded `builtin.vpk` (see below) | engine resources |
| `plugins://` | the managed plugin store `<project>/.vultra/plugins/<id>/<version>/...` | managed plugin content |

The key property of `res://` is that it resolves the **same path** in both modes:

- **While editing**, the asset root is mounted through an editor remap filesystem over a
  physical filesystem, so loose source files and their `imported/` outputs are visible
  transparently. (Runtime-only builds without the importer mount the physical filesystem
  directly and expect pre-baked assets.)
- **In a packaged build**, `resources.vpk` is mounted read-only under `res://` and its
  embedded registry resolves UUIDs.

So `res://textures/foo.png` works identically whether the bytes come from a loose file in
the editor or from the cooked VPK in a shipped game.

## 4. Runtime asset loading

The `AssetSystem` (an engine subsystem providing `IAssetService`) loads assets on demand
and at a high level behaves as follows:

- **On-demand.** Assets are loaded when first requested (e.g. a texture referenced by a
  material, or a mesh referenced by a scene), not eagerly.
- **Async CPU load + GPU upload.** When async loading is enabled, reading and parsing the
  asset bytes happens on worker threads via a task scheduler; the parsed CPU data is then
  queued for GPU upload. GPU uploads are drained on the render thread a few per frame, so
  uploading never blocks worker threads.
- **Residency and refcounting.** Each asset record tracks a load state, a refcount, and the
  last frame it was used. After GPU upload the CPU copy is typically released (kept only
  when explicitly requested, e.g. skinned meshes that still need their skeleton). Idle,
  zero-ref CPU caches can be released after a configurable number of idle frames. Meshes,
  textures, and Gaussian splats become resident in GPU pools; textures register into a
  global bindless table.

The exact thresholds and pool layouts are implementation details — treat the above as the
intended behavior rather than a contract.

## 5. The builtin.vpk

Engine resources that ship inside the binary — GLSL shader libraries, fonts, builtin
render graphs, textures (LTC LUTs, environment maps, editor icons, cursors), and i18n
catalogs — are packed into a single **zstd-compressed** `builtin.vpk`. The pack is built
by the `builtinpack` host tool from a manifest in
[`xmake/builtin_pack_cook.lua`](../xmake/builtin_pack_cook.lua) (level 19, smallest blob;
already-compressed formats are stored uncompressed).

That VPK is then baked into each self-contained binary and mounted under the `builtin://`
scheme at startup (see
[`builtin/embed/builtin_pack_mount.cpp`](../builtin/embed/builtin_pack_mount.cpp)). The
embed mechanism is platform-specific: a Windows `.rc` RCDATA resource, `.incbin` symbols on
Linux/macOS, a preloaded `/builtin.vpk` in MEMFS on wasm, and an APK read on Android. Engine
code therefore reads e.g. `builtin://shaders/...`, `builtin://fonts/...`, or
`builtin://i18n/...` with no loose files on disk. See [i18n](i18n.md) for how catalogs are
embedded and [Plugins](plugins.md) for the `plugins://` store.
