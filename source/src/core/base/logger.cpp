#include "vultra/core/base/logger.hpp"

#include <magic_enum/magic_enum.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if !defined(__ANDROID__)
#include <spdlog/sinks/basic_file_sink.h>
#endif

namespace vultra
{
    namespace
    {
#if defined(__ANDROID__)
        [[nodiscard]] int toAndroidPriority(const Logger::Level level)
        {
            switch (level)
            {
                case Logger::Level::eTrace:
                    return ANDROID_LOG_VERBOSE;
                case Logger::Level::eInfo:
                    return ANDROID_LOG_INFO;
                case Logger::Level::eWarn:
                    return ANDROID_LOG_WARN;
                case Logger::Level::eError:
                    return ANDROID_LOG_ERROR;
                case Logger::Level::eCritical:
                    return ANDROID_LOG_FATAL;
                default:
                    return ANDROID_LOG_DEFAULT;
            }
        }

        [[nodiscard]] const char* toAndroidTag(const Logger::Region region)
        {
            switch (region)
            {
                case Logger::Region::eCore:
                    return "VULTRA_CORE";
                case Logger::Region::eClient:
                    return "VULTRA_CLIENT";
                default:
                    return "VULTRA_CORE";
            }
        }
#endif
    } // namespace

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

        logSinks[0]->set_pattern("%^[%Y-%m-%d %H:%M:%S:%f] %n: %v%$");

#if !defined(__ANDROID__)
        logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("Vultra.log", true));
        logSinks[1]->set_pattern("[%Y-%m-%d %H:%M:%S:%f] [%l] %n: %v");
#endif

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
#if defined(__ANDROID__)
        __android_log_print(toAndroidPriority(level), toAndroidTag(region), "%s", msg.data());
#endif
        publish<LogEvent>({region, level, msg.data()});
    }
} // namespace vultra
