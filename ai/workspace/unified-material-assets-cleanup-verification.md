# Unified Material Assets Cleanup Verification

Date: 2026-06-03

## Cleanup

- Removed stale material-owned shader chain/pass-array direction from code docs.
- Renamed the durable spec/task to:
  - `ai/specs/unified-material-assets.md`
  - `ai/tasks/unified-material-assets.md`
- Deleted stale workspace notes for the abandoned shader pass/resource-layer
  direction.
- Current material source model:
  - `builtin`
  - `graph`
  - single `shader`
- Material Graph custom nodes are the planned reusable extension point.

## Static Verification

- `rg "shader-bound-materials|Shader-bound|shader-bound|shaderPasses|MaterialShaderPass|eShaderBound|shaderBoundMaterials|source\\.passes|pass array|pass arrays|pass group|pass groups|mini render graph|minimal render graph|multiple shader|multi-post|shader chain" source/vultra source/vultra_app ai doc resources`
  - No code or active docs retain the abandoned symbols/model.
- `git diff --check`
  - Passed; only existing CRLF warnings for generated builtin headers.
- `xmake build -y vultra-app`
  - Passed.

## Runtime MCP Verification

Started editor through direct xmake run:

```text
xmake run vultra-app --editor --mcp --mcp-port 8867 --project example.vproject --no-xr
```

MCP flow:

- `initialize`: ok.
- `vultra.runtime.status`: editor mode on `example.vproject`.
- `vultra.editor.back_to_launcher`: ok, mode `launcher`.
- `vultra.project.create_empty`: created minimal temp project at
  `.vultra/mcp_projects/material_sources_20260603_8867`.

Created three material assets via `vultra.assets.write`:

- `res://materials/smoke_builtin_blue.vmat.json`
  - `source.kind = "builtin"`
- `res://materials/smoke_graph_red.vmat.json`
  - `source.kind = "graph"`
- `res://materials/smoke_shader_single.vmat.json`
  - `source.kind = "shader"`
  - `shaderLibrary = "project"`
  - `id = "fullscreen/pixelate.frag"`

Created three cube entities and assigned each material through
`MeshComponent.materialOverrides[].material`. Readback through
`vultra.scene.get_component` confirmed all three use the unified `material`
field.

Saved scene:

- `res://scenes/material_sources_smoke.vscn`

Captured visual smoke:

- `.vultra/mcp/material_sources_smoke_rgb.png`

Observed in screenshot:

- Builtin material cube rendered blue.
- Graph material cube rendered with graph-backed color.
- Single shader material asset was accepted, referenced, saved, and rendered
  through the current safe path. Full shader reflection/render binding remains
  a later phase.

Shutdown:

- `vultra.runtime.playback stop`: ok.
- `vultra.editor.quit`: ok.
- No xmake/vultra process remained.
