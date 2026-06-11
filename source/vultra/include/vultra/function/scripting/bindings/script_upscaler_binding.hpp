#pragma once

#include <sol/forward.hpp>

namespace vultra
{
    struct ScriptContext;

    void registerScriptUpscalerBindings(sol::state& lua, ScriptContext& ctx);
} // namespace vultra
