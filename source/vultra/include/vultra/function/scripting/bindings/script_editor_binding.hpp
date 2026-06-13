#pragma once

#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    // Binds the `Editor` table (registerPanel / registerMenuItem /
    // registerInspector / unregister) when an IEditorExtensionService is
    // present in the ScriptContext. In runtimes without an editor the table is
    // absent, so plugins guard with `if Editor then ... end`.
    void registerScriptEditorBindings(sol::state& lua, ScriptContext& ctx);
} // namespace vultra
