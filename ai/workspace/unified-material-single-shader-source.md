# Unified Material Single Shader Source

Date: 2026-06-03

## Scope

- Added a first editor-facing single shader material template under
  `Material/Single Shader`.
- Added conversion from `vshadersystem::MaterialDescription` params/textures to
  Vultra material source schemas, including defaults and UI ranges for supported
  scalar/vector params.
- Added coverage using a real inline `.vshader` compiled through
  `vshadersystem::build_single_shader()`. The test verifies that generated
  `Texture2D` index params such as `albedoTex_index` become user-facing
  Texture2D URI properties named `albedoTex`.
- Updated the material Inspector to load the selected fragment shader from the
  project or builtin shader library and expose its material description as
  editable `.vmat.json` properties.
- Updated runtime material resolution for `source.kind = "shader"` assets to
  load shader reflection, pack `.vmat.json` values plus
  `MaterialPropertyBlock` overrides into the reflected material parameter block,
  and render through the DirectGBuffer mesh material ABI.
- Added the first formal shader material contract through
  `builtin/shaders/include/vultra/mesh_material.glsl`. Handwritten mesh
  material shaders can read engine constants/varyings, call
  `VULTRA_MATERIAL()` when they declare `[properties]`, sample bindless
  textures through the ABI helper, and write `VultraMaterialEval` for the
  existing deferred lighting path.

## Verification

- `xmake build -y test-material-asset`
- `xmake run test-material-asset`
- `xmake build -y vultra-app`
- `xmake run test-material-asset` now covers real mesh material ABI shaders
  with reflected properties plus a constant-only shader without `[properties]`
  for native and WebGPU profiles.
- `xmake run vultra-app --editor --mcp --project build/.tmp/material-mcp-step-20260603-144649/Material_MCP_Step.vproject --no-xr --render-mode=offscreen`
- Runtime MCP `vultra.render.capture_rgb` wrote
  `build/.tmp/material-mcp-step-20260603-144649/material_mcp_rgb_after_shader_render.png`.
  Pixel sample on the shader sphere returned `R=101 G=0 B=18`, confirming the
  reflected `tint` rendered red instead of the default material.

## Handoff

Single shader `.vmat.json` assets are now createable, inspectable, and rendered
as real handwritten mesh material shaders. The PBR-name adapter is only a
compatibility/diagnostic path when the ABI shader variant is unavailable; the
main path is reflected parameter packing plus the DirectGBuffer ABI fragment
pipeline.
