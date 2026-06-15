# Script Binding Codegen — Authoring Standard

**English** | [简体中文](zh_CN/script_binding_codegen_CN.md)

This is the **normative guide** for adding or changing Lua script bindings in
libvultra. Bindings are **generated** from `VBIND_*` annotations through a
two-stage IR pipeline; you almost never hand-write registration code. Read this
before touching anything under `source/vultra/.../scripting/bindings/`.

Companion docs: [lua_api_design.md](lua_api_design.md) is the normative Lua API
*surface* spec (naming, properties-vs-methods, error modes). This doc is the
*codegen mechanics* — how to express a binding so the generator emits it.

---

## 1. Architecture (what runs)

```
annotated C++ headers  ──libclang──►  bindings.ir.json  ──backends──►  script_<area>_binding.gen.cpp
(VBIND_* / shim decls)   stage 1        (checked-in IR)     stage 2     tools/lua-stubs/vultra.lua (LuaLS stub)
```

- **Stage 1** `tools/python/extract_bindings.py` parses headers (compiled with
  `-DVULTRA_BINDGEN`, so `VBIND_*` become `[[clang::annotate]]`) into one
  checked-in IR: `tools/bindings/ir/bindings.ir.json` (schema:
  `tools/bindings/ir/bindings.ir.schema.json`).
- **Stage 2** `tools/python/gen_lua.py` runs backends (`tools/python/bindgen/
  backends/`): `lua_sol2` → the `.gen.cpp`; `lua_typestub` → the generated
  sections of the LuaLS stub.
- The **IR is the single source of truth**. The stub is a backend output, so it
  can never drift. A future Python/C# backend reads the same IR.
- Everything is **checked in** (IR + `.gen.cpp` + stub), so builds never need
  Python. `xmake build` regenerates automatically via the `lua-codegen` dep
  (best-effort, stamp-guarded); `xmake codegen` runs it on demand.

**Area-centric.** An *area* = one `registerScript<Area>Bindings(sol::state&,
ScriptContext&)` + one `script_<area>_binding.gen.cpp`. An area aggregates
everything tagged `area=`: namespace tables (modules), usertypes, value structs,
enums, raw hooks. The registrar name + header are the pre-existing hand-written
ones, so the generated file is a **drop-in** (`script_binding.cpp` is unchanged).

**Hard rules**
- **Never hand-edit a `*.gen.cpp`** — it is overwritten. Change the annotation.
- The engine API stays **Lua-agnostic** — no `sol::` types on `core/`/`function/`
  service interfaces. sol lives only in shim/`.gen.cpp` code.
- **Registration must never dereference a service** — the conformance test
  registers against an all-null `ScriptContext`. Service access happens inside
  the lambda body, guarded.

---

## 2. Decision guide — which annotation?

```
Adding a Lua function / type. Is it…

a namespace function (Input.isKeyHeld, Physics.raycast)?
├─ a clean 1:1 forward to a service method?      → VBIND_MODULE + VBIND_FN  (serviceForward — zero hand code)
└─ irregular (options table, builds a Lua table, ├─ → VBIND_FN(body=shim) + a hand-written shim fn
   multi-return, UUID-or-uri, service fallback)?

an entity-backed type (Transform, RigidBody, Animator, a component)?
├─ pure-data component (only fields)?            → VBIND_USERTYPE + VBIND_FIELD on the component struct (requireComponentRef — zero hand code)
└─ has methods / irregular props?                → VBIND_USERTYPE + VBIND_PROPERTY (shim get/set) + VBIND_FN(usertype=) methods

a plain value struct returned to Lua (RaycastHit)? → VBIND_STRUCT (+ allFields)
a C++ enum exposed as a table (KeyCode)?           → VBIND_ENUM
irreducibly sol2 (operators, signal machinery,     → VBIND_RAW on void f(sol::state&, ScriptContext&)
  constant tables, ad-hoc Lua tables)?
```

**Prefer the zero-hand-code paths** (serviceForward, component `VBIND_FIELD`).
Reach for a shim only when the glue genuinely can't be a clean C++ forward. Reach
for `VBIND_RAW` only for stateful machinery or irreducible sol2 idioms.

---

## 3. Annotation reference

