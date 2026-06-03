# Vultra Formats

- `.vproject` identifies a Vultra project and declares the asset root, default
  scene, and editable render graph.
- `.vscn` stores scene entities and reflected component fields.
- `.vrg.json` stores declarative render graph nodes, resources, editor layout,
  and pass parameters.
- `.vrp.lua` stores Lua-authored render pipeline or pass definitions.
- `.vshaderlib.lua` declares project shader libraries and shader globs.
- `.vmat.json` stores the mesh-facing material asset. Version 1 references a
  `builtin`, `graph`, or single `shader` source and stores material property
  overrides. Builtin defaults live under `builtin://materials/`;
  project-authored or forked materials live under `res://`.
- `.vmatgraph.json` stores material graph authoring data. It is a material
  source editor format; mesh slots should prefer `.vmat.json` assets that
  reference graphs when graph-backed material instances need fork/edit behavior.
  Canonical graph files use `typeId` for node type identifiers and `nodeId` for
  link endpoint node references. Pin `type` remains the value type. Runtime
  readers may accept old `type`/`node` graph files, but new files, templates,
  tests, and docs should write `typeId`/`nodeId`.
- `.vmatnode.json` stores reusable Material Graph custom node descriptors:
  `typeId`, display name, typed input/output pins, default params, and optional
  GLSL output expressions. The `vultra.*` namespace is reserved for engine
  builtin nodes; project/plugin nodes should use their own namespace. GLSL
  expressions can use `{{input:name}}` and `{{param:name}}` placeholders.
- `.vultrapackage` stores exported source assets for project-to-project import.
  Import skips existing files when the package MD5 matches the destination MD5.
- Source-side `.vimport` files may store texture import metadata under
  `[params]` using stable `texture.*` keys. Texture subtype ids include
  `default`, `ui_sprite`, `normal_map`, and `cursor`; unknown subtype strings
  remain valid for future extension. Current texture params include
  `texture.subtype`, `texture.generate_mipmaps`, `texture.flip_y`,
  `texture.target_format`, `texture.uastc`, `texture.quality_level`,
  `texture.compression_level`, `texture.compress_only_large`,
  `texture.downscale_large`, `texture.downscale_min_dimension`,
  `texture.downscale_target_dimension`, `texture.bake_normal_map`, and
  `texture.directx_normal_map`.
- `resources.vpk` is a packaged runtime asset bundle.

Treat these formats as project-facing API. Prefer additive changes and safe
defaults over renames.

See `ai/knowledge/packaged-runtime.md` for packaged runtime rules. In short:
VPK runtime content discovery must go through the mounted registry/VFS, not a
recursive physical scan of `res://`.
