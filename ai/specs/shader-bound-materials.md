# Shader-Bound Materials

Date: 2026-05-30

## Intent

Vultra should support two complementary material authoring paths:

- Material Graph: node-authored surface/material functions compiled into shader
  code.
- Shader-Bound Material: asset-authored materials that bind one or more
  `.vshader` entries and their parameters directly.

Shader-bound materials are for effects that do not fit a single generated
surface model: multi-pass outlines, dissolve plus depth prepass, layered
forward/deferred variants, custom compute preparation, ray tracing hit groups,
and other compound shading.

## Model

- A shader source is not a single stage asset. `.vshader` may contain multiple
  stage sections such as `[vert]`, `[frag]`, `[comp]`, `[rgen]`, `[rmiss]`,
  `[rchit]`, and `[rahit]`.
- A shader library is a compilation unit. Project manifests should include all
  project shader sources with `**/*.vshader` unless a project explicitly wants a
  narrower library.
- A shader-bound material owns parameter values and texture/resource bindings.
  It references one or more shader ids from one or more shader libraries.
- A material may define multiple passes. Each pass declares its pipeline kind,
  shader stages, render state, expected render graph inputs/outputs, and
  material parameter block layout.
- Renderers decide which material pass groups they consume. For example, a
  renderer may consume `depth`, `gbuffer`, `forward`, `post`, `compute`, or
  `raytracing` pass groups.

## Desired Asset Shape

The exact extension is still open, but the asset should be data-first and
friendly to both editor UI and scripting, for example:

```lua
return ShaderMaterial {
    shaderLibrary = "project",
    parameters = {
        baseColor = { type = "color", default = { 1.0, 1.0, 1.0, 1.0 } },
        dissolve  = { type = "float", default = 0.0 },
    },
    passes = {
        {
            name = "depth",
            pipeline = "graphics",
            shader = { vertex = "stylized.vshader", fragment = "stylized.vshader" },
        },
        {
            name = "outline",
            pipeline = "graphics",
            shader = { vertex = "outline.vshader", fragment = "outline.vshader" },
        },
        {
            name = "prepare",
            pipeline = "compute",
            shader = { compute = "material_prepare.comp" },
        },
    },
}
```

## Acceptance Criteria

- Content Browser can create shader source templates without treating stage as
  the asset boundary.
- Project shader libraries compile project `.vshader` files from arbitrary
  shader subdirectories.
- Render graph Lua passes can bind graphics and compute shader stages from the
  project shader library.
- Shader-bound material assets can reference multiple shader passes and expose
  parameters to Inspector.
- Renderers can query material pass groups without hardcoding a single PBR,
  Unlit, or Toonlike surface model.

## Open Work

- Define the concrete shader-bound material asset extension and serializer.
- Add an Inspector for shader-bound material parameters and pass list.
- Add runtime material parameter buffer packing for shader-bound materials.
- Wire renderer pass selection so custom material passes can participate in
  depth, GBuffer, forward, post, compute, and ray tracing paths.
