# Custom Material-Graph Nodes (BXDF / helper functions)

Projects can add custom material-graph nodes by dropping a `*.vmatnode.json`
descriptor anywhere under the project. They are scanned and layered on top of the
builtin node set (`loadProjectMaterialGraphNodes` → `makeBuiltinNodeRegistry()`),
so they appear in the material graph editor and are used by the compiler.

## Referencing a helper / BXDF function by name

A custom node's `implementation.outputs` can call a **whole GLSL function** pulled
in from a shader-library artifact via `implementation.includes`, instead of
inlining the math. This is the basis for user-defined BXDFs/shading helpers.

Example — [`resources/materials/nodes/sheen.vmatnode.json`](../resources/materials/nodes/sheen.vmatnode.json)
referencing [`resources/shaders/bxdf/sheen.glsl`](../resources/shaders/bxdf/sheen.glsl):

```json
{
  "type": "MaterialGraphNode", "version": 1,
  "typeId": "project.bxdf.sheen", "displayName": "Sheen Rim",
  "inputs": [
    { "name": "tint", "type": "color", "default": [1,1,1,1] },
    { "name": "normalWS", "type": "vec3" }, { "name": "viewDirWS", "type": "vec3" },
    { "name": "intensity", "type": "float", "default": 1.0 }
  ],
  "outputs": [ { "name": "rgb", "type": "vec3" } ],
  "implementation": {
    "language": "glsl",
    "includes": [ "bxdf/sheen.glsl" ],
    "outputs": {
      "rgb": "vultra_node_sheen({{input:tint}}.rgb, {{input:normalWS}}, {{input:viewDirWS}}, {{input:intensity}})"
    }
  }
}
```

The compiler collects every used node's `implementation.includes` and emits the
`#include` directives at the top of the generated surface source, so the node's
expression can call the included function. `{{input:name}}` / `{{param:name}}`
are substituted as before. Verified by `tests/material_graph` (asserts the
`#include` and the call appear in the generated source).

The shading model is path-agnostic by design (`builtin/shaders/include/vultra/shading_model_abi.glsl`):
a BXDF authored as `vec3 fn(VultraSurface, VultraLight, VultraShadingExtra)` is
callable from any lighting path via the generated `vultra_eval_bxdf(model, …)`
dispatch — deferred today, forward+ later.

## Runtime caveats (important)

Two engine realities bound what renders **today** vs. what needs further work:

1. **Material graphs are evaluated parametrically at runtime.** The live render
   path (`render_system.cpp::materialGraphSurfaceParams`) reduces a graph's output
   to **constant** surface params + texture indices and feeds a generic GBuffer
   shader. The per-graph GLSL codegen (`eval_material_graph_*`, where node
   `includes` take effect) is exercised by the **editor preview / MCP
   `compile_material_graph`**, not the live render. Per-pixel procedural / custom
   node GLSL renders at runtime only via the **shader-material path**
   (`GpuMaterialModel::eShaderMaterial`, a custom `.vshader` fragment) or once a
   "material graph → compiled material shader" runtime path exists.
2. **Lighting is fixed Cook-Torrance in the deferred pass.** A custom BXDF's
   *lighting response* needs a forward path where the material shader evaluates
   lights itself (Forward+). The ABI / registry / dispatch are already in place
   for that; the forward light binding is the remaining piece.

So the helper-function-include mechanism and the `project.bxdf.sheen` example are
real and codegen-verified now; full runtime custom-BXDF **shading** lands with the
forward material path.
