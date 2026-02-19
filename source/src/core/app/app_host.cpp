#include "vultra/core/app/app_host.hpp"

namespace vultra
{
    int AppHost::run()
    {
        // Let derived app configure subsystems/features/plugins
        onConfigure(m_Engine);

        if (!m_Engine.initCore())
            return 1;

        onPostConfigure(m_Engine);

        while (!onShouldClose())
        {
            onPollEvents();

            const fsec dt = onFrameDelta();

            onBeforeEngineTick(dt);
            m_Engine.tickFrame(dt);
            onAfterEngineTick(dt);
        }

        m_Engine.shutdownCore();
        return 0;
    }
} // namespace vultra
