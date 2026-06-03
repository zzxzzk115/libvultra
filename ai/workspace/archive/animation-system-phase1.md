# Animation System Phase 1

Date: 2026-05-31

Implemented the first runtime path for imported skeletal assets:

- Added `AnimatorComponent` for scene/prefab serialization and editor inspector editing.
- Added internal `SkinPaletteComponent` for sampled joint model matrices.
- Added skeleton and animation loading through `IAssetService` / `AssetSystem`.
- Added `AnimationSystem` using ozz runtime sampling and local-to-model jobs.
- Registered `AnimationSystem` in the demo app host.
- Added GPU skin matrix staging to `GpuSceneDatabase` and render instances.
- Routed skin metadata through mesh upload, render world cooking, draw records, mesh tables, direct draw, shadow draw, visibility buffer, thin gbuffer, and meshlet shaders.
- Updated vasset model prefab import to attach an `AnimatorComponent` on the root when a skeleton and at least one animation are imported.

Verification:

- `xmake build -y vultra-app` passed.
- `xmake build -y vasset-import` passed.

Notes:

- Runtime currently supports single-animation playback per animator with play, loop, speed, and time fields.
- Skinning matrices are computed as sampled joint model matrix multiplied by the mesh inverse bind pose during render-world cooking.
- Animation controller/state-machine editor work is intentionally left for a later phase.
- Sub-animation preview still needs an animation preview scene/system integration.
