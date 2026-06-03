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
- Closed the editor with `vultra.editor.quit` and confirmed no `xmake` or
  `vultra` processes remained.

## Follow-Up

- Add a dedicated Lua script smoke once the test harness has a direct script
  execution path for small runtime snippets.
- Continue Phase 9 cleanup by routing material operations through a small C++
  material/mesh service if the runtime grows more material mutation rules.
