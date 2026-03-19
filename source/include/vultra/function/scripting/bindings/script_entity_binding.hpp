#pragma once

#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    void registerScriptEntityBindings(sol::state& lua, ScriptContext& ctx);
}
