#pragma once

#include <sol/sol.hpp>

namespace vultra
{
    // Installs warn-once aliases for renamed Lua APIs and publishes the
    // deprecation registry global `__vultraDeprecated` (old symbol ->
    // canonical symbol). Must run AFTER all other bindings are registered.
    // Policy: doc/lua_api_design.md section 7 -- aliases live for one release,
    // then the corresponding entries here are deleted.
    void registerScriptDeprecations(sol::state& lua);
} // namespace vultra