All macros are in `vultra/core/base/script_annotations.hpp`. They expand to
nothing in normal builds.

| Macro | On | Purpose |
|---|---|---|
| `VBIND_MODULE(name=, area=, service=)` | a class/struct (a service interface, or a tag struct) | declares a `name` namespace table in `area`; `service=` is the null-checked `ScriptContext` member |
| `VBIND_FN(name=, body=shim, self=, service=, null=, deprecated=, module=, usertype=)` | a method of a `VBIND_MODULE` class, **or** a free shim function | a namespace function, or a usertype method when `usertype=` is given (`usertype=`, not `self=`, selects the method form; `self=` is an optional handle hint). Without `body=shim` on a service method → **serviceForward** (full body generated). `body=shim`/free function → forwards to a hand-written shim |
| `VBIND_USERTYPE(name=, handle=, area=, component=, accessor=, postRegister=)` | a struct/class (a component struct, or a tag struct) | entity-ref usertype. `handle=` ref struct; `component=` → auto `valid` + `requireComponentRef`; `accessor=` → `entity.<accessor>`/`entity:has<Name>()`; `postRegister=` → `fn(usertype&, ctx)` run after registration |
| `VBIND_FIELD(name=, readonly, deprecated=)` | a data member of a `VBIND_USERTYPE`/`VBIND_STRUCT` | a property; on a component usertype the generator emits `requireComponentRef` get/set |
| `VBIND_PROPERTY(usertype=, name=, set=)` | a getter shim function | a usertype property backed by hand-written getter/`set=` setter shims |
| `VBIND_STRUCT(name=, module=, allFields)` | a value struct | binds it as a sol2 usertype; `allFields` binds every public field |
| `VBIND_ENUM(name=, stripE, module=)` | a C++ enum | binds an enum table; `stripE` drops the leading `e` (`eSpace`→`Space`) |
| `VBIND_RAW(area=)` | `void f(sol::state&, ScriptContext&)` | escape hatch; the area registrar calls it; body hand-written |

**Shim convention.** A shim is a `namespace vultra` free function whose **first
parameter is `ScriptContext&`**; the rest are the script-facing params (for a
usertype method, the first script param is the `handle`). The generator forwards
`ctx` + params verbatim; the shim owns all glue (service null-check included).

**Type tokens / marshalling** (serviceForward + fields, handled automatically):
`bool int uint uint64 float double string`, `vec2/3/4` ↔ `ScriptVecN`,
`uuid` (CoreUUID ↔ string), `entity` (entt::entity ↔ ScriptEntity; unwraps
`.value` and adds an `isValid` guard), `enum` (↔ int). Shim params are passed as
their verbatim C++ type (so `sol::optional<...>`, `sol::table`, `sol::this_state`
work).

---

## 4. Recipes

### Namespace function, serviceForward (no hand code)
Annotate the service interface; the generator emits the whole body.
```cpp
class VBIND_MODULE(name = Input, service = inputService) IInputService {
    VBIND_FN() virtual bool isKeyHeld(KeyCode key) const = 0;   // Input.isKeyHeld(int)->bool
    VBIND_FN(null = 1.0f) virtual float timeScale() const = 0;  // null override; bool/0/0.f inferred otherwise
};
```

### Namespace function, shim (irregular glue)
`script_<area>_shim.hpp` (in the manifest) + `script_<area>_shim.cpp` (body):
```cpp
struct VBIND_MODULE(name = Asset, area = asset, service = assetService) AssetModule {};
VBIND_FN(module = Asset, name = loadText, body = shim)
ScriptTextAssetResult assetLoadText(ScriptContext& ctx, const std::string& uri);
```

### Entity-ref usertype with methods/props (shim-backed)
```cpp
struct VBIND_USERTYPE(name = RigidBody, handle = ScriptRigidBodyRef, area = physics,
                      component = RigidBodyComponent, accessor = rigidBody) RigidBodyUsertype {};
VBIND_PROPERTY(usertype = RigidBody, name = mass, set = rbSetMass)
float rbGetMass(ScriptContext& ctx, const ScriptRigidBodyRef& self);
void  rbSetMass(ScriptContext& ctx, const ScriptRigidBodyRef& self, float value);
VBIND_FN(usertype = RigidBody, name = addForce, body = shim)
bool rbAddForce(ScriptContext& ctx, const ScriptRigidBodyRef& self, const ScriptVec3& force);
```

