# Unified Material Assets

Date: 2026-06-02

## Intent

Vultra materials have one mesh-facing asset model. Mesh slots reference a
`.vmat.json` material asset that can be inspected, forked, edited, and
overridden per entity or slot.

Material authoring has three source kinds:

- Builtin source: engine-provided material behavior such as `builtin/pbr`.
- Graph source: a `.vmatgraph.json` material graph.
- Shader source: one handwritten mesh material shader entry using the Vultra
  mesh material ABI.

Material Graph is the extensibility path. Users who need reusable custom
behavior add reusable graph nodes, with shader code hidden behind node
implementations where needed. A material asset stays a source reference plus
property values.

## Asset Model

`.vmat.json` is the material asset format. Builtin defaults live under
`builtin://materials/`, while project-authored or forked material assets live
under `res://`.

```json
{
  "type": "Material",
  "version": 1,
  "name": "Default",
  "source": { "kind": "builtin", "id": "builtin/pbr" },
  "properties": {
    "baseColor": [1.0, 1.0, 1.0, 1.0],
    "metallic": 0.0,
    "roughness": 0.5
  }
}
```

Graph-backed material:

```json
{
  "type": "Material",
  "version": 1,
  "source": {
    "kind": "graph",
    "uri": "res://materials/stylized.vmatgraph.json"
  },
  "properties": {
    "tint": [1.0, 0.2, 0.1, 1.0]
  }
}
```

Single-shader material:

```json
{
  "type": "Material",
  "version": 1,
  "source": {
    "kind": "shader",
    "shaderLibrary": "project",
    "id": "materials/custom_surface.frag"
  },
  "properties": {}
}
```

Material parameters are not owned by the Inspector. A material source exposes a
schema, and `.vmat.json` stores values for that schema in `properties`. Builtin
sources expose engine-authored schemas, graph sources expose blackboard schemas,
and shader sources expose reflection-derived schemas from the compiled shader
library when available.

`.vmatnode.json` stores reusable Material Graph node descriptors. Version 1
defines a custom node `typeId`, display name, typed input/output pins, default
params, and an optional GLSL expression implementation. The reserved `vultra.*`
namespace belongs to engine-provided nodes; project/custom nodes should use
names such as `project.tint` or plugin-owned namespaces.

`.vmatgraph.json` stores graph authoring data. Its canonical version 1 node
identifier fields are:

- graph nodes use `typeId` for the node type, such as
  `vultra.texture.sample2d`;
- link endpoint references use `nodeId` for the referenced node instance id;
- pin `type` fields remain value-type declarations, such as `float`, `color`,
  or `texture2D`.

Loaders may read old graph files that used `type` for node type ids or `node`
for link endpoints, but all new writers, templates, tests, and examples should
emit `typeId` and `nodeId` directly.

The first implementation backend is a GLSL snippet expression per output pin.
Expressions may use `{{input:name}}` to reference resolved graph inputs and
`{{param:name}}` to reference node params/default params. This keeps custom
behavior inside Material Graph composition instead of adding multiple material
shaders or a material-owned render graph.

```json
{
  "type": "MaterialGraphNode",
  "version": 1,
  "typeId": "project.tint",
  "displayName": "Project Tint",
  "inputs": [
    { "name": "color", "type": "color", "defaultValue": [1.0, 1.0, 1.0, 1.0] },
    { "name": "amount", "type": "float", "defaultValue": 1.0 }
  ],
  "outputs": [
    { "name": "out", "type": "color" }
  ],
  "defaultParams": {
    "amount": 0.5
  },
  "implementation": {
    "language": "glsl",
    "outputs": {
      "out": "mix({{input:color}}, vec4(1.0, 0.0, 0.0, 1.0), {{param:amount}})"
    }
  }
}
```

## Runtime Semantics

Material properties resolve in this order:

1. source defaults;
2. `.vmat.json` asset property overrides;
3. future parent material chain, if a separate instance asset is introduced;
4. entity or slot-level `MaterialPropertyBlock`.

Version 1 does not add `.vmatinst`. A `.vmat.json` is already a source plus
overrides, and can be duplicated or forked as a project asset.

## Import And Compatibility

Explicit reimport emits `.vmat.json` assets for imported mesh materials under
`res://materials/imported/<mesh>/<slot>_<material>.vmat.json`. Generated assets
use `source.kind = "builtin"` with `id = "builtin/pbr"` and map imported
material factors/textures into material properties. Old imported assets are not
migrated in place; delete imported output and reimport the source asset when the
generated material layout needs to refresh.

`.vimport` acts as source/import sidecar metadata: importer identity, import
settings, stable source identity, dependency tracking, and future generated
asset mapping policy. Runtime material parameter values live in `.vmat.json`
properties, not in `.vimport`.

Existing scene `materialGraph` overrides stay readable for compatibility, but
new UI and new authoring paths should prefer the unified `material` URI.
Builtin primitive meshes with no explicit material override use
`builtin://materials/default.vmat.json`; imported meshes keep their imported
embedded materials until explicit reimport emits project `.vmat.json` assets.

Mesh material slot overrides can carry a first-version MaterialPropertyBlock.
The block supports float, color/vec4, and texture URI values and is applied on
top of shared `.vmat.json` properties for that entity/slot.

MaterialPropertyBlock values authored in the Mesh Inspector are scene authoring
data and are serialized with `MeshComponent/materialOverrides`. Runtime or Lua
writes update the current world/entity component state and must not mutate the
referenced shared `.vmat.json`; they become persistent only if an editor
workflow explicitly saves that world state back to a scene.

## Acceptance Criteria

- The engine includes `builtin://materials/default.vmat.json`; new projects do
  not copy it unless the user forks it into project content.
- Content Browser and Inspector treat `.vmat.json` as a material asset.
- Inspector draws `.vmat.json` properties from the resolved material source
  schema instead of hardcoding UI fields per asset.
- Mesh material slot overrides can reference `.vmat.json` while old
  `.vmatgraph.json` overrides still load.
- Builtin `.vmat.json` properties can be packed into the existing GPU material
  parameter path.
- Material Graph blackboard parameters feed the same material source schema path
  as builtin schemas, and graph-backed `.vmat.json` properties feed the current
  graph surface evaluation path.
- Material Graph supports user-extensible reusable nodes through
  `.vmatnode.json` descriptors and GLSL output expressions.
- Single-shader material sources render through the DirectGBuffer mesh material
  ABI. Shader reflection drives editable schemas and parameter packing, while
  the shader writes `VultraMaterialEval` for the existing deferred lighting
  path.
- MaterialPropertyBlock can override float, color, and texture URI properties per
  mesh slot without modifying the referenced shared `.vmat.json`.
