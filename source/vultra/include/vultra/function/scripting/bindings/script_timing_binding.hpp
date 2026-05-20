#pragma once

#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    void registerScriptTimingBindings(sol::state& lua, ScriptContext& ctx);
}
