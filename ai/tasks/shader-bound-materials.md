# Shader-Bound Materials Task

Spec: `ai/specs/shader-bound-materials.md`

## Scope

- Keep `.vshader` as the shader source boundary; do not create separate source
  asset types per stage.
- Add editor creation templates for common shader workflows.
- Add project shader examples and ensure new project shader manifests compile
  arbitrary shader subdirectories.
- Design and later implement shader-bound material assets that bind multiple
  shader passes.

## Current Slice

- Added Content Browser templates for Surface, Post Processing, Compute, and
  Raytracing shader sources.
- Added Content Browser templates for Render Graph render, post-processing,
  compute, and raytracing passes. Pass creation scans project `.vshader`
  sources by stage and writes the selected shader binding into the generated
  `RenderGraphPass` Lua file.
- Added a first Inspector drawer for `RenderGraphPass` Lua source assets so
  pass descriptors can be edited as structured assets instead of source text.
- Replaced raw shader binding fields in that drawer with stage-aware project
  shader selectors, while keeping manual id entry as a fallback.
- Added a project compute shader example under `resources/shaders/compute/`.
- Changed project shader library manifests to include `**/*.vshader`.
- Recorded the shader-bound material design direction.

## Follow-Up

- Add the concrete shader-bound material asset type and importer.
- Add Inspector editing for material shader/pass bindings.
- Add structured Inspector drawers for shader library manifests, shader-bound
  materials, render pipelines/features, and material graph metadata.
- Add renderer hooks for material pass groups.
- Add executable script-defined ray tracing render graph passes after TLAS/SBT
  binding conventions are available.
