#include <vultra/core/base/logger.hpp>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <vector>

namespace vultra
{
    namespace
    {
        struct Loggers
        {
            std::shared_ptr<spdlog::sinks::stderr_color_sink_mt> console =
                std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            spdlog::logger core {"Vultra", console};
            spdlog::logger app {"App", console};
        };

        Loggers& loggers()
        {
            static Loggers instance;
            return instance;
        }
    } // namespace

    void Logger::configure(spdlog::level::level_enum level, const std::filesystem::path& file)
    {
        // Configure on the main thread before starting devices or worker threads.
        auto&                         state = loggers();
        std::vector<spdlog::sink_ptr> sinks {state.console};
        if (!file.empty())
        {
            if (!file.parent_path().empty())
            {
                std::filesystem::create_directories(file.parent_path());
            }
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(file.string(), false));
        }
        for (auto* logger : {&state.core, &state.app})
        {
            logger->sinks() = sinks;
            logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
            logger->set_level(level);
            logger->flush_on(spdlog::level::warn);
        }
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
