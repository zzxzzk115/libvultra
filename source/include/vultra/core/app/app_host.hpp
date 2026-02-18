#pragma once

#include <vultra/core/base/api.hpp>
#include <vultra/core/engine/engine.hpp>

namespace vultra
{
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
        virtual void onPollEvents()        = 0;
        virtual bool onShouldClose() const = 0;

        virtual fsec onFrameDelta() { return fsec {1.0f / 60.0f}; }

        // Optional
        virtual void onBeforeEngineTick(fsec /*dt*/) {}
        virtual void onAfterEngineTick(fsec /*dt*/) {}

        EngineContext&       engineCtx() { return m_Engine.ctx(); }
        const EngineContext& engineCtx() const { return m_Engine.ctx(); }

    protected:
        Engine m_Engine;
    };
} // namespace vultra
