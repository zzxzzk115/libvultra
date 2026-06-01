#pragma once

#include "vultra/function/scripting/script_context.hpp"

#include <sol/forward.hpp>

namespace vultra
{
    void registerScriptAnimationBindings(sol::state& lua, ScriptContext& ctx);
}
