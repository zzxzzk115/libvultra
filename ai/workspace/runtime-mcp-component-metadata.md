# Runtime MCP Component Metadata

Date: 2026-06-02

## Change

- Added `vultra.scene.component_metadata` to expose the component field contract
  used by MCP scene component commands.
- Added `vultra.scene.get_component` to read an entity component as
  `scene.update_component`-compatible JSON.
- Covered the existing editable component kinds, including nested
  `MeshComponent.materialOverrides` and per-slot material property block
  entries.
- Kept existing `scene.update_component` behavior compatible and added camera
  `cullingMask` update parity with the returned camera component data.

## Verification

- `xmake build -y vultra-app` passed.
- Started Runtime MCP with:
  `xmake run vultra-app --editor --mcp --project example.vproject --no-xr`
- Called `initialize` and `tools/list`; confirmed these tools are registered:
  `vultra.scene.component_metadata` and `vultra.scene.get_component`.
- Called `vultra.scene.component_metadata` for `mesh`; confirmed the returned
  schema includes `materialOverrides` with `slot`, `.vmat.json` material URI,
  legacy `.vmatgraph.json` graph URI, and `float/color/texture2D` property
  entries.
- Created a temporary primitive entity, updated its mesh `materialOverrides`,
  then read it back through `vultra.scene.get_component`; the material URI and
  property block values were returned correctly.
- Called `vultra.editor.quit` and confirmed no `xmake` or `vultra` processes
  remained.

## Follow-Up

- Use this metadata as the shared contract for future MCP/editor automation
  instead of adding one-off parsing patches for each new component field.
- Next cleanup step is to make `scene.update_component` consume the same field
  descriptors where practical.
