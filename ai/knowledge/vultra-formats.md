# Vultra Formats

- `.vproject` identifies a Vultra project and declares the asset root, default
  scene, and editable render graph.
- `.vscn` stores scene entities and reflected component fields.
- `.vrg.json` stores declarative render graph nodes, resources, editor layout,
  and pass parameters.
- `.vrp.lua` stores Lua-authored render pipeline or pass definitions.
- `.vshaderlib.lua` declares project shader libraries and shader globs.
- `.vultrapackage` stores exported source assets for project-to-project import.
  Import skips existing files when the package MD5 matches the destination MD5.
- `resources.vpk` is a packaged runtime asset bundle.

Treat these formats as project-facing API. Prefer additive changes and safe
defaults over renames.

See `ai/knowledge/packaged-runtime.md` for packaged runtime rules. In short:
VPK runtime content discovery must go through the mounted registry/VFS, not a
recursive physical scan of `res://`.
