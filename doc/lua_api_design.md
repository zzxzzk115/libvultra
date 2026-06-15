# Vultra Lua API Design Specification

**English** | [简体中文](zh_CN/lua_api_design_CN.md)

This document is **normative**. Every Lua-facing binding merged into the engine
must follow these rules. Conformance is enforced mechanically by
`tests/lua_api_conformance` (see [Enforcement](#enforcement)); pre-existing
violations are tracked in its `exceptions.lua` burn-down list and may not grow.

For the user-facing scripting guide, see [lua_scripting.md](lua_scripting.md).
For the foundation roadmap that motivates this spec, see
`ai/workspace/foundation-roadmap.md`.

## 1. Naming

| Surface | Convention | Examples |
|---|---|---|
| Global namespace tables | `PascalCase` | `Input`, `Physics`, `World`, `Time` |
| Types (usertypes) | `PascalCase` | `Vec3`, `Entity`, `RigidBody` |
| Enum tables and members | `PascalCase` / `PascalCase` | `KeyCode.Space`, `ForceMode.Impulse` |
| Methods, free functions | `camelCase`, verb first | `setParent`, `playOneShot`, `raycast` |
| Properties | `camelCase`, noun | `position`, `linearVelocity`, `deltaTime` |
| Event/signal properties | `onPascalCaseEvent` | `onClick`, `onPointerEnter` |

Additional rules:

- **Constructors** are the callable type table: `Vec3(1, 2, 3)`. Lowercase free
  constructor functions (`vec3(...)`) are deprecated aliases (see
  [Deprecation](#7-deprecation-policy)).
- **Enums never leak raw integers** through signatures. A function takes/returns
  an enum table member, a string token (documented set), or a typed value --
  never "pass 2 for impulse".
- **Boolean queries** are prefixed `isX` / `hasX` / `canX`: `isKeyHeld`,
  `hasComponent`, `canJump`. Bare-noun boolean *properties* are allowed when
  they read naturally as state: `entity.valid`, `animator.playing`.
- No `get` / `set` prefixes on properties (see section 2). `setX(...)` method
  form is reserved for operations that take arguments beyond the value or have
  side effects worth flagging (`setLayerCollision(a, b, enabled)`).
- **Symmetric-pair exception**: a *parameterized lookup* keeps the `get`
  prefix when a matching `setX` with the same parameters exists --
  `Animation.getFloat(entity, name)` / `Animation.setFloat(entity, name, v)`
  (Unity-style). The conformance test only flags `getX` with no `setX`
  sibling; do not use this exception for parameterless accessors.
- **C++ and Lua names match.** The Lua surface mirrors the C++ identifiers --
  when a name needs to change for script ergonomics, rename the C++ side too
  (service method, component field), keeping one vocabulary engine-wide.
  Generator `name=` overrides are a last resort for irreconcilable cases.
  Component-field renames keep a legacy `.vscn` read alias in
  `scene_reflection.cpp` (dual `.data<>()` registration) so existing scenes
  load; the writer emits only the new key.

## 2. Property vs method

- A parameterless, cheap, side-effect-free read (and its paired write) is a
  **property**: `Time.deltaTime`, `transform.position`, `Physics.enabled`,
  `rigidBody.mass`. Reads and writes use field syntax, never `getX()`/`setX()`.
- Anything that takes arguments, does non-trivial work, or has effects beyond
  storing the value is a **method**, named imperative-verb-first:
  `Input.isKeyDown(KeyCode.W)`, `world:findByName(name)`,
  `rigidBody:addImpulse(v)`.
- Methods on instances use `:` (self) call form; namespace-table functions use
  `.` call form (`Physics.raycast(...)`, not `Physics:raycast(...)`).

### Entity component access

Entity handles expose only the always-on fields as direct properties --
`entity.valid`, `entity.id`, `entity.name`, `entity.active`, `entity.visible`,
`entity.transform` -- plus the hierarchy methods (`destroy`, `parent`,
`firstChild`, `nextSibling`, `setParent`). Every **other** component is reached
through one generic, Unity-style method set keyed by a `Component` enum token,
never through per-component properties:

- `entity:addComponent(Component.X)` -- add if absent, return the component ref.
- `entity:getComponent(Component.X)` -- return the ref, or `nil` if absent
  (the section 4 "not found returns `nil`" rule).
- `entity:removeComponent(Component.X)` -- return `true` if one was removed.
- `entity:hasComponent(Component.X)` -- boolean.

These are camelCase methods (section 1); `Component` is a PascalCase enum table
whose members are PascalCase tokens (`Component.RigidBody`, `Component.Camera`,
...) and are the only thing passed across the boundary -- raw integers never leak
(section 1, enums rule). The returned references are the same typed component-ref
usertypes documented elsewhere; only the access path is generic. Per-component
accessor properties (`entity.rigidBody`), per-component `has<Name>()` methods, and
the `World.add*/remove*` component family are **not** part of the surface.

## 3. Units

- **Defaults carry no suffix**: distance in meters, time in seconds, angles in
  **degrees** (designer-facing convention, matching Unity/Godot expectations).
- Any non-default unit **must** be suffixed: `Px` (UI pixels), `Ms`
  (milliseconds), `Radians`. Examples: `anchoredPositionPx`, `fadeOutMs`.
- Canonical euler rotation property is `rotation` (Vec3, degrees). The legacy
  `rotationEuler` / `rotationDegrees` pair is deprecated. Add `rotationRadians`
  only if a real need appears.

## 4. Errors and nil

Each failure category has exactly one style; modules must not mix them:

- **"Not found" queries return `nil`** -- never `false`, never an error:
  `World.findByName(name) -> Entity|nil`, `Camera.findPrimary() -> Camera|nil`.
- **Programmer errors raise** (wrong argument type/count) -- sol2's default
  behavior; do not catch-and-return.
- **Runtime-fallible operations return `ok, err`**:
  `local ok, err = Scene.load(uri)`. `err` is a human-readable string.
- Handle/reference types expose a `valid` property; calling into an invalid
  handle raises rather than silently no-oping.

## 5. Options-table pattern

A function with more than 3 parameters, or 2+ optional parameters, takes
`(required..., opts)` where `opts` is a table of documented keys:

```lua
Audio.playMusic(clip, { volume = 0.8, loop = true, fadeInMs = 250 })
```

Do not grow long positional optional tails (`f(a, b, nil, nil, 0.5)` is a spec
violation).

## 6. Events and signals

- The canonical form is an `onX` signal property supporting `:connect(fn)`
  (multi-subscriber) -- `button.onClick:connect(handler)`.
- Event callbacks receive a single event object (`event.target`,
  `event:stopPropagation()`), not positional argument lists.
- Duplicate aliases for the same event (e.g. `clicked` next to `onClick`) are
  forbidden; existing ones are deprecated.

## 7. Deprecation policy

Pre-1.0, renames are allowed but never silent:

1. The new name lands; the old name stays as an alias for **one release**.
2. The alias logs a one-time warning per session:
   `[Lua] 'vec3' is deprecated, use 'Vec3'` (single shared shim mechanism --
   see `script_deprecations.cpp` once it exists; do not hand-roll per-module
   warnings).
3. The alias is deleted in the following release.
4. All in-repo Lua (examples, templates, builtin scripts, doc samples) migrates
   in the **same PR** as the rename. Aliases exist only for external projects.

## 8. Coverage rule (binding parity)

A gameplay-facing engine feature is not "done" until its Lua surface is:

- bound following this spec,
- present in the LuaLS stub (`tools/lua-stubs/vultra.lua`),
- documented in `doc/lua_scripting.md`,
- green in `tests/lua_api_conformance`.

A PR adding a gameplay-facing component or system without these fails review.
Editor-only or engine-internal systems (job system, GPU resources, shader
service) are exempt by design; the conformance test's root-table list is the
authoritative scope.

## 9. Documented exceptions

- **`ImGui.*`** (when it lands, Phase 4): keeps upstream Dear ImGui PascalCase
  function names (`ImGui.Begin`, `ImGui.Button`) so upstream docs and community
  knowledge transfer directly. The conformance test whitelists the `ImGui`
  table.
- Lua standard library globals are obviously out of scope.

## Enforcement

`tests/lua_api_conformance` boots a headless Lua state with the full binding
set registered, then:

1. enumerates the live API surface (root tables, members, usertypes),
2. asserts the naming rules above,
3. cross-checks the surface against `tools/lua-stubs/vultra.lua`
   (a stub symbol with no live counterpart is a **hard failure** -- stale
   stubs are never acceptable; a live symbol missing from the stub must be
   listed in `exceptions.lua`),
4. requires every violation to be either fixed or present in the checked-in
   `exceptions.lua` baseline. The baseline may only shrink.

Run it with:

```
xmake build test-lua-api-conformance
xmake run test-lua-api-conformance
```

To regenerate the burn-down baseline after intentionally fixing violations
(the file must be ASCII/UTF-8 -- PowerShell's `>` writes UTF-16, which Lua
cannot parse):

```powershell
& <target-dir>\test-lua-api-conformance.exe --dump |
    Out-File -Encoding ascii tests\lua_api_conformance\exceptions.lua
```

## Binding generator

> **Adding or changing a binding? Read [script_binding_codegen.md](script_binding_codegen.md)** —
> the authoring standard (annotation vocabulary, decision guide, recipes, the
> step-by-step, and the conventions every new binding must follow). The summary
> below is the rationale; that doc is the how-to.

Bindings are generated from annotated C++ through a two-stage, IR-based,
multi-language pipeline. Annotations are language-neutral `VBIND_*` markers
(see `vultra/core/base/script_annotations.hpp`); a libclang frontend extracts
them into a checked-in IR, and per-language backends emit bindings from that IR:

```
# stage 1: annotated headers -> tools/bindings/ir/bindings.ir.json
python tools/python/extract_bindings.py
# stage 2: IR -> sol2 .gen.cpp + the generated LuaLS stub sections
python tools/python/gen_lua.py
```

Both run automatically via the `lua-codegen` xmake target (best-effort,
stamp-guarded). The IR -- not any one generated file -- is the single source of
truth; the LuaLS stub is itself a backend output, so it cannot drift from the
bound surface. A future Python / C# backend reads the same IR without re-running
the C++ extraction. The frontend enforces the naming rules above at extraction
time.

An **area** = one `registerScript<Area>Bindings` + one `.gen.cpp` (the existing
hand-written registrar, so the generated file is a drop-in). Areas aggregate
everything tagged with `area=`: namespace tables, usertypes, structs, enums, raw
hooks. The annotation vocabulary:

* **Modules** (`VBIND_MODULE` + `VBIND_FN`) -- namespace tables. Two body kinds:
  `serviceForward` (annotate a service interface directly; the generator emits
  the whole body `ctx.<svc>->method(args)` with a service null-check + `isValid`
  guard for entity params -- no hand-written code) and `shimCall`
  (`VBIND_FN(... body = shim)` on a `namespace vultra` free function taking
  `ScriptContext&` first; the generator forwards, the hand-written shim body owns
  irregular glue: options tables, table building, multi-return, UUID-or-uri).
* **Usertypes** (`VBIND_USERTYPE` + `VBIND_PROPERTY` getters/setters +
  `VBIND_FN(usertype=...)` methods) -- entity-ref handle usertypes. `component=`
  auto-emits `valid`; `postRegister=` runs a hook after registration (e.g. the
  generated component accessors on `Entity`).
* **Enums** (`VBIND_ENUM`) -- enumerators read directly, never hand-listed.
* **Value structs** (`VBIND_STRUCT` + `VBIND_FIELD`, or `allFields` to bind every
  public field) -- subsume the legacy `VLUA_CLASS`/`VLUA_FIELD` component pattern.
* **Raw hooks** (`VBIND_RAW` on a `void f(sol::state&, ScriptContext&)`) -- the
  escape hatch for irreducibly-sol2 registration that can't be expressed
  declaratively: constant tables (`Layer`), `meta_function` operators (`Math`
  Vec types), and signal connect/dispatch machinery (`UI`). The area registrar
  calls the hook; the body is hand-written. **Only the body, never the surface.**

Renames use `VBIND_FIELD(name = newName, deprecated = oldName)` (or the matching
option on `VBIND_FN`/`VBIND_PROPERTY`), which also emits the warn-once alias.

**Status:** everything runs through this one pipeline. Every API-surface
subsystem is migrated -- Input, Time, Script, Scene, Asset, Upscaler, Audio,
Render, Animation, Transform, Entity, World, Physics, Math, UI, Editor -- and the
pure-data **components** (Camera/Light/shapes/audio/probe/particle) too: their
field bindings (`requireComponentRef` get/set on the typed ref handle) are
generated with no shim. Components are not exposed as per-entity properties;
they are reached through the generic `entity:getComponent/addComponent/
removeComponent/hasComponent(Component.X)` API (see "Entity component access"
below), keyed by the `Component` enum token. The legacy
`gen_lua_bindings.py` was retired; the extractor reads both `VBIND_*` and the
components' existing `VLUA_CLASS`/`VLUA_FIELD` (mapping `ref=`->`handle=`). Only
the coroutine runtime + deprecation-alias registry (engine machinery, not API
surface) remain hand-written. ImGui is generated by its own `gen_imgui_lua.py`
from dear_bindings metadata (not the VBIND_* IR pipeline). The
conformance test + `exceptions.lua` burn-down proved parity at each step
(303 -> 15 baselined; the remaining are `Layer`/`UI` raw-hook surfaces, which a
future stub backend for raw hooks can cover).

## Review checklist

Reviewers (human or AI) of any binding change verify:

- [ ] Names follow section 1 (PascalCase types/tables/enums, camelCase members,
      `isX/hasX/canX` booleans, `onX` signals).
- [ ] Parameterless cheap accessors are properties, not `getX()/setX()`.
- [ ] Units: meters/seconds/degrees unsuffixed; everything else suffixed.
- [ ] Failure style matches section 4 and is consistent within the module.
- [ ] >3 params or 2+ optionals use an options table.
- [ ] No new duplicate aliases; renames go through the deprecation shim.
- [ ] Stub (`tools/lua-stubs/vultra.lua`) updated; `doc/lua_scripting.md`
      updated; conformance test green without growing `exceptions.lua`.
