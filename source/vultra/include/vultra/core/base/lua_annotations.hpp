#pragma once

// Lua binding annotations consumed by tools/python/gen_lua_bindings.py.
// They expand to nothing in normal builds; the generator parses headers with
// -DVULTRA_BINDGEN (clang) where they become [[clang::annotate]] attributes.
//
// Usage:
//   struct VLUA_CLASS(name=Camera, ref=ScriptCameraRef, accessor=camera) CameraComponent
//   {
//       VLUA_FIELD() bool primary {false};
//       VLUA_FIELD(name=fovY, deprecated=fovYDegrees) float fovYDegrees {60.0f};
//       VLUA_FIELD(readonly) uint32_t internalState {0};
//   };
//
// Class options:
//   name=      Lua usertype name, PascalCase (e.g. Light -> the Light type)
//   ref=       script ref struct from script_types.hpp holding
//              `entt::entity entity` (e.g. ScriptLightRef)
//   accessor=  optional; entity property that returns the ref
//              (accessor=light -> entity.light) and the matching
//              entity:hasLight() query (capitalized accessor name)
// Field options:
//   name=        Lua property name, camelCase; defaults to the C++ field name
//   deprecated=  also bind this old name with a one-release warn-once shim
//   readonly     property has no setter
//
// Supported field types: bool, int, unsigned int, float, double, glm::vec2/3/4
// (-> Vec2/3/4), std::string, CoreUUID (<-> string). The generator enforces
// doc/lua_api_design.md naming rules at generation time and refuses
// non-conformant names. It emits both the usertype data bindings
// (script_components_binding.gen.cpp) and the entity accessors, so the C++
// component and the Lua surface stay in lockstep from one source.

#if defined(VULTRA_BINDGEN)
#define VLUA_CLASS(...) [[clang::annotate("vlua_class:" #__VA_ARGS__)]]
#define VLUA_FIELD(...) [[clang::annotate("vlua_field:" #__VA_ARGS__)]]
#else
#define VLUA_CLASS(...)
#define VLUA_FIELD(...)
#endif
