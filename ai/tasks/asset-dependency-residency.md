# Asset Dependency Graph and Runtime Residency

## Goal

Add first-class asset dependency tracking and runtime residency management so
Vultra can detect hidden dependency cycles, diagnose and repair missing asset
references, and automatically release unused CPU/GPU asset memory.

## Current State

- `AssetHandle` increments/decrements `AssetRecord::refCount`, but this is only
  a transient handle count.
- Scene/render code commonly stores UUIDs or URIs and requests temporary handles
  during cooking, so `refCount == 0` does not mean the asset is no longer
  referenced by the active world.
- `AssetSystem::update()` has a GC hook but no eviction policy.
- `GpuResourcePool` can defer-free bindless texture slots, but mesh, material,
  and gaussian splat pools are append-oriented and do not yet expose safe
  per-asset reclamation.
- `VAssetRegistry` is the right place to persist imported asset identity, but it
  does not yet persist dependency edges or dependency diagnostics.

## In Scope

1. Persist an asset dependency graph.
   - Add dependency edge records to imported registry data using UUID as the
     stable key and URI/path as repair context.
   - Track edge kind, such as source include, runtime payload, material texture,
     skeleton, animation, script, render graph feature, shader library, or scene
     component reference.
   - Preserve additive format compatibility for existing registries and VPKs.

2. Build and validate the graph during import/registry reload.
   - Extract dependencies from importer outputs and supported project-facing
     text formats.
   - Report missing dependencies with enough context for editor repair.
   - Detect cycles and classify allowed versus invalid cycles.
   - Use the graph for packaging order and completeness checks.

3. Add repair-friendly dependency resolution.
   - Resolve references by UUID first, then URI/path fallback.
   - Keep unresolved edges in diagnostics instead of silently dropping them.
   - Expose lookup helpers for "who depends on this asset" and "what is missing".

4. Add runtime residency pins.
   - Introduce explicit runtime pins/leases separate from temporary
     `AssetHandle` copies.
   - Let worlds/render cooking/animation/material systems declare which UUIDs
     are live for the current scene or frame window.
   - Keep transient handles useful for readiness and data access, but do not use
     them as the sole ownership signal.

5. Add staged eviction.
   - Evict only when an asset is unpinned, has no outstanding handles, has no
     pending CPU load/upload work, and has been idle for a configurable grace
     period.
   - Start with CPU-only assets and textures, since texture slots already have a
     deferred-free path.
   - Add mesh/material/gaussian reclamation only after the resource pools can
     safely reclaim or compact append-only storage.

6. Surface diagnostics.
   - Add editor/runtime diagnostics for resident bytes, pins, dependents,
     missing edges, and cycle reports.
   - Update runtime MCP asset tools when the service surface exists.

## Out of Scope

- Rewriting all asset formats in one pass.
- Compacting global mesh/gaussian buffers before resource-pool free lists or
  generation-safe handles exist.
- Treating temporary `AssetHandle::refCount` as scene ownership.
- Lua-facing changes unless gameplay authors need direct dependency or residency
  inspection APIs.

## Proposed Phases

### Phase 1: Registry Graph Foundation

- Add dependency edge types and serialization in `libvasset`.
- Populate edges for imported mesh/material/texture relationships first.
- Add graph validation helpers and tests for missing edges and simple cycles.
- Keep old registries readable with empty dependency lists.

### Phase 2: Runtime Pin Surface

- Add `IAssetService` pin/lease APIs.
- Track live pins per world/render/animation owner.
- Mark `AssetRecord::lastUsedFrame` from asset requests and pins.
- Expose residency debug stats.

### Phase 3: Safe Eviction

- Evict CPU-only skeleton/animation data after idle grace.
- Evict textures by clearing cache records, bindless map entries, and scheduling
  texture-slot deferred free.
- Leave mesh/material/gaussian GPU data resident until pool reclamation exists.

### Phase 4: Wider Graph Coverage

- Extract dependencies from scenes, scripts, material graphs, shader libraries,
  render graphs, and package manifests.
- Add editor repair affordances driven by UUID-first dependency diagnostics.
- Use graph closure for VPK/package export completeness checks.

### Phase 5: Full GPU Pool Reclamation

- Add generation-safe resource handles or free-list/compaction support for mesh,
  material, and gaussian pools.
- Evict all runtime asset classes without stale render-world references.

## Gameplay API Parity Decision

No immediate Lua binding is required for Phase 1 or Phase 2. This is engine
infrastructure and editor/runtime memory behavior. Revisit Lua bindings only if
game scripts need stable APIs for preloading, pinning, dependency inspection, or
manual unloading.

## Verification Plan

- Add unit tests for dependency graph serialization, missing references, and
  cycle detection.
- Build `vultra-app`.
- Run an editor/runtime smoke test with scene load, asset import/reimport, and
  repeated scene switches.
- Use profiler/runtime memory stats to verify texture and CPU asset residency
  drops after the idle grace period.
- Verify VPK mode uses registry/VFS data and does not physically scan `res://`.

## Notes

The key design rule is that `AssetHandle` remains a short-lived access handle.
Scene ownership must be represented by explicit pins or by a live-world
reference scan, otherwise automatic unloading will be unstable.
