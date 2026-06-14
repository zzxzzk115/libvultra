#pragma once

// The `math` area is registered via a raw hook: the Vec2/Vec3/Vec4 usertypes use
// sol2 constructors + meta_function operators + sol::resolve/overload, which are
// irreducibly sol2-idiomatic (the documented non-generatable case). The IR
// pipeline still owns the registrar shell (script_math_binding.gen.cpp calls
// this hook); the body lives in script_math_shim.cpp.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    VBIND_RAW(area = math)
    void mathRegisterTypes(sol::state& lua, ScriptContext& ctx);
} // namespace vultra
