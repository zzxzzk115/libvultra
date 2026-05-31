# VMesh Local Bounds

Date: 2026-05-31

## Change

- Added `VMesh::hasLocalBounds`, `localBoundsMin`, and `localBoundsMax`.
- Serialized local AABB in the existing `VMESH_META1` tail metadata using a new
  metadata flag.
- Kept old `.vmesh` assets readable by computing bounds from positions when the
  metadata is missing.
- Updated mesh import output schema to `vmesh:4` so imported meshes are
  refreshed with bounds.
- Updated editor preview/focus/physics-shape bounds paths to use mesh local AABB
  before falling back to scanning positions.
- Fixed a SunTemple FBX import crash by decoding Assimp material property raw
  payloads directly instead of probing incompatible `Get<T>()` overloads.
- Shortened generated imported mesh keys with a stable hash suffix to avoid
  Windows path length failures on deeply named FBX node meshes.
- Baked SunTemple-style BC5/DirectX normal DDS assets into generic RGBA normal
  textures at import time.
- Treated SunTemple legacy `Specular` textures as packed ORM using
  R=occlusion, G=roughness, B=metalness, while avoiding factor double-multiply.
- Classified packed BaseColor opacity as alpha mask only after shallow alpha
  sampling finds actual non-opaque pixels; all-1.0 alpha remains opaque.

## Verification

- `xmake build -y test-binary-serialization`
- `xmake run -y test-binary-serialization`
- `xmake build -y test-importers`
- `xmake run -y test-importers`
- `xmake build -y vultra-app`
- `vultra.exe asset import build/test2/resources/models/SunTemple --reimport`
- `vultra.exe asset import build/test2/resources --reimport`
- `git diff --check`
- `git -C external/vasset diff --check`
