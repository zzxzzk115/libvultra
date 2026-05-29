# Async Asset Loading Phase 1

## Goal

Start addressing GitHub issue #8 by making runtime mesh, texture, and gaussian
splat asset requests non-blocking on the render/world cooking path.

## In Scope

- Add `IAssetService` async request APIs.
- Load CPU asset bytes and deserialize on `vtask` workers.
- Keep GPU upload on `AssetSystem::update()` main/render thread.
- Change RenderWorld cooking to request async assets and skip not-ready assets.
- Avoid synchronous mesh loads during scene instantiation default-transform setup.
- Add an engine JobSystem wrapper over `vtask` for UI-visible background jobs.
- Surface current job progress in the editor bottom task bar.
- Retry runtime asset read/decode failures up to three attempts.
- Move texture thumbnail `stb_image` cooking to background jobs with retry.
- Add import-time guardrails for large texture size and meshoptimizer passes.

## Out of Scope

- Full RenderWorld double buffering redesign.
- Asset reference-count garbage collection.
- Async scene text/prefab document loading.
- Migrating every existing editor task to the JobSystem in one pass.

## Relevant Context

- `ai/specs/ai-harness.md`
- GitHub issue #8: Next-Generation World & Asset Management
- Existing `AssetHandle`, `AssetRecord`, `AssetCache`, and upload queue classes.

## Verification Plan

- Build `vultra-app` with xmake.
- Inspect remaining synchronous runtime hot-path calls for future phases.

## Status

Implemented and build-verified with `xmake build -y vultra-app`.
