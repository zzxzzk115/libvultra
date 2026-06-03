# Unified Material MCP Smoke

Date: 2026-06-03

## Goal

Verify the unified material workflow through Runtime MCP on a fresh project:

- create a new project and scene;
- author a graph-backed `.vmat.json` and a single-shader `.vmat.json`;
- set material parameters and per-slot property blocks;
- apply the materials to mesh primitives;
- capture editor/render output and inspect component/material readback.

## Planned MCP Steps

1. Start the editor MCP server:
   `xmake run vultra-app --editor --mcp --project example.vproject --no-xr`
2. Smoke the MCP protocol:
   `initialize`, `tools/list`, `vultra.runtime.status`.
3. Create a fresh project under `build/.tmp/material-mcp-smoke-*` with
   `vultra.project.create_empty`.
4. Create a fresh scene with `vultra.scene.new`.
5. Write graph, graph-backed material, single-shader source, shader library,
   and shader-backed material assets with `vultra.assets.write`.
6. Compile the graph with `vultra.material_graph.compile`.
7. Add two primitive mesh entities with `vultra.scene.add_entity`.
8. Apply material assets and property blocks through
   `vultra.scene.update_component` on the Mesh component.
9. Read back Mesh state with `vultra.scene.get_component`.
10. Capture evidence with `vultra.editor.capture`,
    `vultra.render.capture_rgb`, or `vultra.runtime.dump_frame_textures`.

## Result

Runtime MCP smoke completed on a fresh project:

- Project: `build/.tmp/material-mcp-step-20260603-144649/Material_MCP_Step.vproject`
- Scene: `res://scenes/material_mcp_smoke.vscn`
- Graph: `res://materials/mcp_smoke_graph.vmatgraph.json`
- Graph material: `res://materials/mcp_smoke_graph.vmat.json`
- Shader: `res://shaders/mcp_smoke.frag.vshader`
- Shader material: `res://materials/mcp_smoke_shader.vmat.json`
- Captures:
  - `build/.tmp/material-mcp-step-20260603-144649/material_mcp_editor.png`
  - `build/.tmp/material-mcp-step-20260603-144649/material_mcp_rgb.png`
  - `build/.tmp/material-mcp-step-20260603-144649/material_mcp_rgb_after_shader_render.png`

Verified through MCP:

- `vultra.project.create_empty` created the fresh project, although the
  request timed out while switching context.
- `vultra.scene.new` created `material_mcp_smoke.vscn` with defaults.
- `vultra.assets.write` authored the material graph, graph material,
  single-shader source, shader library manifest, and shader material.
- `vultra.material_graph.compile` compiled the graph with no diagnostics.
- `vasset import` registered `.vmat.json` as `material` and
  `project.vshaderlib.lua` as `shader_library`.
- Two primitives were added to the new scene:
  - `MCP Graph Material Cube`
  - `MCP Shader Material Sphere`
- `vultra.scene.update_component` applied material slot overrides and
  property blocks.
- `vultra.scene.get_component` read back both material overrides and property
  values.
- `vultra.scene.save` saved `res://scenes/material_mcp_smoke.vscn`.
- `vultra.editor.capture` and `vultra.render.capture_rgb` both wrote valid PNGs.
- After shader material render support was wired, `vultra.render.capture_rgb`
  showed the graph cube in green and the shader material sphere in red. A pixel
  sample on the sphere returned `R=101 G=0 B=18`, confirming the shader-backed
  material used its reflected `tint` property instead of the default material.

Observed fixes needed for the smoke:

- `external/vasset` source text detection needed `.vmat.json` support so
  project material assets enter the live registry.
- `external/vasset` glob matching treated `**/*.vshader` as one-or-more
  directories; the smoke shader at shader-root was skipped. `**/` now matches
  zero or more directories.
- The initial test shader used push constants directly and failed WebGPU
  conversion. That was later replaced by the formal mesh material ABI plus SSBO
  material access, which is now covered by `test-material-asset` for native and
  WebGPU profiles.

## Current Limitation

This early smoke used the transitional reflected surface-property path. Current
shader-backed `.vmat.json` rendering has since moved to the formal mesh material
ABI:

- `res://shaders/materials/mcp_mesh_material.frag.vshader` includes
  `vultra/mesh_material.glsl` and writes `VultraMaterialEval`.
- `res://materials/mcp_shader_material.vmat.json` is packed into a stable SSBO
  material block, with per-slot `MaterialPropertyBlock` overrides layered on
  top.
- Runtime MCP screenshot `.vultra/mcp/render_rgb_1780507858309.png` verifies a
  shared shader material renders two mesh instances differently through
  DirectGBuffer plus DeferredLighting without mutating the shared `.vmat.json`.

Replacing DirectGBuffer or DeferredLighting with shader-owned render graph
passes is still a separate Phase 2 render graph design. Phase 1 mesh material
shaders intentionally output unlit material evaluation data and let the engine
own lighting.
