#pragma once

#include <vultra/core/base/api.hpp>
#include <vultra/core/engine/engine.hpp>

#include <span>
#include <string>
#include <vector>

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
        int run(int argc, char** argv);

    protected:
        // App configuration entry point
        virtual void onConfigure(Engine& engine) = 0;
        virtual void onPostConfigure(Engine& engine) {}

        // Platform loop hooks
        virtual void onPollEvents()        = 0;
        virtual bool onShouldClose() const = 0;

        virtual fsec onFrameDelta();

        // Optional
        virtual void onBeforeEngineTick(fsec /*dt*/) {}
        virtual void onAfterEngineTick(fsec /*dt*/) {}

        EngineContext&       engineCtx() { return m_Engine.ctx(); }
        const EngineContext& engineCtx() const { return m_Engine.ctx(); }
        std::span<const std::string> commandLineArgs() const { return m_CommandLineArgs; }

    protected:
        Engine m_Engine;
        std::vector<std::string> m_CommandLineArgs;
    };
} // namespace vultra