### Pure-data component (no hand code)
On the component struct itself — fields forward via `requireComponentRef`:
```cpp
struct VBIND_USERTYPE(name = Light, handle = ScriptLightRef, accessor = light) LightComponent {
    VBIND_FIELD() float intensity {1.0f};
    VBIND_FIELD(deprecated = oldName) glm::vec3 color {1.0f};
};
```
Component usertypes default to the `components` area and auto-get `entity.light` /
`entity:hasLight()`.

### Value struct / enum
```cpp
struct VBIND_STRUCT(name = PhysicsRaycastHit, module = Physics, allFields) ScriptPhysicsRaycastHit { ... };
enum class VBIND_ENUM(name = KeyCode, stripE, module = Input) KeyCode : uint16_t { eUnknown, eA, ... };
```

### Raw hook (irreducible)
```cpp
VBIND_RAW(area = math) void mathRegisterTypes(sol::state& lua, ScriptContext& ctx);
```

---

## 5. Step-by-step: adding a binding

1. **Annotate.** Either annotate a clean service interface (serviceForward), or
   write `script_<area>_shim.hpp` (declarations) + `script_<area>_shim.cpp`
   (bodies) for shaped APIs. Reuse the existing `script_<area>_binding.hpp`
   registrar/header name so the `.gen.cpp` is a drop-in.
2. **Register the header** in `tools/bindings/headers.json`.
3. **Build** (`xmake build <target>`) — the `lua-codegen` dep regenerates the IR
   + `.gen.cpp` + stub. (Or `xmake codegen`.)
4. **If you replaced a hand-written `script_<area>_binding.cpp`, delete it** (the
   generated `.gen.cpp` now defines the same `registerScript<Area>Bindings`).
5. **Run conformance:** `xmake run test-lua-api-conformance`. It must PASS.
6. **Burn down exceptions:** the test reports `stale baseline entry` for any
   `stub.missing:*` the new stub now covers — delete those lines from
   `tests/lua_api_conformance/exceptions.lua`. The baseline may only shrink.

---

## 6. Conventions (the standard)

- **Naming** (enforced at extraction; also see lua_api_design.md §1): modules /
  usertypes / enums PascalCase; namespace functions camelCase; usertype members
  & struct fields camelCase (instance members aren't conformance-enumerated, so
  unit suffixes / deprecated aliases are allowed there).
- **One area per registrar.** Multiple namespaces may share an area (e.g.
  `Camera`/`Render`/`RenderBackend` all in `render`). Namespaces may even be
  split across areas (`getOrCreateTable` merges).
- **Drop-in registrars.** `script_binding.cpp` calls `registerScript<Area>Bindings`
  + the post-`registerScriptDeprecations` ordering; don't reorder. Generated
  registrars reuse those exact symbol/header names.
- **Renames / deprecations:** `VBIND_FIELD(name=new, deprecated=old)` (and the
  matching options on `VBIND_FN`/`VBIND_PROPERTY`) emit a warn-once alias.
- **Determinism:** generators are write-if-changed; re-running must be a no-op.
  CI runs `xmake codegen && git diff --exit-code` to catch un-regenerated output.

## 7. What stays hand-written (by design, not a gap)

- **Engine machinery, not API surface:** the coroutine runtime
  (`script_coroutine_binding.cpp`) and the deprecation-alias registry
  (`script_deprecations.cpp`, must run last).
- **ImGui:** its own generator (`tools/python/gen_imgui_lua.py`) from
  dear_bindings metadata.
- **Behind `VBIND_RAW`:** stateful signal connect/dispatch (UI), sol2
  `meta_function` operators (Math), constant tables (`Layer`). These are
  *implementations*, not per-binding shims.

## 8. Verification checklist (every binding change)

- [ ] `xmake codegen` regenerates with no manual `.gen.cpp` edits.
- [ ] `xmake run test-lua-api-conformance` → PASS; `exceptions.lua` only shrank.
- [ ] No new hand-written registrar (used the pipeline).
- [ ] Re-running codegen is a no-op (`git diff` clean).
- [ ] Engine service interfaces gained no `sol::` dependency.
