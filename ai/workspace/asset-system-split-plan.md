# AssetSystem architecture and split plan

> **STATUS (2026-06-17): COMPLETE.** Steps 1-3 landed earlier (material_params.hpp,
> mesh_vertex_packing.cpp, builtin_assets_io.cpp). Steps 4-6 landed in Round 9
> (asset_gpu_upload.cpp, asset_material_upload.cpp + imported_material_path.hpp,
> asset_registry.cpp). `asset_system.cpp` is now 1219 lines (was 2595) and holds only
> lifecycle + residency + async CPU loading + text/binary I/O. See
> codex-debt-cleanup-execution.md Round 9.


`source/vultra/src/function/asset/asset_system.cpp` is ~3000 lines covering registry,
loading, residency, GPU upload, and material emission in one translation unit. This
document maps those responsibilities and a build-incremental split plan. Execute in
small steps, each followed by `xmake build -y vultra-app`.

## Current responsibilities (file order)

1. **Anonymous-namespace helpers** (~lines 62–753)
   - CPU-cache usage/eviction templates, builtin-URI classification.
   - Builtin asset path mapping + (platform-specific) builtin texture byte loading,
     `makeTextureFromBytes`.
   - Material asset (de)serialization: `MaterialParamsPBRMR`, JSON value helpers,
     alpha-mode/roughness conversions, imported-material path calc.
   - Vertex packing: `buildVertexAttributes`, `packVertices`, f16 packers, quaternion
     sanitization.
2. **Lifecycle**: `~AssetSystem`, `onInit`, `onShutdown`.
3. **Config / registry**: `memoryStats`, `configure` (~200 lines: mount, registry,
   import scan, resolver).
4. **Per-frame / residency**: `update`, `releaseZeroRefCpuAssets`, `enqueueUploadOnce`,
   `collectFinishedCpuLoadTasks`, `waitForCpuLoadTasks`.
5. **Async CPU loading**: `startMeshCpuLoadAsync`, `startTextureCpuLoadAsync`,
   `startGaussianSplatCpuLoadAsync`.
6. **URI/UUID resolution + bindless texture index**.
7. **Material dependency / refresh**: `materialTextureDependenciesReady`,
   `refreshGpuMaterialParams`, `refreshPendingMaterialParams`.
8. **GPU upload**: `uploadTexture`, `createAndAppendGpuMaterial[FromAsset]`,
   `emitImportedMaterialAssets`, `uploadMesh` (~170 lines), `uploadGaussianSplat`.
9. **Import/registry management**: `reimportAsset`, `reloadRegistry`, URI resolve.
10. **Text/binary asset I/O + overrides**.

## Proposed split (by responsibility)

| New unit | Contents | Notes |
|---|---|---|
| `material/material_params.hpp` | `MaterialParamsPBRMR` + alpha-mode helpers | Shared with the render system (see render-system.md); extract once, use everywhere. |
| `asset/builtin_assets_io.cpp` | builtin URI classification, path mapping, builtin texture bytes, `makeTextureFromBytes` | Contains the platform-specific (`_WIN32`) resource loading; isolating it shrinks the `#ifdef` surface. |
| `asset/mesh_vertex_packing.cpp` | `buildVertexAttributes`, `packVertices`, f16 packers, quaternion sanitize | Pure CPU transform; independently testable (good first unit test target). |
| `asset/asset_material_upload.cpp` | `createAndAppendGpuMaterial[FromAsset]`, `refreshGpuMaterialParams`, `refreshPendingMaterialParams`, `emitImportedMaterialAssets` | Depends on the GPU resource service + the material-refresh queue member. |
| `asset/asset_gpu_upload.cpp` | `uploadTexture`, `uploadMesh`, `uploadGaussianSplat` | Tightly coupled to `m_GpuResourceService` / `m_RenderDevice`. |
| `asset/asset_registry.cpp` | `configure`, `reloadRegistry`, `reimportAsset`, URI/UUID resolve | Owns mount/registry/import. |
| `asset_system.cpp` (slimmed) | lifecycle, `update`, residency, async CPU loading, text/binary I/O | Central coordination hub. |

## Barriers to handle

- Most shared state is **member variables** (mutex-guarded queues, caches), so functions
  move cleanly with their data — but keep tightly-locked groups together
  (`m_PendingMaterialRefreshes`, `m_UploadQueue`, `m_CpuLoadTasks`,
  `m_TextAssetOverrides`).
- The duplicated `MaterialParamsPBRMR` struct must be extracted to a shared header first.
- `VULTRA_HAS_VASSET_IMPORT`-conditional code (`emitImportedMaterialAssets`,
  `reimportAsset`) should stay together so the guard stays in one place.

## Execution order (each step builds)

1. Extract `material/material_params.hpp` (shared with render system). Build.
2. Move vertex packing to its own TU + add a small CPU unit test for `packVertices`. Build + test.
3. Move builtin asset I/O. Build.
4. Move GPU upload. Build.
5. Move material upload/refresh. Build.
6. Move registry/import. Build.
7. Confirm `asset_system.cpp` is lifecycle + residency + I/O only. Build + MCP asset smoke.
