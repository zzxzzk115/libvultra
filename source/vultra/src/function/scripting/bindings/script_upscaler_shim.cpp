#include "vultra/function/scripting/bindings/script_upscaler_shim.hpp"

#include "vultra/function/services/render_upscaler_service.hpp"

namespace vultra
{
    namespace
    {
        IRenderUpscalerService* service(ScriptContext& ctx) { return ctx.renderUpscalerService; }
    } // namespace

    sol::table upscalerProviders(ScriptContext& ctx, sol::this_state state)
    {
        sol::state_view luaState(state);
        sol::table      out = luaState.create_table();
        auto*           svc = service(ctx);
        if (svc == nullptr)
            return out;

        int index = 1;
        for (const auto& name : svc->providers())
            out[index++] = name;
        return out;
    }

    sol::object upscalerActive(ScriptContext& ctx, sol::this_state state)
    {
        sol::state_view luaState(state);
        auto*           svc = service(ctx);
        if (svc == nullptr || svc->activeProvider() == nullptr)
            return sol::make_object(luaState, sol::lua_nil);
        return sol::make_object(luaState, std::string(svc->activeProvider()->name()));
    }

    bool upscalerSetActive(ScriptContext& ctx, const std::string& name)
    {
        auto* svc = service(ctx);
        return svc != nullptr && svc->setActiveProvider(name);
    }

    void upscalerSetEnabled(ScriptContext& ctx, bool enabled)
    {
        if (auto* svc = service(ctx))
            svc->setEnabled(enabled);
    }

    void upscalerSetMode(ScriptContext& ctx, const std::string& mode)
    {
        if (auto* svc = service(ctx))
            svc->setMode(upscalerModeFromName(mode));
    }

    sol::table upscalerStatus(ScriptContext& ctx, sol::this_state state)
    {
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
    }
} // namespace vultra
