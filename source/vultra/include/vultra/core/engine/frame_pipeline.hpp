#pragma once

#include "vultra/core/base/api.hpp"
#include "vultra/core/engine/engine_feature.hpp"
#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/rendering/runtime_profiler.hpp"

#include <algorithm>
#include <string>
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
            RuntimeProfiler::ExternalScope frameScope {"FramePipeline::tickFrame"};
            auto subsystemLabel = [](const char* phase, EngineSubsystem* subsystem) {
                return std::string("FramePipeline::") + phase + "/" + (subsystem ? subsystem->name() : "<null>");
            };

            // Update phases
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::PreUpdate"};
                for (auto* s : m_Subsystems)
                    s->onPreUpdate(dt);
            }
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::Update"};
                for (auto* s : m_Subsystems)
                {
                    RuntimeProfiler::ExternalScope scope {subsystemLabel("Update", s)};
                    s->onUpdate(dt);
                }
            }
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::Physics"};
                for (auto* s : m_Subsystems)
                    s->onPhysics(dt);
            }
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::PostUpdate"};
                for (auto* s : m_Subsystems)
                    s->onPostUpdate(dt);
            }

            // Feature frame begin
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::FeatureBeginFrame"};
                for (auto* f : m_Features)
                    f->onBeginFrame();
            }

            // Render phases
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::PreRender"};
                for (auto* s : m_Subsystems)
                    s->onPreRender();
            }
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::Render"};
                for (auto* s : m_Subsystems)
                {
                    RuntimeProfiler::ExternalScope scope {subsystemLabel("Render", s)};
                    s->onRender();
                }
            }
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::PostRender"};
                for (auto* s : m_Subsystems)
                    s->onPostRender();
            }
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::Present"};
                for (auto* s : m_Subsystems)
                    s->onPresent();
            }

            // Feature frame end
            {
                RuntimeProfiler::ExternalScope phaseScope {"FramePipeline::FeatureEndFrame"};
                for (auto* f : m_Features)
                    f->onEndFrame();
            }
        }

    private:
        std::vector<EngineSubsystem*> m_Subsystems;
        std::vector<EngineFeature*>   m_Features;
    };
} // namespace vultra
