# Custom Material-Graph Nodes (BXDF / helper functions)

**English** | [简体中文](zh_CN/material_custom_nodes_CN.md)

Projects can add custom material-graph nodes by dropping a `*.vmatnode.json`
descriptor anywhere under the project. They are scanned and layered on top of the
builtin node set (`loadProjectMaterialGraphNodes` → `makeBuiltinNodeRegistry()`),
so they appear in the material graph editor and are used by the compiler.

## Surface output nodes (per shading model)

A surface graph ends in exactly **one** per-model output node — the shading model is
the node identity (`typeId`), not a parameter, so each node exposes only the pins that
model actually uses:

| Output node `typeId`    | Shading model            | Extra pins beyond baseColor/normal/emissive/alpha/alphaCutoff |
|-------------------------|--------------------------|---------------------------------------------------------------|
| `vultra.output.pbr_mr`  | PBR Metallic-Roughness   | `metallic`, `roughness`, `ao`                                 |
| `vultra.output.pbr_sg`  | PBR Specular-Glossiness  | `specular`, `glossiness`, `ao`                                |
| `vultra.output.phong`   | Phong                    | `specular`, `shininess`, `ao`                                 |
| `vultra.output.unlit`   | Unlit                    | (none — just baseColor/alpha)                                 |
| `vultra.output.toon`    | Toon / cel               | `ao`                                                          |
| `vultra.output.custom`  | a registered custom model| `shadingModelName` param (resolved against the registry)      |

Each node bakes its GBuffer model code into the generated surface (`surface.shadingModel`);
the GBuffer is metallic-roughness shaped, so SG/Phong are converted to it at codegen time,
mirroring the hand-authored asset path (`thin_gbuffer.frag` `material_mra`). Legacy graphs
authored with the old single `vultra.output.surface` node (+ a `shadingModel` param) are
migrated to the matching per-model node on load (`graphFromJson`).

The single source of truth for "what counts as an output node" is
`material_graph::surfaceOutputTypeIds()` / `isSurfaceOutputType()`, shared by the
validator, compiler, render system, and editor.

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

1. **Constant-foldable graphs are packed as a real per-model material.** When a
   graph's surface reduces to constants, `render_system.cpp::packGraphConstantMaterial`
   packs the matching `MaterialParamsPBRMR/PBRSG/Phong/Unlit/Toon` block and sets the
   GpuMaterial's real model code — it renders through the **exact same GPU path** as a
   hand-authored `.vmat.json` material (there is no separate "graph" GPU model anymore).
   Graphs that need per-pixel evaluation (texture/procedural/custom-node GLSL, where
   `includes` take effect) render through the compiled **shader-material path**
   (`GpuMaterialModel::eShaderMaterial`, the `MeshMaterialBackend` `.material.frag`).
2. **Lighting is fixed Cook-Torrance in the deferred pass.** A custom BXDF's
   *lighting response* needs a forward path where the material shader evaluates
   lights itself (Forward+). The registry + Lua descriptor foundation is in place:
   a pipeline asset can declare `ShadingModel{ name=…, bxdfLibrary=…, bxdfArtifact=…,
   bxdfFunction=…, extraParamSize=… }` entries, which `DeclarativeRenderer` parses into
   its `ShadingModelRegistry` (codes assigned from `kFirstCustomShadingModelCode`=8).
   The `vultra.output.custom` graph node selects one by `shadingModelName`. **Not yet
   wired:** the `ShadingModelParamsBuffer` GPU binding and the generated
   `vultra_eval_bxdf` dispatch — the deferred lighting shader has no custom dispatch, so
   a custom-model material currently renders with the **default PBR response**. Custom
   BXDF *shading* lands with the forward/clustered path (or a runtime-recompiled deferred
   variant), which is the remaining piece.

So the helper-function-include mechanism and the `project.bxdf.sheen` example are
real and codegen-verified now; the shading-model registry + `ShadingModel{}` descriptor
are wired; full runtime custom-BXDF **shading** lands with the forward material path.
