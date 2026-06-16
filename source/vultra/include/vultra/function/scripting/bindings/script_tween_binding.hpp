#pragma once

#include <sol/forward.hpp>

namespace vultra
{
    // Registers the pure-Lua Tween / Timer / Timeline / Ease runtime (global tables) plus the
    // hidden `__vultraTween` driver. ScriptSystem advances it once per frame via tick(dt). This
    // is hand-written (not part of the VBIND IR pipeline), like the coroutine runtime.
    void registerScriptTweenRuntime(sol::state& lua);
} // namespace vultra
