# Unified Material Graph Custom Node Descriptors

Date: 2026-06-03

## Scope

Implemented the first custom Material Graph node extension point as descriptor,
discovery, and GLSL expression compilation. This deliberately does not add a
mini render graph or multi-shader material model.

## Format

`.vmatnode.json` describes a reusable Material Graph node:

- `type = "MaterialGraphNode"`
- `version = 1`
- `typeId`
- `displayName`
- `inputs`
- `outputs`
- `defaultParams`
- optional `implementation.outputs` GLSL expressions

The `vultra.*` namespace is reserved for engine builtin nodes. Project/custom
nodes should use a separate namespace such as `project.*`.

## Code Changes

- Added `NodeDescriptorParseResult`, `nodeDescriptorFromJson()`, and
  `loadNodeDescriptorFromText()` to the material graph node registry.
- Added parser validation for root type, version, type id, reserved namespace,
  input/output pin schemas, duplicate pin names, default params, and GLSL
  output expression value types.
- Added `.vmatnode.json` as a source text asset.
- Added Content Browser creator: `Material Graph/Custom Node`.
- Added a puzzle icon for `.vmatnode.json` source assets.
- Material Graph Window now scans project assets for `.vmatnode.json` whenever
  asset generation changes, registers valid custom node descriptors, and
  refreshes existing graph node ports from the updated registry.
- Material Graph compiler expands custom node GLSL output expressions using
  `{{input:name}}` and `{{param:name}}` placeholders.
- Runtime MCP exposes `vultra.material_graph.list_nodes` to scan the current
  project asset root for `.vmatnode.json` descriptors and report parsed custom
  nodes plus diagnostics.
- Runtime MCP exposes `vultra.material_graph.compile` to compile a project
  `.vmatgraph.json` with project `.vmatnode.json` descriptors loaded into the
  compiler registry.
- Material graph JSON now uses `typeId` as the canonical serialized node type
  field and `nodeId` as the canonical serialized link endpoint field, matching
  generated/MCP graph JSON.
- Default material graph source assets and the new-project minimal template now
  write canonical `typeId`/`nodeId` graph JSON.

## Verification

- `xmake run test-material-graph`
  - Passed. Covers valid custom descriptor parsing, registration, validation
    against a graph, reserved `vultra.*` rejection, invalid implementation
    output rejection, and GLSL snippet compilation.
- `xmake build -y vultra-app`
  - Passed.
- Runtime MCP smoke:
  - Launched editor with `xmake run vultra-app --editor --mcp --project
    C:\tmp\vultra_material_node_mcp_test\MaterialNodeMcpTest.vproject --no-xr
    --mcp-port 8851`.
  - Wrote two project node descriptors through `vultra.assets.write`:
    `project.mcp_tint_green` and `project.mcp_boost_color`.
  - `vultra.material_graph.list_nodes` returned `count = 2` with both type ids
    and no diagnostics.
- Runtime MCP compile smoke:
  - Reused the temporary project, wrote
    `res://materials/mcp_two_project_nodes.vmatgraph.json` linking
    `vultra.param.color -> project.mcp_tint_green ->
    project.mcp_boost_color -> vultra.output.surface`.
  - `vultra.material_graph.compile` returned `ok = true`, `projectNodeCount =
    2`, and generated source containing `mix(`, `clamp(`, and the `1.25`
    override.
  - This exposed that the graph serializer was still using the older `node`
    link endpoint field. The canonical serialized fields are now `typeId` and
    `nodeId`.
- New project canonical graph smoke:
  - Launched editor with `xmake run vultra-app --editor --mcp --project
    example.vproject --no-xr --mcp-port 8855`.
  - Created `C:\tmp\vultra_canonical_material_graph_project_2` with
    `vultra.project.create_empty`, `template = "minimal"`.
  - Verified `resources/materials/default.vmatgraph.json` contains canonical
    `typeId` on all nodes and `nodeId` on all links, with no legacy
    material-graph `type` or `node` fields.
- Remaining phase documentation pass:
  - Updated `ai/tasks/unified-material-assets.md` with explicit phase status
    markers and a concrete remaining work queue.
  - Updated `ai/specs/unified-material-assets.md` and
    `ai/knowledge/vultra-formats.md` to state that `.vmatgraph.json` writers
    must emit `typeId` and `nodeId` directly.
  - Search checks passed:
    `rg 'typeId.*alias|nodeId.*alias|node type alias|link endpoint alias|alias expansion' ai/tasks ai/workspace ai/specs ai/knowledge tests source/vultra/src/function/material_graph -n`
    and
    `rg '"type"\s*: "vultra\.|"node"\s*:' resources/materials/default.vmatgraph.json source/vultra_app/src/project_launcher/project_launcher.cpp -n`.
  - `xmake run test-material-graph` passed.
  - `xmake run test-material-asset` passed.
  - `xmake build -y vultra-app` passed.
  - `git diff --check` passed with only existing CRLF warnings for generated
    builtin headers.

## Follow-Up

- Continue from the remaining work queue in
  `ai/tasks/unified-material-assets.md`, starting with import coverage and mesh
  slot material reference render smokes.
