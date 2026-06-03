# Unified Material Assets Parser Diagnostics

Date: 2026-06-03

## Scope

Tightened the current `.vmat.json` implementation after the material model
cleanup. The active source model is:

- `builtin`
- `graph`
- single `shader`

Multiple shader/pass-array material ownership and mini render graph material
plans remain intentionally removed. Reusable extension should happen through
Material Graph custom nodes.

## Code Changes

- Added a shared `.vmat.json` parser in
  `source/vultra/include/vultra/function/material/material_asset.hpp`.
- Parser diagnostics cover:
  - non-object roots;
  - non-`Material` type;
  - invalid or non-positive version;
  - missing or unknown `source.kind`;
  - missing builtin id, graph uri, or shader id;
  - non-object `properties`;
  - unsupported property JSON values.
- Runtime material loading now uses the shared parser before accepting builtin
  PBR or graph material assets.
- Inspector material source read/edit paths now use the shared parser and show
  diagnostics in the `.vmat.json` asset view.
- Content Browser keeps only the `Material/Builtin PBR` template for now because
  that is the only fully wired renderer path. Graph and single shader sources
  are parseable/savable but their complete render bindings remain later phases.

## Verification

- `rg 'shaderPasses|MaterialShaderPass|eShaderBound|shaderBoundMaterials|source\.passes|pass group|pass groups|mini render graph|minimal render graph|multiple shader|multiple shaders|multi-post|shader chain' source\vultra source\vultra_app ai\specs ai\tasks ai\knowledge -n`
  - No active code/spec/task/knowledge hits.
- `git diff --check`
  - Passed with only existing CRLF warnings in generated builtin files.
- `xmake build -y vultra-app`
  - Passed.
- `xmake build -y test-material-asset`
  - Passed.
- `xmake run test-material-asset`
  - Passed with `material_asset tests passed`.

## Follow-Up

- Add focused parser unit tests if/when this repo exposes a convenient C++ test
  target for material utilities.
- Continue with Material Graph blackboard/custom node work before treating graph
  materials as fully authorable project assets.
- Keep single shader material source simple: one shader library plus one shader
  id, with parameters coming from future shader reflection.
