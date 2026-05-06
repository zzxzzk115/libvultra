#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <vultra/core/base/logger.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vultra_app
{
    class ConsoleWindow final : public EditorWindow
    {
    public:
        ConsoleWindow();

        void draw(EditorContext& ctx) override;

    private:
        struct LogEntry
        {
            vultra::Logger::Region region {vultra::Logger::Region::eClient};
            vultra::Logger::Level  level {vultra::Logger::Level::eInfo};
            std::string            message;
        };

        void subscribeLogger();

        std::array<char, 128> m_Filter {};
        std::vector<LogEntry> m_Logs;
        uint32_t              m_LevelMask {0xFFFFFFFFu};
        bool                  m_Subscribed {false};
        bool                  m_AutoScroll {true};
    };
} // namespace vultra_app
