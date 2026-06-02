# Asset Dependency and Residency Handoff

## Summary

The asset system already has transient `AssetHandle` reference counting, but
render/world code often stores UUIDs/URIs and requests short-lived handles while
cooking. Because of that, `AssetRecord::refCount` cannot be used directly as the
only signal for runtime unloading.

## Findings

- `AssetSystem::update()` has a GC hook but no policy.
- `AssetRecord::lastUsedFrame` exists but is not currently a complete residency
  signal.
- `GpuResourcePool` already supports deferred texture-slot reuse through
  `scheduleFreeTextureSlot()` / `processDeferredFrees()`.
- Mesh, material, and gaussian splat GPU pools are append-oriented and need
  generation-safe reclaim or compaction before full automatic GPU eviction.
- Dependency graph work belongs in registry/importer data first, with runtime
  residency pins layered on top.

## Recommended Next Step

Phase 1 has started:

- `VAssetRegistry` now stores dependency edges and serializes them as additive
  `@dep` rows in the TSV registry format.
- Dependency validation now reports missing required targets and simple cycles.
- Mesh import records material texture dependencies and skeleton dependencies.
- `dependents()` returns owner UUID plus the matching dependency edge so tooling
  can answer "who references this asset".

Next implementation step: extend graph extraction beyond imported meshes into
scene manifests, material graphs, shader libraries, scripts, and render graphs,
then start Phase 2 runtime pins.

Packaging now has a dependency-root mode:

- Source text imports scan `res://...` references and serialized UUID strings
  into dependency edges.
- `VpkPackOptions::rootPaths` enables reachable-asset packing from one or more
  root scene/assets.
- `vasset-cli pack` accepts repeated `--root <scene-or-asset>`.
- Editor runtime export passes both `vultra.package.vmanifest` and the entry
  scene as pack roots, so unused registry/raw assets are excluded from the VPK.
- Without `--root`, pack behavior remains the old full asset-root behavior.

Build scene lists are now represented:

- `.vproject` supports ordered `build_scene.<index>` entries, plus optional
  `build_scene_name.<index>` and `build_scene_enabled.<index>`.
- `vultra.package.vmanifest` writes the same ordered scene list.
- Old projects remain compatible: `default_scene` is normalized into build scene
  index 0 when no explicit list exists.
- Editor Project Settings now has a `Build Scenes` page. It can add the default
  scene, add all project `.vscn` assets, edit scene names/URIs, enable/disable
  scenes, reorder scene indices, and remove scenes.
- `AppState::currentBuildScenes` carries the project build scene list while the
  editor is open, and Project Settings `Save` preserves/writes the list.
- Editor export packs all enabled build scenes as roots, along with
  `vultra.package.vmanifest`.
- Editor export also packs all project `.vrg.json` render graphs as roots.
  Render graphs are small, while cameras only store `rendererKey`; keeping all
  project render graphs avoids trying to infer graph dependencies from renderer
  keys and preserves custom/example renderer-key behavior.
- Editor export also packs all project `.vshaderlib.lua` shader library
  manifests as roots, so their cooked `.vshlib/.vshweblib` outputs are retained
  in root-closure VPKs.
- Editor export also packs project Lua render pipeline assets as roots:
  `.vfeature.lua`, `.vsrp.lua`, and `res://render/**/*.lua` render pass files.
- Runtime still defaults to `entry_scene`; adding `--scene-index` or a UI build
  scenes editor is the next step for Unity-like scene selection.

Runtime release has also started conservatively:

- `AssetSystemDesc` now has `releaseZeroRefCpuAssets` and
  `zeroRefCpuAssetIdleFrames`.
- Asset requests refresh `AssetRecord::lastUsedFrame`.
- `AssetSystem::update()` releases zero-ref, idle, CPU-only skeleton and
  animation records back to `eUnloaded`.
- Existing deferred texture frees are now advanced each frame, but textures are
  not automatically released yet.

Do not enable automatic texture/mesh/gaussian VRAM release until material and
resource-table references are generation-safe. Material params currently store
bindless texture indices, so freeing and reusing a texture slot independently
can make stale materials sample the wrong texture.

Scene switching prerequisites have started:

- Async scene readiness now also requests animator skeleton and animation assets,
  in addition to mesh previews, gaussian splats, and pending material refreshes.
- `clearWorld` scene instantiation now clears the target world through a scene
  replacement helper.
- When that target is the main `IWorldService::world()`, the render service is
  notified with `resetSceneState()`, dropping stale `RenderWorld`,
  `GpuSceneDatabase`, `GpuSceneView`, override render worlds, and cached geometry
  state immediately.
- Staging/thumbnail worlds can still be cleared without resetting the main
  renderer because the helper checks the `World` pointer before notifying render.
- `DontDestroyOnLoad` is not implemented yet. The next scene-manager layer should
  preserve those entities before `World::clear()` and pin their asset closure so
  scene unload GC does not release shared resources.

## Verification

- `xmake build -y test-binary-serialization` passed.
- `xmake run -y test-binary-serialization` passed: 11 tests.
- `xmake build -y vultra-app` passed.
- `xmake build -y vultra-app` passed again after conservative CPU-only runtime
  release changes.
- `xmake build -y test-binary-serialization` passed after root-closure packing
  changes.
- `xmake run -y test-binary-serialization` passed: 11 tests.
- `xmake build -y vultra-app` passed after editor export root arguments.
- `xmake build -y vultra-app` passed after build scene list support.
- `xmake run -y test-binary-serialization` passed again: 11 tests.
- `xmake build -y vultra-app` passed after scene replacement render reset and
  animator readiness changes.
- `xmake build -y vultra-app` passed after Project Settings Build Scenes UI.
- `xmake build -y vultra-app` passed after adding all project render graphs as
  export pack roots.
- `xmake build -y vultra-app` passed after adding all project shader libraries
  as export pack roots.
- `xmake build -y vultra-app` passed after adding Lua render pipeline assets as
  export pack roots.
- `xmake build -y vultra-app` passed after final export-root cleanup.
- `xmake build -y test-binary-serialization` passed after final cleanup.
- `xmake run -y test-binary-serialization` passed after final cleanup: 11 tests.
