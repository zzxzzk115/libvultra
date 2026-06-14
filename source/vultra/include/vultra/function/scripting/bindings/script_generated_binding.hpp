#pragma once

#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    // Adds the generated entity.<accessor> properties and entity:has<Name>()
    // queries onto the Entity usertype. Defined in script_components_binding.gen.cpp
    // (IR pipeline); called from the Entity usertype's postRegister hook so the
    // component accessors stay in lockstep with the annotated components.
    void applyGeneratedEntityAccessors(sol::usertype<ScriptEntity>& entityType, ScriptContext& ctx);
} // namespace vultra
