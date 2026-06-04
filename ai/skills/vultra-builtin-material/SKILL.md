---
name: vultra-builtin-material
description: Use when adding, changing, or reviewing builtin materials in libvultra — .vmat.json assets, the MaterialAsset model and source schema, builtin/pbr GPU params (MaterialParamsPBRMR), render_system parameter packing, content-browser creators, and the inspector material panel. Covers both a new preset on an existing source and a brand-new builtin source kind.
---

# Vultra Builtin Material Workflow

First decide which of two things you are adding — they differ by an order of magnitude:

- **A. New preset on an existing source** (almost always `builtin/pbr`): just a new
  `.vmat.json` with different default `properties`. No C++ changes required.
- **B. New builtin source kind** (e.g. `builtin/toon`, a different shading model with
  its own parameters/GPU layout/shader): a full vertical slice through schema, GPU
  params, the render-system loader, a shader, and editor surfaces.

Read `ai/specs/unified-material-assets.md` before starting. Pick A unless the new
material genuinely needs parameters or shading that `builtin/pbr` cannot express.

## The Material Model

- Asset format: `.vmat.json` text asset. Shape:
  ```json
  {
    "type": "Material", "version": 1, "name": "Default",
    "source": { "kind": "builtin", "id": "builtin/pbr" },
    "properties": { "baseColor": [0.8, 0.82, 0.86, 1.0], "metallic": 0.0, "roughness": 0.55 }
  }
  ```
- C++ model: `MaterialAsset` / `MaterialSourceRef` / `MaterialPropertySchema` in
  `source/vultra/include/vultra/function/material/material_asset.hpp`. `source.kind`
  is `eBuiltin | eGraph | eShader`; for builtin, `source.id` selects behavior.
- Schema (the editable parameter list + UI ranges) comes from
  `resolveMaterialSourceSchema(source)` in the same header; `builtin/pbr` maps to
  `builtinPbrMaterialSchema()`. The inspector and validation both read this.
- GPU layout: `MaterialParamsPBRMR` in
  `source/vultra/include/vultra/function/material/material_params.hpp` (16-byte
  aligned). `builtinPbrMaterialParamsFromAsset(...)` in
  `source/vultra/src/function/rendering/render_system.cpp` parses the `.vmat.json`,
  resolves texture indices, and packs this struct. It **rejects any `source.id`
  other than `builtin/pbr`** — so a new source kind needs its own loader.
- Shader ABI: `builtin/shaders/include/vultra/mesh_material.glsl`
  (`VultraMaterialInput` / `VultraMaterialEval` / `VULTRA_MATERIAL_MAIN`).
- Builtin material assets live in `builtin/materials/` (e.g. `default.vmat.json`).

## Path A — New Preset (no C++)

1. Create `builtin/materials/<name>.vmat.json` with `source.id = "builtin/pbr"` and the
   preset `properties`. Only include keys present in `builtinPbrMaterialSchema()`.
2. (Optional) Add a content-browser creator so authors can spawn it from the menu:
   in `source/vultra_app/src/editor_app/content_asset_registry.cpp`, add a
   `makeBuiltin<Name>MaterialText(...)` template and a `registry.registerCreator(...)`
   entry (model it on `vultra.builtin_pbr_material` / `makeBuiltinPbrMaterialText`).
3. Verify the asset parses and the inspector shows the schema fields.

## Path B — New Builtin Source Kind

1. **Schema** — in `material_asset.hpp`, add `builtin<Name>MaterialSchema()` (use
   `materialColorParam` / `materialFloatParam` / `materialBoolParam` /
   `materialTextureParam`) and add a branch in `resolveMaterialSourceSchema` for
   `source.id == "builtin/<name>"`.
2. **GPU params** — in `material_params.hpp`, add a 16-byte-aligned
   `MaterialParams<Name>` struct (keep the `static_assert(sizeof(...) % 16 == 0)`).
   Reuse `MaterialParamsPBRMR` if the layout matches.
3. **Loader** — in `render_system.cpp`, add
   `builtin<Name>MaterialParamsFromAsset(...)` that validates the new `source.id`,
   reads `properties`, resolves texture URIs to GPU indices, and packs the struct.
   Wire it into the material-resolution path that currently calls the PBR loader so
   the renderer selects the right loader by `source.id`.
4. **Shader** — author the material shader using the `mesh_material.glsl` ABI so the
   GBuffer/lighting path can evaluate it, and ensure the pass that draws it can find
   the variant.
5. **Editor** — confirm the inspector material panel renders the new schema
   (it is schema-driven, so usually automatic). Add a content-browser creator
   (Path A step 2) for the new `source.id`.
6. **Builtin asset** — add `builtin/materials/<name>.vmat.json` referencing the new id.

## Naming

- Source ids are lowercase `builtin/<name>`.
- Property keys are camelCase and must match a schema parameter name exactly.
- Files: `<name>.vmat.json`; creators `vultra.builtin_<name>_material`.

## Verification

- Build: `xmake build -y vultra-app` (and `xmake build -y test-material-asset` if you
  touch parsing/schema; add a parse + schema-resolution case in `tests/material_asset/`).
- Content browser can create the material; inspector shows and edits its properties;
  edits round-trip to the `.vmat.json` `properties` block.
- Assign the material to a mesh and confirm shading via Runtime MCP
  (`ai/knowledge/mcp-tools.md`): `vultra.render.capture_rgb`.

## Files To Check

- `source/vultra/include/vultra/function/material/material_asset.hpp`
- `source/vultra/include/vultra/function/material/material_params.hpp`
- `source/vultra/src/function/rendering/render_system.cpp` (`builtinPbrMaterialParamsFromAsset`)
- `source/vultra_app/src/editor_app/content_asset_registry.cpp`
- `source/vultra_app/src/editor_app/ui/windows/inspector_window.cpp` (material panel)
- `builtin/materials/*.vmat.json`, `builtin/shaders/include/vultra/mesh_material.glsl`
- `tests/material_asset/`

## Pitfalls

- Choosing Path B for what is really a preset — most "new materials" are Path A.
- Adding a new `source.id` without a loader: `builtinPbrMaterialParamsFromAsset`
  returns `nullopt` for unknown ids and the material silently renders wrong/default.
- `properties` keys that are not in the schema: ignored, not errored.
- Forgetting the GPU struct's 16-byte alignment / `static_assert`.
