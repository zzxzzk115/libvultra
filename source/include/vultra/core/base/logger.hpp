#pragma once

#include "vultra/core/base/base.hpp"

#include <entt/entt.hpp>
#include <spdlog/spdlog.h>

namespace vultra
{
    class Logger : public entt::emitter<Logger>
    {
    public:
        Logger()              = delete;
        Logger(const Logger&) = delete;
        Logger(Logger&&) noexcept;
        ~Logger() override;

        Logger& operator=(const Logger&) = delete;
        Logger& operator=(Logger&&) noexcept;

        enum class Level : uint8_t
        {
            eTrace,
            eInfo,
            eWarn,
            eError,
            eCritical,
            eMaxLevels
        };

        Logger& setLevel(Level);

        enum class Region : uint8_t
        {
            eCore,
            eClient,
        };

        template<typename... Args>
        void trace(bool isCore, std::string_view fmt, Args&&... args)
        {
            auto formatMsg = getFormatString(fmt, args...);
            if (m_Level <= Level::eTrace)
            {
                getLogger(isCore)->trace(formatMsg);
                triggerLogEvent(getRegion(isCore), Level::eTrace, formatMsg);
            }
        }

        template<typename... Args>
        void info(bool isCore, std::string_view fmt, Args&&... args)
        {
            auto formatMsg = getFormatString(fmt, args...);
            if (m_Level <= Level::eInfo)
            {
                getLogger(isCore)->info(formatMsg);
                triggerLogEvent(getRegion(isCore), Level::eInfo, formatMsg);
            }
        }

        template<typename... Args>
        void warn(bool isCore, std::string_view fmt, Args&&... args)
        {
            auto formatMsg = getFormatString(fmt, args...);
            if (m_Level <= Level::eWarn)
            {
                getLogger(isCore)->warn(formatMsg);
                triggerLogEvent(getRegion(isCore), Level::eWarn, formatMsg);
            }
        }

        template<typename... Args>
        void error(bool isCore, std::string_view fmt, Args&&... args)
        {
            auto formatMsg = getFormatString(fmt, args...);
            if (m_Level <= Level::eError)
            {
                getLogger(isCore)->error(formatMsg);
                triggerLogEvent(getRegion(isCore), Level::eError, formatMsg);
            }
        }

        template<typename... Args>
        void critical(bool isCore, std::string_view fmt, Args&&... args)
        {
            auto formatMsg = getFormatString(fmt, args...);
            if (m_Level <= Level::eCritical)
            {
                getLogger(isCore)->critical(formatMsg);
                triggerLogEvent(getRegion(isCore), Level::eCritical, formatMsg);
            }
        }

        class Builder
        {
        public:
            Builder()                   = default;
            Builder(const Builder&)     = delete;
            Builder(Builder&&) noexcept = delete;
            ~Builder()                  = default;

            Builder operator=(const Builder&)     = delete;
            Builder operator=(Builder&&) noexcept = delete;

            Builder& setLevel(Level);

            [[nodiscard]] Logger build() const;

        private:
            Level m_Level = Level::eTrace;
        };

        struct LogEvent
        {
            Region      region;
            Level       level;
            std::string msg;

            [[nodiscard]] std::string toString() const;
        };

    private:
        explicit Logger(Level);

        template<typename... Args>
        [[nodiscard]] std::string getFormatString(std::string_view fmt, Args&&... args)
        {
            return fmt::vformat(fmt, fmt::make_format_args(args...));
        }

        [[nodiscard]] Ref<spdlog::logger> getLogger(bool isCore) { return isCore ? m_CoreLogger : m_ClientLogger; }

        void triggerLogEvent(Region, Level, std::string_view);

        [[nodiscard]] static Region getRegion(bool isCore) { return isCore ? Region::eCore : Region::eClient; }

    private:
        Level m_Level {Level::eTrace};

        Ref<spdlog::logger> m_CoreLogger {nullptr};
        Ref<spdlog::logger> m_ClientLogger {nullptr};
    };

    using LogEvent = Logger::LogEvent;
} // namespace vultra