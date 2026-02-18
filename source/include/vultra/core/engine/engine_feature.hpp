#pragma once

#include "vultra/core/base/api.hpp"
#include "vultra/core/engine/engine_subsystem.hpp"

namespace vultra
{
    class FramePipeline;

    class VULTRA_API EngineFeature : public EngineSubsystem
    {
    public:
        EngineFeature()                                = default;
        EngineFeature(const EngineFeature&)            = delete;
        EngineFeature& operator=(const EngineFeature&) = delete;
        EngineFeature(EngineFeature&&)                 = delete;
        EngineFeature& operator=(EngineFeature&&)      = delete;
        ~EngineFeature() override                      = default;

        bool isEnabled() const { return m_Enabled; }

        // FeatureManager calls these
        bool enable();
        void disable();

        // Feature hooks (called by FramePipeline)
        virtual void onBeginFrame() {}
        virtual void onEndFrame() {}

    protected:
        // Called by enable()/disable()
        virtual bool onEnable() { return true; }
        virtual void onDisable() {}

    private:
        bool m_Enabled = false;
    };
} // namespace vultra
