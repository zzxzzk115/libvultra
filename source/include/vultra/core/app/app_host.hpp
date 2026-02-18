#pragma once

#include <vultra/core/base/api.hpp>
#include <vultra/core/engine/engine.hpp>

#include <chrono>

namespace vultra
{
    // ------------------------------------------------------------
    // AppHost
    // ------------------------------------------------------------
    // Platform/application host that owns an Engine instance.
    // You can adapt this to your existing BaseApp / XRApp / ImGuiApp.
    //
    // Design goals:
    // - Keep platform/window/event loop outside the engine core.
    // - Apps configure the engine by enabling features and registering systems.
    // ------------------------------------------------------------
    class VULTRA_API AppHost
    {
    public:
        AppHost()          = default;
        virtual ~AppHost() = default;

        AppHost(const AppHost&)            = delete;
        AppHost& operator=(const AppHost&) = delete;
        AppHost(AppHost&&)                 = delete;
        AppHost& operator=(AppHost&&)      = delete;

        int run();

    protected:
        // App configuration entry point
        virtual void onConfigure(Engine& engine) = 0;

        // Platform loop hooks
        virtual void onPollEvents() = 0;
        virtual bool onShouldClose() const = 0;

        // If you already have your own timestep controller, override these.
        virtual fsec onFrameDelta() { return fsec{1.0f / 60.0f}; }

        // Optional
        virtual void onBeforeEngineTick(fsec /*dt*/) {}
        virtual void onAfterEngineTick(fsec /*dt*/) {}

    protected:
        Engine m_Engine;
    };
}
