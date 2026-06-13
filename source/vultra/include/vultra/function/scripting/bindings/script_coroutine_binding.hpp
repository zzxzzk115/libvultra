#pragma once

#include <sol/sol.hpp>

namespace vultra
{
    // Installs the pure-Lua coroutine scheduler for entity scripts:
    // __vultraCoroutines (bindEnv/stopAll/tick/reset) plus the global
    // wait/waitFrames yield helpers and error-raising global fallbacks for
    // startCoroutine/stopAllCoroutines. ScriptSystem binds the per-entity
    // functions into each script environment and drives tick(dt).
    // Requires sol::lib::coroutine to be open on the state.
    void registerScriptCoroutineRuntime(sol::state& lua);
} // namespace vultra
