# Skeletal Animation Import Phase 1

## Summary

- Added `.vskel` and `.vanim` wrapper assets that store ozz binary payloads
  without making the runtime `vasset` target depend on ozz.
- Added `skeleton` and `animation` registry asset types.
- Extended `VMesh` with optional skin metadata: skeleton reference, joint names,
  joint parents, and inverse bind poses.
- Changed `VJointIndices` to integer semantics and uploads location 6 as
  `rhi::VertexAttribute::Type::eInt4`.
- `vasset-import` now links `ozz-animation` and converts Assimp bones and
  animations into `.vskel` / `.vanim` outputs during model prefab import.

## Verification

- `xmake build -y vasset-import` passed.
- `xmake build -y test-binary-serialization` passed.
- `xmake run test-binary-serialization` passed: 8 tests.
- `xmake build -y vultra-app` passed.

## Handoff

- Runtime playback and GPU skinning are intentionally not implemented yet.
- Follow-up should add ECS animation components, ozz runtime loading from the
  wrapper payloads, skin matrix upload, and shader-side skinning.
- The importer stores `VMesh::skeletonPath` as the source-style subasset path
  (`source#skeleton`) so future component/prefab wiring can resolve it through
  the registry path model.

## Follow-up Fix

- Mixamo FBX imports exposed that choosing the first deform bone whose parent is
  not a deform bone can miss sibling bone subtrees. Skeleton root selection now
  walks down from the scene root to the smallest single child subtree containing
  all referenced bones, and missing mesh bones log their concrete name before
  returning `invalid_format`.
- Mixamo texture imports also exposed that Assimp texture strings must be kept
  intact for `aiScene::GetEmbeddedTexture()` before resolving them relative to
  the model path. Material texture import now tries embedded textures first and
  falls back to filesystem paths only when no embedded payload is found.
- Mixamo FBX exports are authored with FBX unit metadata. The importer enables
  Assimp's FBX unit conversion and `aiProcess_GlobalScale`, but leaves
  `AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY` at its default so the FBX app/unit scale is
  applied exactly once. During this development phase model/mesh importer
  versions are kept at `model_prefab:1` and `mesh:1`; verification should clear
  imported assets and thumbnails instead of bumping versions for every trial.
- Runtime texture UUID resolution now treats textures like meshes and prefers
  the imported cooked path when present. This is required for embedded model
  texture subassets because paths such as `source.fbx#texture/...` are registry
  source identifiers, not physical files or `.vimport` remap targets.
- Import cache checks no longer treat missing import database records as a cache
  hit. This prevents old cooked model/mesh/texture outputs from being silently
  blessed after importer version bumps such as the FBX unit conversion change.
- Rendered asset thumbnail cache version was bumped so old thumbnails generated
  before embedded texture resolution fixes are regenerated.
- Content Browser model expansion now lists all registry subassets whose
  `sourcePath` belongs to the model (`source#...`), including meshes, embedded
  textures, skeletons, and animations. Non-mesh subassets use type icons and
  labels instead of mesh thumbnails.
- Embedded texture subassets now use runtime texture preview in the Content
  Browser and Inspector instead of source-file thumbnail cooking. Skeleton and
  animation subassets show basic Inspector metadata; animation playback preview
  remains blocked on the runtime animation system.
