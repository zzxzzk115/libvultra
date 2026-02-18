#pragma once

#include "vultra/core/base/api.hpp"
#include "vultra/core/engine/engine_feature.hpp"
#include "vultra/core/engine/engine_subsystem.hpp"

#include <algorithm>
#include <vector>

namespace vultra
{
    class VULTRA_API FramePipeline final
    {
    public:
        FramePipeline()                                = default;
        FramePipeline(const FramePipeline&)            = delete;
        FramePipeline& operator=(const FramePipeline&) = delete;
        FramePipeline(FramePipeline&&)                 = delete;
        FramePipeline& operator=(FramePipeline&&)      = delete;
        ~FramePipeline()                               = default;

        void addSubsystem(EngineSubsystem& s) { m_Subsystems.push_back(&s); }

        void attach(EngineFeature& f)
        {
            // avoid duplicates
            if (std::find(m_Features.begin(), m_Features.end(), &f) == m_Features.end())
                m_Features.push_back(&f);
        }

        void detach(EngineFeature& f) { std::erase(m_Features, &f); }

        void tickFrame(fsec dt)
        {
            // Update phases
            for (auto* s : m_Subsystems)
                s->onPreUpdate(dt);
            for (auto* s : m_Subsystems)
                s->onUpdate(dt);
            for (auto* s : m_Subsystems)
                s->onPhysics(dt);
            for (auto* s : m_Subsystems)
                s->onPostUpdate(dt);

            // Feature frame begin
            for (auto* f : m_Features)
                f->onBeginFrame();

            // Render phases
            for (auto* s : m_Subsystems)
                s->onPreRender();
            for (auto* s : m_Subsystems)
                s->onRender();
            for (auto* s : m_Subsystems)
                s->onPostRender();

            // Feature frame end
            for (auto* f : m_Features)
                f->onEndFrame();
        }

    private:
        std::vector<EngineSubsystem*> m_Subsystems;
        std::vector<EngineFeature*>   m_Features;
    };
} // namespace vultra
