# RenderGraph Editor Pass Catalog Sync

## Summary

- Fixed the RenderGraph editor catalog missing `GeneralGaussianSplatComposite`.
- Runtime declarative renderer already registered this builtin pass, so graphs rendered correctly but editor validation reported:
  - `Unknown pass type 'GeneralGaussianSplatComposite' on pass 'GeneralGaussianSplatComposite'.`

## Verification

- Scanned builtin/project `.vrg.json` pass types under `builtin/render` and `resources/render`.
- Compared runtime builtin pass registrations with editor builtin pass registrations for the Gaussian path.
- `xmake build -y vultra-app`
  - Passed.

## 2026-05-29 Follow-up

- Removed the editor-only builtin pass catalog duplication.
- Moved editor builtin pass registration into the engine declarative renderer layer instead of keeping it in `render_graph_window.cpp`.
- Project Lua render graph passes can now declare `input`/`output` or `inputs`/`outputs`; runtime and editor registration both preserve those slots.
- Verification: `xmake build -y vultra-app` passed.

## 2026-05-29 Stale Link Repair

- Added editor-side repair for stale input refs after pass deletion/recreation.
- If an input references a missing node such as `ShadowMap.shadowData`, but the graph has a unique pass of type `ShadowMap` with that output slot, the ref is migrated to the replacement pass id.
- If no unique replacement exists, the dangling ref is cleared so validation reports a disconnected input instead of a missing node.
- Verification: `xmake build -y vultra-app` passed.

## 2026-05-29 Render Graph Data Migration

- Migrated builtin and sample project `.vrg.json` files to remove redundant default pass `outputs` entries.
- Kept non-default output selectors, such as XR explicit eye backbuffer outputs.
- Updated Project Launcher default render graph templates to emit the migrated format for new projects.
- Changed the editor so `ensureSlots` no longer reintroduces default output refs, and save/export strips any remaining default output refs.
- Runtime now materializes registry default output refs in memory before calling `vrendergraph::RenderGraph::build()`, so migrated files can omit default outputs without missing resources such as `DirectGBuffer.depth`.
- Editor link creation now applies topological pass ordering, and runtime sorts active graphs before build, so newly appended passes such as a recreated `ShadowMap` execute before downstream consumers.
- Regenerated embedded builtin render graph header via `xmake build -y vultra-app`.
- Verification: `xmake build -y vultra-app` passed.

## 2026-05-29 default_xr Removal

- Removed the redundant `resources/render/default_xr.vrg.json` graph.
- Removed its `.vimport`, imported render graph cache, and asset registry/database rows.
- Verified no runtime/source/resource references to `default_xr` remain.
- Verification: `xmake build -y vultra-app` passed.
