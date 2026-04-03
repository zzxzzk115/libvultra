#include "vultra/core/app/app_host.hpp"
#include "vultra/core/base/common_context.hpp"

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
        return run(0, nullptr);
    }

    int AppHost::run(const int argc, char** argv)
    {
        m_CommandLineArgs.clear();
        if (argc > 1 && argv != nullptr)
        {
            m_CommandLineArgs.reserve(static_cast<size_t>(argc - 1));
            for (int i = 1; i < argc; ++i)
            {
                if (argv[i] != nullptr)
                {
                    m_CommandLineArgs.emplace_back(argv[i]);
                }
            }
        }

        try
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
        catch (const std::exception& e)
        {
            VULTRA_CORE_ERROR("[AppHost] Unhandled exception: {}", e.what());
        }
        catch (...)
        {
            VULTRA_CORE_ERROR("[AppHost] Unknown unhandled exception");
        }
        return 1;
    }
} // namespace vultra
