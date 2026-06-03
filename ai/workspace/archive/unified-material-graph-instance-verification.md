# Unified Material Graph Instance Verification

Date: 2026-06-03

## Issue

Runtime smoke exposed a real graph-backed material risk: two `.vmat.json`
assets can reference the same `.vmatgraph.json`, but they must still produce
independent material parameters. At the same time, graph shader identity should
continue to identify the source graph, not the material instance.

## Fixes

- Split graph material identity in `render_system.cpp`:
  - GPU material table lookup/cache uses the material instance key.
  - `MaterialGraphSurfaceParams.graphId` uses the source graph URI id.
- Material graph JSON now uses `typeId` as the canonical serialized node type
  field.
  Saving still emits the existing `type` field.
- Added a `test-material-graph` case for canonical `typeId` graph JSON.
- Kept the defensive MCP `assets.write` argument validation added while
  debugging. The original MCP failure was caused by a PowerShell helper using
  `$args`, which is a reserved automatic variable, and sending array arguments.

## Verification

- `xmake build -y test-material-graph`
  - Passed.
- `xmake run test-material-graph`
  - Passed with `material_graph tests passed`.
- `xmake run test-material-asset`
  - Passed with `material_asset tests passed`.
- `xmake build -y vultra-app`
  - Passed.
- Runtime MCP visual smoke:
  - Started editor with:
    `xmake run vultra-app --editor --mcp --mcp-port 8871 --project example.vproject --no-xr`
  - Wrote one shared graph asset and two `.vmat.json` assets pointing at it.
  - Assigned the two materials to two cube entities.
  - Captured:
    `.vultra/mcp/mcp_shared_graph_materials_rgb_after_fix.png`
  - Screenshot showed the shared graph material instances rendering as separate
    red and blue cubes.

## Notes

- Temporary `resources/materials/mcp_*` assets created by the smoke test were
  removed after capture.
- The scene was not saved while play mode was active, so the smoke scene did not
  become project content.
- The PowerShell `$args` MCP helper pitfall is now documented in:
  - `ai/README.md`
  - `ai/specs/runtime-mcp.md`
  - `ai/knowledge/ai-runtime-rpc.md`

## Follow-Up Verification

- `rg '\$args|toolArgs|PowerShell MCP|tools/call\.params\.arguments' ai\README.md ai\knowledge\ai-runtime-rpc.md ai\specs\runtime-mcp.md -n`
  - Confirmed the warning is present in the quickstart, runtime MCP spec, and
    stable knowledge.
- `rg 'shaderPasses|MaterialShaderPass|eShaderBound|shaderBoundMaterials|source\.passes|pass group|pass groups|mini render graph|minimal render graph|multiple shader|multiple shaders|multi-post|shader chain' source\vultra source\vultra_app ai\specs ai\tasks ai\knowledge tests -n`
  - No active hits.
- `xmake run test-material-graph`
  - Passed.
- `xmake run test-material-asset`
  - Passed.
- `xmake build -y vultra-app`
  - Passed.
