# Builtin vshader conventions

A single `.vshader` file may contain **multiple stages** (`[vert]` / `[geom]` /
`[frag]` / `[comp]` / `[rgen]` / `[rchit]` / `[rahit]` / `[rmiss]` …). The runtime
resolves shaders by `id` from the compiled `.vshlib` libraries — **the filename is
not part of addressing** — so the stages of one pipeline should be kept together in
the same file: it is clearer and more concise. See
`passes/highend/particle_billboard.vshader` and
`passes/highend/visibility_buffer.vshader` for the established pattern, alongside the
merged `shadow_map` / `skybox` / `geometry_warp` / `ui_overlay` /
`gaussian_splat_render` / `basecolor_cpu`.

## When to merge, when to split

**Merge**: when a set of stages together form — and *only* form — a single pipeline,
and none of them is reused by another pipeline, define them in one `<name>.vshader`.

**Keep separate**: a stage reused by **multiple pipelines** must stay in its own file
to avoid duplicating it or breaking the reuse. In this repo that currently applies to:

- `passes/*/fullscreen_triangle.vert` — reused by 16+ fullscreen fragment shaders.
- `passes/highend/mesh.vert` — reused by `depth_pre.frag` (a cross-named pairing).
- `passes/highend/direct_gbuffer.vert` — reused by both `direct_gbuffer.frag` and
  `direct_depth_pre.frag`.

## Naming and id

- Filename: `<name>.vshader` (**without** a `.vert` / `.frag` stage suffix).
- `id = "builtin/<profile>/<name>"` (likewise without a stage suffix), where
  `<profile>` is `highend` / `general` / `compatibility`.
- C++ loads by **base name + stage enum**:
  `loadHighendShader("<name>", vshadersystem::ShaderStage::eVert)` /
  `…::eFrag` / `…::eGeom` … (see `loadHighendShader` / `loadGeneralShader` /
  `loadCompatibilityShader` in `source/vultra/core/rhi/base_pass.hpp`).

## Keywords (permute axes)

- A `.vshader` has exactly **one file-level** `[keywords]` block, defining the
  permutation axes for the whole shader.
- When the merged stages declare **different** keywords, the file-level `[keywords]`
  is their **union**. A stage that does not reference a given axis ends up with
  redundant variants, but this is harmless (the stage ignores it).
- **Pass the full keyword set at every load site** so the variant is selected
  explicitly, rather than relying on the (undocumented) loader behaviour of
  defaulting an unspecified permute axis to 0. For example `shadow_map`'s fragment
  stage does not use `VTX_HAS_SKIN`, yet the fragment load still passes it; and
  `gaussian_splat_render`'s vert/frag each use only one axis, yet both load sites
  pass both `USE_MULTIVIEW` and `WRITE_ENTITY_ID`.

## Build

`shader_task` in `builtin/xmake.lua` collects sources by glob
(`passes/<profile>/**.vshader`), so a new or merged `.vshader` is picked up
automatically — **no manifest edit needed**. The only exception is a file that is
listed explicitly (historically `basecolor_cpu` was), in which case keep that list in
sync after merging.
