# Unified Material Property Block Authoring

Date: 2026-06-03

## Scope

- Added Mesh Inspector support for adding MaterialPropertyBlock entries from
  the referenced material source schema.
- Supported authoring entries are the current Phase 7 block value types:
  `float`, `color`/`vec4`, and `texture2D`.
- Source schema lookup works through `.vmat.json` assets first, including
  builtin and graph-backed materials. Legacy direct `materialGraph` overrides
  use the graph blackboard schema.
- Manual property entry remains available for unsupported or future property
  names.
- Documented the persistence policy: Inspector-authored blocks are scene data;
  Lua/runtime edits change current entity state and do not mutate shared
  `.vmat.json` assets.

## Verification

- `xmake build -y vultra-app`

## Handoff

This closes one editor-authoring gap for Phase 7 without changing runtime
semantics. Remaining PropertyBlock work is live verification: two entities
sharing one `.vmat.json` should diverge visually through per-slot blocks.
