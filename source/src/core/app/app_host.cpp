#include "vultra/core/app/app_host.hpp"

#include <algorithm>
#include <chrono>

namespace vultra
{
    fsec AppHost::onFrameDelta()
    {
        using Clock = std::chrono::steady_clock;

        static auto s_LastTick = Clock::now();

        const auto now = Clock::now();
        const auto dt  = std::chrono::duration_cast<fsec>(now - s_LastTick);
        s_LastTick     = now;

        return fsec {std::clamp(dt.count(), 0.0f, 0.25f)};
    }

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
