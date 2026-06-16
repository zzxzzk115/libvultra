# Task: Save / persistence system (P1.2)

Spec: `ai/specs/save-persistence.md`. Roadmap: P1.2.

## Goal

Typed KV store + named file slots, exposed to Lua as `Save`.

## In scope

- `ISaveService` + `SaveSystem` (in-memory KV, JSON slot files, writable-dir resolution).
- Lua `Save` shim (generic set/get + slots), regenerated bindings, stub, conformance, EN+CN docs.

## Also done (this pass)

- DontDestroyOnLoad: `PersistentComponent` + `Scene.dontDestroyOnLoad`/`isPersistent` +
  selective `clearWorldForSceneReplacement` + `World::roots()` + reflection.

## Out of scope (follow-ons)

- Nested-table KV values; encryption; slot metadata / auto-save; dedicated persistence scene.

## Implementation plan (done)

1. `save_service.hpp` (`ISaveService` + `ValueType`).
2. `save/save_system.{hpp,cpp}` (KV map, JSON slots, save-dir resolution + slot sanitize).
3. `script_save_shim.{hpp,cpp}` (`Save.*`); `script_save_binding.hpp` + aggregator call;
   manifest entry; `ScriptContext::saveService` + populated in `ScriptSystem`.
4. Registered `SaveSystem` in `demo_app_host` before `ScriptSystem`.
5. Codegen (`extract_bindings` + `gen_lua`) -> `script_save_binding.gen.cpp` + stub.
6. Docs: lua_scripting.md "Save / Persistence" (EN+CN).

## Verification plan

- `xmake build -y vultra` compiles.
- `xmake run test-lua-api-conformance` passes (Save in the stub, get/set are KV accessors).
- Lua: `Save.set` then `Save.save("slot1")`, reload scene, `Save.load("slot1")` + `Save.get`.

## Status

Code + bindings + docs implemented. Final compile/run verification pending a local build.
Cross-scene entity persistence deferred to a follow-on.
