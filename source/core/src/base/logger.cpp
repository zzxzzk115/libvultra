#include <vultra/core/base/logger.hpp>

#include <spdlog/sinks/basic_file_sink.h>
#if defined(__ANDROID__)
#include <spdlog/sinks/android_sink.h>
#else
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

#include <vector>

namespace vultra
{
    namespace
    {
        struct Loggers
        {
#if defined(__ANDROID__)
            std::shared_ptr<spdlog::sinks::android_sink_mt> console =
                std::make_shared<spdlog::sinks::android_sink_mt>("Vultra");
#else
            std::shared_ptr<spdlog::sinks::stderr_color_sink_mt> console =
                std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
#endif
            spdlog::logger core {"Vultra", console};
            spdlog::logger app {"App", console};
        };

        Loggers& loggers()
        {
            static Loggers instance;
            return instance;
        }
    } // namespace

    std::expected<void, Error> Logger::configure(spdlog::level::level_enum level, const std::filesystem::path& file)
    {
        // Configure on the main thread before starting devices or worker threads.
        auto&                         state = loggers();
        std::vector<spdlog::sink_ptr> sinks {state.console};
        if (!file.empty())
        {
            std::error_code error;
            if (!file.parent_path().empty())
            {
                std::filesystem::create_directories(file.parent_path(), error);
            }
            if (error)
            {
                return makeError("Create log directory {}: {}", file.parent_path().string(), error.message());
            }
            // spdlog reports a file it cannot open only by throwing.
            try
            {
                sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(file.string(), false));
            }
            catch (const spdlog::spdlog_ex& failure)
            {
                return makeError("Open log file {}: {}", file.string(), failure.what());
            }
        }
        for (auto* logger : {&state.core, &state.app})
        {
            logger->sinks() = sinks;
            logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
            logger->set_level(level);
            logger->flush_on(spdlog::level::warn);
        }
        return {};
    }

    spdlog::logger& Logger::core()
    {
        return loggers().core;
    }

    spdlog::logger& Logger::app()
    {
        return loggers().app;
    }
} // namespace vultra
