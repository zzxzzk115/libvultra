# Skeletal Animation Import Phase 1

## Goal

Add the first skeletal animation asset-pipeline step: Assimp model import can
emit skin-ready mesh data plus Vultra wrapper assets for ozz skeleton and
animation payloads.

## Scope

- Add `skeleton` and `animation` asset registry types.
- Add `.vskel` and `.vanim` runtime-readable wrapper formats.
- Extend `.vmesh` tail metadata with optional skin metadata while keeping old
  static meshes readable.
- Use `ozz-animation` only in `vasset-import` to build runtime skeleton and
  animation payloads from Assimp data.
- Preserve static mesh import behavior.

## Out of Scope

- ECS animation playback.
- Skin matrix upload buffers.
- Shader/runtime GPU skinning.
- Skinned ray tracing BLAS updates.

## Verification

- `xmake build -y vasset-import`
- `xmake build -y test-binary-serialization`
- `xmake run test-binary-serialization`
- `xmake build -y vultra-app`
