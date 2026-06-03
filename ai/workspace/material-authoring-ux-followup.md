# Material Authoring UX Follow-Up

Date: 2026-06-03

## Scope

- Added a Mesh Inspector `Fork Builtin` action for material slot overrides.
  When a slot is empty or references `builtin://materials/default.vmat.json`,
  the button creates an editable project material under
  `res://materials/forked_builtin_material_slot_<slot>.vmat.json`, imports it,
  and assigns the slot to the new `.vmat.json`.
- Added search to the Material Graph add-node popup. Empty search preserves the
  existing categorized menu; non-empty search shows a flat filtered list matching
  node display name, `typeId`, category, or subcategory.
- Reduced raw string entry in the Material Inspector:
  - builtin material source uses a builtin selector instead of a free-form id
    for normal `builtin/pbr` assets;
  - graph source uses the material graph URI selector;
  - Texture2D material properties and MaterialPropertyBlock texture overrides
    use the texture picker/preview selector instead of manual URI text fields;
  - material properties render in a two-column property table so labels no
    longer sit after compressed controls.
- Added a first-pass `.vmatnode.json` Inspector editor. Project material graph
  node descriptors can now edit `typeId`, display name, input/output pins,
  default params, GLSL output expressions, diagnostics, save, and reload from
  Inspector instead of requiring raw JSON/code-editor edits.
- Deferred shader-driven render graph pass replacement and broader backend
  smoke coverage per user direction. Commit/push/release remains the final step.

## Verification

- `xmake build -y vultra-app`

## Notes

- The fork action intentionally writes a project `.vmat.json`; it never edits
  builtin material definitions.
- Existing MCP material smokes already cover project builtin PBR material
  creation, assignment, render capture, and scene save:
  `.vultra/mcp/render_rgb_1780512088131.png` and
  `res://scenes/mcp_builtin_material_edit_smoke.vscn`.
