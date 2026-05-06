#include "editor_app/ui/windows/console_window.hpp"

#include <vultra/core/base/common_context.hpp>

#include <imgui.h>

#include <algorithm>

namespace vultra_app
{
    namespace
    {
        uint32_t levelBit(vultra::Logger::Level level)
        {
            return 1u << static_cast<uint32_t>(level);
        }

        const char* levelName(vultra::Logger::Level level)
        {
            switch (level)
            {
            case vultra::Logger::Level::eTrace:
                return "Trace";
            case vultra::Logger::Level::eInfo:
                return "Info";
            case vultra::Logger::Level::eWarn:
                return "Warn";
            case vultra::Logger::Level::eError:
                return "Error";
            case vultra::Logger::Level::eCritical:
                return "Critical";
            default:
                return "Unknown";
            }
        }

        ImVec4 levelColor(vultra::Logger::Level level)
        {
            switch (level)
            {
            case vultra::Logger::Level::eTrace:
                return {0.65f, 0.65f, 0.65f, 1.0f};
            case vultra::Logger::Level::eInfo:
                return {0.40f, 0.70f, 1.00f, 1.0f};
            case vultra::Logger::Level::eWarn:
                return {1.00f, 0.82f, 0.22f, 1.0f};
            case vultra::Logger::Level::eError:
                return {1.00f, 0.35f, 0.30f, 1.0f};
            case vultra::Logger::Level::eCritical:
                return {1.00f, 0.20f, 0.72f, 1.0f};
            default:
                return {1.0f, 1.0f, 1.0f, 1.0f};
            }
        }
    } // namespace

    ConsoleWindow::ConsoleWindow() : EditorWindow("Console") {}

    void ConsoleWindow::draw(EditorContext& ctx)
    {
        subscribeLogger();

        ImGui::Begin(m_Name.c_str(), &m_Open);

        ImGui::InputText("Filter", m_Filter.data(), m_Filter.size());
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            m_Logs.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

        for (int i = 0; i < static_cast<int>(vultra::Logger::Level::eMaxLevels); ++i)
        {
            auto     level   = static_cast<vultra::Logger::Level>(i);
            uint32_t bit     = levelBit(level);
            bool     enabled = (m_LevelMask & bit) != 0;
            ImGui::SameLine();
            if (ImGui::Checkbox(levelName(level), &enabled))
            {
                if (enabled)
                    m_LevelMask |= bit;
                else
                    m_LevelMask &= ~bit;
            }
        }

        ImGui::Separator();
        if (!ctx.state.statusMessage.empty())
            ImGui::BulletText("%s", ctx.state.statusMessage.c_str());

        if (ImGui::BeginTable("ConsoleMessages",
                              3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("Message");
            ImGui::TableHeadersRow();

            for (const auto& log : m_Logs)
            {
                if ((m_LevelMask & levelBit(log.level)) == 0)
                    continue;
                if (m_Filter[0] != '\0' && log.message.find(m_Filter.data()) == std::string::npos)
                    continue;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, levelColor(log.level));
                ImGui::TextUnformatted(levelName(log.level));
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(log.region == vultra::Logger::Region::eCore ? "Core" : "Client");
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", log.message.c_str());
            }

            if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);

            ImGui::EndTable();
        }

        ImGui::End();
    }

    void ConsoleWindow::subscribeLogger()
    {
        if (m_Subscribed)
            return;

        vultra::commonContext.logger.on<vultra::Logger::LogEvent>(
            [this](const vultra::Logger::LogEvent& event, auto&)
            {
                m_Logs.push_back(LogEntry {.region = event.region, .level = event.level, .message = event.msg});
                if (m_Logs.size() > 2000)
                    m_Logs.erase(m_Logs.begin(), m_Logs.begin() + 500);
            });

        m_Subscribed = true;
    }
} // namespace vultra_app
