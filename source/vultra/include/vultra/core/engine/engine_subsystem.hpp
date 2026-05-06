#pragma once

#include "vultra/core/base/api.hpp"
#include "vultra/core/engine/engine_context.hpp"

#include <vbase/module/imodule.hpp>

#include <chrono>

namespace vultra
{
    using fsec = std::chrono::duration<float>;

#define ENGINE_SUBSYSTEM(x) \
    const char* name() const override { return #x; }

    class VULTRA_API EngineSubsystem : public vbase::IModule
    {
    public:
        EngineSubsystem()                                  = default;
        EngineSubsystem(const EngineSubsystem&)            = delete;
        EngineSubsystem& operator=(const EngineSubsystem&) = delete;
        EngineSubsystem(EngineSubsystem&&)                 = delete;
        EngineSubsystem& operator=(EngineSubsystem&&)      = delete;
        ~EngineSubsystem() override                        = default;

        void setContext(EngineContext& ctx) { m_Ctx = &ctx; }

        // vbase::IModule
        bool init() final { return onInit(); }
        void shutdown() final { onShutdown(); }

        // Extended engine lifecycle (called by FramePipeline)
        virtual void onPreUpdate(fsec /*dt*/) {}
        virtual void onUpdate(fsec /*dt*/) {}
        virtual void onPhysics(fsec /*dt*/) {}
        virtual void onPostUpdate(fsec /*dt*/) {}

        virtual void onPreRender() {}
        virtual void onRender() {}
        virtual void onPostRender() {}
        virtual void onPresent() {}

    protected:
        EngineContext& ctx() const { return *m_Ctx; }

        virtual bool onInit() { return true; }
        virtual void onShutdown() {}

    private:
        EngineContext* m_Ctx {nullptr}; // non-owning
    };
} // namespace vultra
