# Texture Subtype Import Parameters

## Goal

Support texture-only import settings in source `.vimport` params so editor users
can classify textures by subtype and reimport them with per-texture options.

## In Scope

- Source texture Inspector subtype and import parameter controls.
- Apply/Revert for source-side `.vimport` params.
- vasset per-texture params merged into `VTextureImporter::ImportOptions`.
- Subtype metadata on `TextureSelection` with optional selector filtering.

## Out of Scope

- Editing imported registry texture assets directly.
- Runtime gameplay or Lua APIs.
- Non-texture asset parameter editors.

## Verification

- `xmake build -y vultra-app`

## Handoff

- Built-in texture subtype ids are `default`, `ui_sprite`, `normal_map`, and
  `cursor`; custom string ids remain format-compatible.
- Apply writes normalized `texture.*` params while preserving unrelated params.
- Apply uses the existing pending import refresh path, but marks that batch as
  forced reimport so texture params are reapplied even when the source file did
  not change.
- Source texture Inspector shows both source file size and imported output size
  as a quick reference for compression/downscale results.
