#pragma once

#include "vultra/core/base/api.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/engine/feature_manager.hpp"
#include "vultra/core/engine/frame_pipeline.hpp"
#include "vultra/core/plugin/plugin_manager.hpp"

#include <memory>
#include <vector>

namespace vultra
{
    class VULTRA_API Engine final
    {
    public:
        Engine();
        Engine(const Engine&)            = delete;
        Engine& operator=(const Engine&) = delete;
        Engine(Engine&&)                 = delete;
        Engine& operator=(Engine&&)      = delete;
        ~Engine()                        = default;

        EngineContext&       context() { return m_Ctx; }
        const EngineContext& context() const { return m_Ctx; }

        // Register a core subsystem BEFORE initCore().
        // Ownership stays with Engine via unique_ptr.
        template<typename T, typename... Args>
        T& emplaceSubsystem(Args&&... args)
        {
            auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
            T&   ref = *ptr;

            ref.setContext(m_Ctx);
            m_Ctx.modules.add(ref);
            m_Pipeline.addSubsystem(ref);

            m_Subsystems.emplace_back(std::move(ptr));
            return ref;
        }

        bool initCore();
        void shutdownCore();

        void tickFrame(fsec dt);

        FeatureManager& features() { return *m_Features; }
        PluginManager&  plugins() { return *m_Plugins; }

    private:
        EngineContext m_Ctx;

        std::unique_ptr<FeatureManager> m_Features;
        std::unique_ptr<PluginManager>  m_Plugins;

        FramePipeline m_Pipeline;

        std::vector<std::unique_ptr<EngineSubsystem>> m_Subsystems;
    };
} // namespace vultra
