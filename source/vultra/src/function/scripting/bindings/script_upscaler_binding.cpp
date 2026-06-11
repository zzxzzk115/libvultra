#include "vultra/function/scripting/bindings/script_upscaler_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/services/render_upscaler_service.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    namespace
    {
        IRenderUpscalerService* service(ScriptContext& ctx) { return ctx.renderUpscalerService; }
    } // namespace

    void registerScriptUpscalerBindings(sol::state& lua, ScriptContext& ctx)
    {
        auto upscaler = script_binding::getOrCreateTable(lua, "Upscaler");

        upscaler.set_function("providers", [&ctx](sol::this_state state) {
            sol::state_view luaState(state);
            sol::table      out = luaState.create_table();
            auto*           svc = service(ctx);
            if (svc == nullptr)
                return out;

            int index = 1;
            for (const auto& name : svc->providers())
                out[index++] = name;
            return out;
        });

        upscaler.set_function("active", [&ctx](sol::this_state state) -> sol::object {
            sol::state_view luaState(state);
            auto* svc = service(ctx);
            if (svc == nullptr || svc->activeProvider() == nullptr)
                return sol::make_object(luaState, sol::lua_nil);
            return sol::make_object(luaState, std::string(svc->activeProvider()->name()));
        });

        upscaler.set_function("setActive", [&ctx](const std::string& name) {
            auto* svc = service(ctx);
            return svc != nullptr && svc->setActiveProvider(name);
        });

        upscaler.set_function("setEnabled", [&ctx](const bool enabled) {
            if (auto* svc = service(ctx))
                svc->setEnabled(enabled);
        });

        upscaler.set_function("setMode", [&ctx](const std::string& mode) {
            if (auto* svc = service(ctx))
                svc->setMode(upscalerModeFromName(mode));
        });

        upscaler.set_function("status", [&ctx](sol::this_state state) {
            sol::state_view luaState(state);
            sol::table      out = luaState.create_table();
            auto*           svc = service(ctx);
            if (svc == nullptr)
            {
                out["available"] = false;
                out["active"]    = "";
                out["message"]   = "Upscaler service unavailable";
                out["enabled"]   = false;
                out["mode"]      = "off";
                return out;
            }

            const auto status   = svc->status();
            const auto settings = svc->settings();
            out["available"]    = status.available;
            out["active"]       = status.activeProvider;
            out["message"]      = status.message;
            out["enabled"]      = settings.enabled;
            out["mode"]         = std::string(upscalerModeName(settings.mode));
            out["outputWidth"]  = settings.outputExtent.width;
            out["outputHeight"] = settings.outputExtent.height;
            out["hdr"]          = settings.hdr;
            out["autoExposure"] = settings.autoExposure;
            return out;
        });
    }
} // namespace vultra
