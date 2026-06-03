# Mesh Material SSBO Baseline Verification

Date: 2026-06-03

Summary:
- Mesh material shader access was moved off the BDA default path.
- `vshadersystem` v0.10.2 adds `MaterialAccessInjection::postMaterialDecl`, allowing Vultra to inject a descriptor-backed `vshader_LoadMaterial()` after the generated `Material` struct.
- Vultra mesh material shaders now use a stable `layout(set = 1, binding = 1, std430) readonly buffer VultraMaterialBlock` loader for both Vulkan and WebGPU profile compilation.

Verification:
- `vshadersystem` v0.10.2 build/release actions completed successfully: Android, Linux, macOS, WASM, Windows, Release Prebuilt.
- `xmake-repo` updated and pushed with v0.10.2 source/git/prebuilt hashes.
- `xmake show -l packages` in libvultra resolves `vshadersystem` and `vshadersystem~host` to v0.10.2.
- `xmake run test-material-asset` passed, including real mesh material vshader compilation for native and WebGPU profiles.
- `xmake run test-material-asset` also covers a mesh material shader with no `[properties]`; it compiles for native and WebGPU profiles without injected `Material` access.
- `xmake build -y vultra-app` passed.
- Runtime MCP smoke loaded `res://scenes/material_shader_smoke.vscn`, captured RGB output, and showed shader material cube/sphere rendering through DirectGBuffer and deferred lighting.

Follow-up update:
- Shader-backed `.vmat.json` runtime packing now emits once-per-material warnings for missing shader variants, unknown properties, non-string texture values, and unsupported reflected property types.
- Runtime MCP readback verified mesh slot references:
  - `Shader Cube A` loads slot 0 with `material = res://materials/mcp_shader_material.vmat.json` and no property block.
  - `Shader Sphere B` loads the same `material` URI plus `tint` and `roughness` property block entries.
  - A temporary legacy scene with `MeshComponent/materialOverrides = "0=res://materials/legacy_test.vmatgraph.json"` loads into `materialGraph` while leaving `material` empty.
- Runtime MCP AssetSystem import smoke copied DamagedHelmet into the test project and imported `res://models/DamagedHelmet/DamagedHelmet.gltf`. The editor generated:
  `res://materials/imported/DamagedHelmet_0_node_damagedHelmet_-6514_mesh_0_mesh_helmet_LP_13930damagedHelmet/0_Material_MR.vmat.json`.
  Asset service readback confirmed builtin PBR source plus imported PBR/texture properties.
- Runtime MCP AssetSystem import smoke copied CornellBox into the test project and imported `res://models/CornellBox/CornellBox-Original.obj`.
  The editor generated 8 imported `.vmat.json` assets across the OBJ's meshes/material slots, including distinct red `leftWall` and green `rightWall` builtin PBR materials.
- Delete-and-reimport smoke deleted the generated CornellBox `0_leftWall.vmat.json`, reimported the OBJ through Runtime MCP, and confirmed the file was recreated with the expected red builtin PBR material properties.
- Old imported output fallback smoke deleted the generated DamagedHelmet `0_Material_MR.vmat.json`, instantiated mesh UUID `715f93b4fa201d106e1472855cad10d1`, stepped runtime, and captured `.vultra/mcp/render_rgb_1780509661057.png`. The mesh rendered from embedded imported material data without requiring generated `.vmat.json` output.

Runtime capture:
- `.vultra/mcp/render_rgb_1780507858309.png`
- `.vultra/mcp/render_rgb_1780509661057.png`
