#pragma once

// Shim declarations for the Lua `Upscaler` namespace. The IR pipeline generates
// the sol2 registration (script_upscaler_binding.gen.cpp) + stub; the bodies in
// script_upscaler_shim.cpp own the table building and provider/mode marshalling.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

#include <string>

namespace vultra
{
    struct VBIND_MODULE(name = Upscaler, service = renderUpscalerService) UpscalerModule
    {
    };

    VBIND_FN(module = Upscaler, name = providers, body = shim)
    sol::table upscalerProviders(ScriptContext& ctx, sol::this_state state);

    VBIND_FN(module = Upscaler, name = active, body = shim)
    sol::object upscalerActive(ScriptContext& ctx, sol::this_state state);

    VBIND_FN(module = Upscaler, name = setActive, body = shim)
    bool upscalerSetActive(ScriptContext& ctx, const std::string& name);

    VBIND_FN(module = Upscaler, name = setEnabled, body = shim)
    void upscalerSetEnabled(ScriptContext& ctx, bool enabled);

    VBIND_FN(module = Upscaler, name = setMode, body = shim)
    void upscalerSetMode(ScriptContext& ctx, const std::string& mode);

    VBIND_FN(module = Upscaler, name = status, body = shim)
    sol::table upscalerStatus(ScriptContext& ctx, sol::this_state state);
} // namespace vultra
