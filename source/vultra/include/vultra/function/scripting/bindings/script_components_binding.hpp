#pragma once

#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    // Generated (script_components_binding.gen.cpp): registers the pure-data
    // component-ref usertypes (Camera/Light/shapes/audio/etc.) from the VBIND_*
    // field annotations on the component structs.
    void registerScriptComponentsBindings(sol::state& lua, ScriptContext& ctx);
}
