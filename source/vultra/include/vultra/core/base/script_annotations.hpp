#pragma once

// Language-neutral script-binding annotations consumed by the binding
// generator (tools/python/extract_bindings.py). They expand to nothing in
// normal builds; the extractor parses headers with -DVULTRA_BINDGEN (clang),
// where they become [[clang::annotate("vbind_<kind>:...")]] attributes that
// libclang reads.
//
// Stage 1 (extract_bindings.py) turns these into a language-neutral IR
// (tools/bindings/ir/bindings.ir.json). Stage 2 backends emit per-language
// bindings from that IR (sol2 registration + LuaLS stub today; Python / C#
// later). The IR -- not any one generated file -- is the single source of
// truth.
//
// ------------------------------------------------------------------ vocabulary
//
// VBIND_MODULE(name=Input, service=inputService)
//   On a class/struct (typically a service interface). Declares a Lua namespace
//   table. `service=` names the ScriptContext pointer member the generated
//   wrappers null-check before dispatching (registration never dereferences it).
//   Annotated VBIND_FN methods of the class become the table's functions, with
//   the call forwarded straight to the service (bodyKind: serviceForward).
//
// VBIND_USERTYPE(name=RigidBody, handle=ScriptRigidBodyRef, component=RigidBodyComponent, accessor=rigidBody, service=physicsService)
//   On a struct/class. Declares a usertype (`:` call form). `handle=` is the
//   thin entity-ref struct from script_types.hpp. `component=`/`accessor=` are
//   optional (auto `valid` + entity.<accessor>/entity:has<Name>()).
//
// VBIND_STRUCT(name=PhysicsRaycastHit)
//   On a plain value struct (not entity-backed). VBIND_FIELD members are bound.
//
// VBIND_ENUM(name=KeyCode, stripE)
//   On an enum. The generator reads the enumerators directly (no hand list).
//   `stripE` drops a leading `e` from `eSpace` -> `Space` (see normalizeEnumName).
//
// VBIND_FIELD(name=fovY, deprecated=fovYDegrees, readonly)
//   On a data member of a VBIND_USERTYPE/VBIND_STRUCT. For a component
//   usertype, the generator emits requireComponentRef get/set per field.
//
// VBIND_PROPERTY(name=deltaTime, setter=setDeltaTime, readonly, service=timingService, deprecated=oldName)
//   On a shim getter; pairs a property get/set over a namespace or usertype.
//
// VBIND_FN(name=raycast, self=ScriptRigidBodyRef, service=physicsService, null=..., returns=value|optionalNil|okErr|valueErr, overload=group, deprecated=oldName, body=shim)
//   On a free shim function OR a service method. `self=` (a handle) makes it a
//   usertype method instead of a namespace function. `service=` + `null=` drive
//   the null-service guard (`null=` is inferred from the return type when
//   omitted: bool->false, int/uint->0, float->0.f, vecN->ScriptVecN{}). `body=shim`
//   marks an irregular hand-written shim body the generator calls instead of
//   forwarding to the service.
//
// VBIND_RAW(area=world)
//   Escape hatch for irregular registration that can't be expressed
//   declaratively (constant tables, sol2 meta_function operators, stateful
//   signal connect/dispatch machinery). On a `void f(sol::state&,
//   ScriptContext&)` function; the area's generated registrar calls it.

#if defined(VULTRA_BINDGEN)
#define VBIND_MODULE(...) [[clang::annotate("vbind_module:" #__VA_ARGS__)]]
#define VBIND_USERTYPE(...) [[clang::annotate("vbind_usertype:" #__VA_ARGS__)]]
#define VBIND_STRUCT(...) [[clang::annotate("vbind_struct:" #__VA_ARGS__)]]
#define VBIND_ENUM(...) [[clang::annotate("vbind_enum:" #__VA_ARGS__)]]
#define VBIND_FIELD(...) [[clang::annotate("vbind_field:" #__VA_ARGS__)]]
#define VBIND_PROPERTY(...) [[clang::annotate("vbind_property:" #__VA_ARGS__)]]
#define VBIND_FN(...) [[clang::annotate("vbind_fn:" #__VA_ARGS__)]]
#define VBIND_RAW(...) [[clang::annotate("vbind_raw:" #__VA_ARGS__)]]
#else
#define VBIND_MODULE(...)
#define VBIND_USERTYPE(...)
#define VBIND_STRUCT(...)
#define VBIND_ENUM(...)
#define VBIND_FIELD(...)
#define VBIND_PROPERTY(...)
#define VBIND_FN(...)
#define VBIND_RAW(...)
#endif
