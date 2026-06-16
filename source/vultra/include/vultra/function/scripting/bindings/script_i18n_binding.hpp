#pragma once

#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    void registerScriptI18nBindings(sol::state& lua, ScriptContext& ctx);
}
