#pragma once

#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    void registerScriptUiBindings(sol::state& lua, ScriptContext& ctx);
    void dispatchScriptUiSignals(sol::state& lua, ScriptContext& ctx);
    void clearScriptUiSignalConnections();
    void clearScriptUiSignalConnections(entt::entity entity);
}
