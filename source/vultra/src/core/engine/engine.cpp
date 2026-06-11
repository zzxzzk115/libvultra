#include "vultra/core/engine/engine.hpp"

namespace vultra
{
    Engine::Engine()
    {
        m_Ctx.framePipeline = &m_Pipeline;

        m_Features = std::make_unique<FeatureManager>(m_Ctx);
        m_Plugins  = std::make_unique<PluginManager>();

        m_Ctx.pluginManager = m_Plugins.get();
    }

    bool Engine::initCore()
    {
        // init all subsystems registered in ModuleRegistry
        return m_Ctx.modules.initAll();
    }

    void Engine::shutdownCore()
    {
        // 1) Disable all features (detaches pipeline hooks)
        if (m_Features)
            m_Features->disableAll();

        // 2) Shutdown subsystems in reverse init order. PluginSystem owns the normal native-plugin
        // unload point so render/backend integrations can shut down after RenderSystem but before
        // RenderBackendSystem.
        m_Ctx.modules.shutdownAll();

        // 3) Fallback: if no PluginSystem was registered, still release any native plugins.
        if (m_Plugins)
            m_Plugins->unloadAll(m_Ctx);

        // 4) Finally destroy owned objects.
        m_Subsystems.clear();
    }

    void Engine::tickFrame(fsec dt)
    {
        m_Pipeline.tickFrame(dt);
        m_Ctx.frameIndex++;
    }
} // namespace vultra
