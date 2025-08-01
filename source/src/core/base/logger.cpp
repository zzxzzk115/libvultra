#include "vultra/core/base/logger.hpp"

#include <magic_enum/magic_enum.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace vultra
{
    Logger::Logger(Logger&& other) noexcept : emitter {std::move(other)}, m_Level(other.m_Level) {}

    Logger::~Logger()
    {
        clear();
        spdlog::shutdown();
    }

    Logger& Logger::operator=(Logger&& rhs) noexcept
    {
        if (this != &rhs)
        {
            emitter::operator=(std::move(rhs));
            m_Level = rhs.m_Level;
        }

        return *this;
    }

    Logger& Logger::setLevel(Level level)
    {
        m_Level = level;
        return *this;
    }

    Logger::Builder& Logger::Builder::setLevel(Level level)
    {
        m_Level = level;
        return *this;
    }

    Logger Logger::Builder::build() const { return Logger {m_Level}; }

    std::string Logger::LogEvent::toString() const
    {
        return fmt::format(
            "Level: {}, Region: {}, Message: {}", magic_enum::enum_name(level), magic_enum::enum_name(region), msg);
    }

    Logger::Logger(const Level level)
    {
        m_Level = level;

        std::vector<spdlog::sink_ptr> logSinks;

        logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("Vultra.log", true));

        logSinks[0]->set_pattern("%^[%Y-%m-%d %H:%M:%S:%f] %n: %v%$");
        logSinks[1]->set_pattern("[%Y-%m-%d %H:%M:%S:%f] [%l] %n: %v");

        m_CoreLogger = std::make_shared<spdlog::logger>("VULTRA_CORE", begin(logSinks), end(logSinks));
        spdlog::register_logger(m_CoreLogger);
        m_CoreLogger->set_level(spdlog::level::trace);
        m_CoreLogger->flush_on(spdlog::level::trace);

        m_ClientLogger = std::make_shared<spdlog::logger>("VULTRA_CLIENT", begin(logSinks), end(logSinks));
        spdlog::register_logger(m_ClientLogger);
        m_ClientLogger->set_level(spdlog::level::trace);
        m_ClientLogger->flush_on(spdlog::level::trace);
    }

    void Logger::triggerLogEvent(Region region, Level level, std::string_view msg)
    {
        publish<LogEvent>({region, level, msg.data()});
    }
} // namespace vultra