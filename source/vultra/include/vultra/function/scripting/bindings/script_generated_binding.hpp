#pragma once

#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    // Implemented by script_components_binding.gen.cpp, which is produced by
    // tools/python/gen_lua_bindings.py from VLUA_*-annotated component
    // headers. Regenerate after changing annotations; the generated file is
    // checked in (no build-time Python dependency).
    void registerGeneratedComponentBindings(sol::state& lua, ScriptContext& ctx);

    // Adds the generated entity.<accessor> properties and entity:has<Name>()
    // queries onto the Entity usertype. Called by script_entity_binding.cpp
    // right after it creates the usertype, so the component accessors stay in
    // lockstep with the annotated components.
    void applyGeneratedEntityAccessors(sol::usertype<ScriptEntity>& entityType, ScriptContext& ctx);
} // namespace vultra
