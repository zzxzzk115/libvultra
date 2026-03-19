#include "vultra/function/scripting/bindings/script_math_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"

namespace vultra
{
    void registerScriptMathBindings(sol::state& lua)
    {
        lua.new_usertype<ScriptVec2>("Vec2",
                                     sol::constructors<ScriptVec2(), ScriptVec2(float, float)>(),
                                     "x",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec2& v) { return v.x; },
                                                         [](ScriptVec2& v, float value) { v.x = value; }),
                                     "y",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec2& v) { return v.y; },
                                                         [](ScriptVec2& v, float value) { v.y = value; }));

        lua.new_usertype<ScriptVec3>("Vec3",
                                     sol::constructors<ScriptVec3(), ScriptVec3(float, float, float)>(),
                                     "x",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec3& v) { return v.x; },
                                                         [](ScriptVec3& v, float value) { v.x = value; }),
                                     "y",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec3& v) { return v.y; },
                                                         [](ScriptVec3& v, float value) { v.y = value; }),
                                     "z",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec3& v) { return v.z; },
                                                         [](ScriptVec3& v, float value) { v.z = value; }));

        lua.set_function("vec2", [](float x, float y) { return ScriptVec2 {x, y}; });
        lua.set_function("vec3", [](float x, float y, float z) { return ScriptVec3 {x, y, z}; });
    }
} // namespace vultra
