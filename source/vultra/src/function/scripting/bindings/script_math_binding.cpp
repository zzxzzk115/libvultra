#include "vultra/function/scripting/bindings/script_math_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    namespace
    {
        ScriptVec2 add(const ScriptVec2& a, const ScriptVec2& b) { return {a.x + b.x, a.y + b.y}; }
        ScriptVec2 sub(const ScriptVec2& a, const ScriptVec2& b) { return {a.x - b.x, a.y - b.y}; }
        ScriptVec2 neg(const ScriptVec2& v) { return {-v.x, -v.y}; }
        ScriptVec2 mul(const ScriptVec2& v, float s) { return {v.x * s, v.y * s}; }
        ScriptVec2 mul(float s, const ScriptVec2& v) { return mul(v, s); }
        ScriptVec2 div(const ScriptVec2& v, float s) { return s != 0.0f ? ScriptVec2 {v.x / s, v.y / s} : ScriptVec2 {}; }

        ScriptVec3 add(const ScriptVec3& a, const ScriptVec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
        ScriptVec3 sub(const ScriptVec3& a, const ScriptVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
        ScriptVec3 neg(const ScriptVec3& v) { return {-v.x, -v.y, -v.z}; }
        ScriptVec3 mul(const ScriptVec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
        ScriptVec3 mul(float s, const ScriptVec3& v) { return mul(v, s); }
        ScriptVec3 div(const ScriptVec3& v, float s)
        {
            return s != 0.0f ? ScriptVec3 {v.x / s, v.y / s, v.z / s} : ScriptVec3 {};
        }

        ScriptVec4 add(const ScriptVec4& a, const ScriptVec4& b)
        {
            return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
        }
        ScriptVec4 sub(const ScriptVec4& a, const ScriptVec4& b)
        {
            return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
        }
        ScriptVec4 neg(const ScriptVec4& v) { return {-v.x, -v.y, -v.z, -v.w}; }
        ScriptVec4 mul(const ScriptVec4& v, float s) { return {v.x * s, v.y * s, v.z * s, v.w * s}; }
        ScriptVec4 mul(float s, const ScriptVec4& v) { return mul(v, s); }
        ScriptVec4 div(const ScriptVec4& v, float s)
        {
            return s != 0.0f ? ScriptVec4 {v.x / s, v.y / s, v.z / s, v.w / s} : ScriptVec4 {};
        }

        float dot(const ScriptVec2& a, const ScriptVec2& b) { return a.x * b.x + a.y * b.y; }
        float dot(const ScriptVec3& a, const ScriptVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        float dot(const ScriptVec4& a, const ScriptVec4& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        }

        float lengthSquared(const ScriptVec2& v) { return dot(v, v); }
        float lengthSquared(const ScriptVec3& v) { return dot(v, v); }
        float lengthSquared(const ScriptVec4& v) { return dot(v, v); }
    } // namespace

    void registerScriptMathBindings(sol::state& lua)
    {
        lua.new_usertype<ScriptVec2>("Vec2",
                                     sol::constructors<ScriptVec2(), ScriptVec2(float, float)>(),
                                     sol::call_constructor,
                                     sol::constructors<ScriptVec2(), ScriptVec2(float, float)>(),
                                     sol::meta_function::addition,
                                     sol::resolve<ScriptVec2(const ScriptVec2&, const ScriptVec2&)>(&add),
                                     sol::meta_function::subtraction,
                                     sol::resolve<ScriptVec2(const ScriptVec2&, const ScriptVec2&)>(&sub),
                                     sol::meta_function::unary_minus,
                                     sol::resolve<ScriptVec2(const ScriptVec2&)>(&neg),
                                     sol::meta_function::multiplication,
                                     sol::overload(sol::resolve<ScriptVec2(const ScriptVec2&, float)>(&mul),
                                                   sol::resolve<ScriptVec2(float, const ScriptVec2&)>(&mul)),
                                     sol::meta_function::division,
                                     sol::resolve<ScriptVec2(const ScriptVec2&, float)>(&div),
                                     "x",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec2& v) { return v.x; },
                                                         [](ScriptVec2& v, float value) { v.x = value; }),
                                     "y",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec2& v) { return v.y; },
                                                         [](ScriptVec2& v, float value) { v.y = value; }));

        lua.new_usertype<ScriptVec3>("Vec3",
                                     sol::constructors<ScriptVec3(), ScriptVec3(float, float, float)>(),
                                     sol::call_constructor,
                                     sol::constructors<ScriptVec3(), ScriptVec3(float, float, float)>(),
                                     sol::meta_function::addition,
                                     sol::resolve<ScriptVec3(const ScriptVec3&, const ScriptVec3&)>(&add),
                                     sol::meta_function::subtraction,
                                     sol::resolve<ScriptVec3(const ScriptVec3&, const ScriptVec3&)>(&sub),
                                     sol::meta_function::unary_minus,
                                     sol::resolve<ScriptVec3(const ScriptVec3&)>(&neg),
                                     sol::meta_function::multiplication,
                                     sol::overload(sol::resolve<ScriptVec3(const ScriptVec3&, float)>(&mul),
                                                   sol::resolve<ScriptVec3(float, const ScriptVec3&)>(&mul)),
                                     sol::meta_function::division,
                                     sol::resolve<ScriptVec3(const ScriptVec3&, float)>(&div),
                                     "x",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec3& v) { return v.x; },
                                                         [](ScriptVec3& v, float value) { v.x = value; }),
                                     "y",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec3& v) { return v.y; },
                                                         [](ScriptVec3& v, float value) { v.y = value; }),
                                     "z",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec3& v) { return v.z; },
                                                         [](ScriptVec3& v, float value) { v.z = value; }));

        lua.new_usertype<ScriptVec4>("Vec4",
                                     sol::constructors<ScriptVec4(), ScriptVec4(float, float, float, float)>(),
                                     sol::call_constructor,
                                     sol::constructors<ScriptVec4(), ScriptVec4(float, float, float, float)>(),
                                     sol::meta_function::addition,
                                     sol::resolve<ScriptVec4(const ScriptVec4&, const ScriptVec4&)>(&add),
                                     sol::meta_function::subtraction,
                                     sol::resolve<ScriptVec4(const ScriptVec4&, const ScriptVec4&)>(&sub),
                                     sol::meta_function::unary_minus,
                                     sol::resolve<ScriptVec4(const ScriptVec4&)>(&neg),
                                     sol::meta_function::multiplication,
                                     sol::overload(sol::resolve<ScriptVec4(const ScriptVec4&, float)>(&mul),
                                                   sol::resolve<ScriptVec4(float, const ScriptVec4&)>(&mul)),
                                     sol::meta_function::division,
                                     sol::resolve<ScriptVec4(const ScriptVec4&, float)>(&div),
                                     "x",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec4& v) { return v.x; },
                                                         [](ScriptVec4& v, float value) { v.x = value; }),
                                     "y",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec4& v) { return v.y; },
                                                         [](ScriptVec4& v, float value) { v.y = value; }),
                                     "z",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec4& v) { return v.z; },
                                                         [](ScriptVec4& v, float value) { v.z = value; }),
                                     "w",
                                     VULTRA_LUA_PROPERTY([](const ScriptVec4& v) { return v.w; },
                                                         [](ScriptVec4& v, float value) { v.w = value; }));

        // vec2/vec3/vec4 lowercase constructors are deprecated aliases of the
        // callable Vec2/Vec3/Vec4 type tables, installed by
        // registerScriptDeprecations.
        lua.set_function("dot", sol::overload(sol::resolve<float(const ScriptVec2&, const ScriptVec2&)>(&dot),
                                              sol::resolve<float(const ScriptVec3&, const ScriptVec3&)>(&dot),
                                              sol::resolve<float(const ScriptVec4&, const ScriptVec4&)>(&dot)));
        lua.set_function("lengthSquared",
                         sol::overload(sol::resolve<float(const ScriptVec2&)>(&lengthSquared),
                                       sol::resolve<float(const ScriptVec3&)>(&lengthSquared),
                                       sol::resolve<float(const ScriptVec4&)>(&lengthSquared)));
    }
} // namespace vultra
