# Spec: Save / persistence system

Status: implemented (P1.2 of `ai/workspace/game-capability-roadmap.md`), including cross-scene
entity persistence (DontDestroyOnLoad).

## Intent

Give games a typed key/value store plus named, file-backed save slots for progress/options —
the foundation any progression game needs.

## Public interface

### `ISaveService` (Lua `Save`)

- KV: `setNumber/setBool/setString`, `has`, `valueType`, `getNumber/getBool/getString`
  (with fallback), `remove`, `clear`.
- Slots: `save(slot)`, `load(slot)`, `hasSlot(slot)`, `deleteSlot(slot)`, `listSlots()`.

### Lua `Save` (shim over the typed service)

- `Save.set(key, value)` (number|bool|string), `Save.get(key, default?)` (typed or default/nil),
  `Save.has/remove/clear`.
- `Save.save(slot)`/`Save.load(slot)` -> bool, `Save.hasSlot/deleteSlot`, `Save.listSlots()` ->
  array.

## Design / constraints

- `SaveSystem` (engine subsystem) holds the KV map in memory; it is **not** world data, so it
  survives `Scene.load`/reload — that's what makes "write, reload, read back" work without a
  slot round-trip.
- Slots are JSON files (`<slot>.vsave`) under a writable dir: `VULTRA_SAVE_DIR` override, else
  the OS app-data dir (`%APPDATA%/Vultra/saves`, `$XDG_DATA_HOME`/`$HOME/.local/share/...`),
  else temp. Slot names are sanitized to `[A-Za-z0-9_-]` (no path traversal).
- Values persist as native JSON (number/bool/string); type is inferred on load. Lua `Save.set`
  dispatches on the sol value type; `Save.get` returns the typed value or the supplied default.
- Registered before `ScriptSystem` so `ScriptContext::saveService` resolves; `Save` is a shim
  module (`script_save_shim`, area `save`), mirroring `Nav`.
- Naming: `Save.get`/`Save.set` are KV accessors (not `getX`/`setX` property prefixes), so they
  pass the conformance naming rules.

## Cross-scene entity persistence (DontDestroyOnLoad)

`PersistentComponent { keepOnLoad }` marks an entity (and its subtree) to survive a scene
replacement. `Scene.dontDestroyOnLoad(entity)` adds it; `Scene.isPersistent(entity)` queries.
`SceneSystem::clearWorldForSceneReplacement` promotes each persistent entity to a root,
collects the kept subtrees, and destroys only the non-kept roots (full `world.clear()` fast
path when none persist). `World::roots()` was added for the root sweep. Persistent entities
nested under others are flattened to roots on the keep pass (v1).

## Out of scope (follow-ons)

- Nested tables/arrays as first-class KV values (store JSON strings for now); encryption;
  slot metadata (timestamps, thumbnails); auto-save; a dedicated DontDestroyOnLoad "scene".

## Acceptance

- Lua writes player progress, reloads the scene, reads it back (KV survives; slot save/load
  round-trips to disk); conformance PASS; doc in `doc/lua_scripting.md` (EN) + CN mirror.
