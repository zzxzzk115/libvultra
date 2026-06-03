# Unified Material Asset Lua API

Date: 2026-06-02

## Change

- Extended the existing Lua `Mesh` usertype with material slot APIs:
  - `setMaterial(slot, uri)`
  - `setMaterialFloat(slot, name, value)`
  - `setMaterialColor(slot, name, vec4)`
  - `setMaterialTexture(slot, name, uri)`
  - `clearMaterialProperty(slot, name)`
  - `clearMaterialProperties(slot)`
- The binding edits `MeshComponent::materialOverrides` and its
  `MaterialPropertyBlock` entries only; it does not touch GPU/RHI objects.
- Updated `doc/lua_scripting.md` and `ai/knowledge/lua-scripting.md`.

## Verification

- `xmake build -y vultra-app` passed after adding the binding.
- Runtime MCP smoke with direct launch:
  `xmake run vultra-app --editor --mcp --project example.vproject --no-xr`
- Created a temporary primitive, updated its mesh `materialOverrides`, and read
  the values back through `vultra.scene.get_component`.
- Runtime MCP direct Lua script smoke with direct launch:
  `xmake run vultra-app --editor --mcp --project build/.tmp/material-shader-mcp-project/material-shader-mcp.vproject --no-xr --render-mode=offscreen`
- Wrote `res://scripts/lua_material_smoke.lua`, attached it to a primitive
  cube, started playback, stepped five frames, and read back the MeshComponent.
  `OnCreate` successfully called `setMaterial`, `setMaterialFloat`,
  `setMaterialColor`, `setMaterialTexture`, `clearMaterialProperty`, and
  `clearMaterialProperties`.
- Mesh readback during playback showed slot 0 referencing
  `res://materials/mcp_shader_material.vmat.json` with `roughness`, `tint`, and
  `albedoTex` property block entries. The temporary property cleared by
  `clearMaterialProperty` was absent, and clearing slot 1 left no empty slot
  override.
- Reading the shared material asset afterwards confirmed the Lua runtime writes
  did not mutate `res://materials/mcp_shader_material.vmat.json`.
- Closed the editor with `vultra.editor.quit` and confirmed no `xmake` or
  `vultra` processes remained.

## Follow-Up

- Continue Phase 9 cleanup by routing material operations through a small C++
  material/mesh service if the runtime grows more material mutation rules.
