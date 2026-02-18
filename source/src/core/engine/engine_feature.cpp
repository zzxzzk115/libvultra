#include "vultra/core/engine/engine_feature.hpp"
#include "vultra/core/engine/frame_pipeline.hpp"

namespace vultra
{
    bool EngineFeature::enable()
    {
        if (m_Enabled)
            return true;

        if (!onEnable())
            return false;

        // Attach to pipeline if present
        if (ctx().framePipeline)
            ctx().framePipeline->attach(*this);

        m_Enabled = true;
        return true;
    }

    void EngineFeature::disable()
    {
        if (!m_Enabled)
            return;

        // Detach first to avoid callbacks during teardown
        if (ctx().framePipeline)
            ctx().framePipeline->detach(*this);

        onDisable();
        m_Enabled = false;
    }
} // namespace vultra
