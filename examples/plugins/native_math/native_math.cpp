// Native (C++) example plugin.
//
// Demonstrates the "glue layer" pattern: a native shared library registers a C++ function into the
// engine's shared Lua state, so Lua plugins and ordinary entity scripts can call it. Here it is a
// trivial math helper, but the same shape wraps any third-party C/C++ SDK and exposes a Lua API.
//
// The plugin only touches the host through the stable EnginePlugin vtable and the service registry
// (which is keyed by service name, so the lookup resolves across the DLL boundary). It deliberately
// avoids the engine's logging/global state and uses stdio instead.

#include <vultra/core/engine/engine_context.hpp>
#include <vultra/core/plugin/engine_plugin.hpp>
#include <vultra/function/services/script_service.hpp>

#include <sol/sol.hpp>

#include <cmath>
#include <cstdio>

namespace
{
    class NativeMathPlugin final : public vultra::EnginePlugin
    {
    public:
        const char* name() const override { return "native_math"; }

        bool install(vultra::EngineContext& ctx) override
        {
            auto* script = ctx.services.tryGet<vultra::IScriptService>();
            if (script == nullptr || script->luaState() == nullptr)
            {
                std::fprintf(stderr, "[native_math] scripting runtime unavailable\n");
                return false;
            }
            sol::state_view lua(script->luaState());
            auto            ns = lua["native_math"].get_or_create<sol::table>();
            ns.set_function("length3",
                            [](double x, double y, double z) { return std::sqrt(x * x + y * y + z * z); });
            std::printf("[native_math] registered native_math.length3()\n");
            return true;
        }

        void uninstall(vultra::EngineContext& ctx) override
        {
            auto* script = ctx.services.tryGet<vultra::IScriptService>();
            if (script != nullptr && script->luaState() != nullptr)
            {
                sol::state_view lua(script->luaState());
                lua["native_math"] = sol::lua_nil;
            }
            std::printf("[native_math] uninstalled\n");
        }
    };
} // namespace

#if defined(_WIN32)
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PLUGIN_EXPORT unsigned int          vultraPluginAbiVersion() { return vultra::kEnginePluginAbiVersion; }
PLUGIN_EXPORT vultra::EnginePlugin* vultraCreatePlugin() { return new NativeMathPlugin(); }
PLUGIN_EXPORT void                  vultraDestroyPlugin(vultra::EnginePlugin* p) { delete p; }
