#include "editor_app/ui/windows/console_window.hpp"

#include "common/ui_widgets.hpp"

#include <vultra/core/base/common_context.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

namespace vultra_app
{
    namespace
    {
        uint32_t levelBit(vultra::Logger::Level level) { return 1u << static_cast<uint32_t>(level); }

        const char* levelIcon(vultra::Logger::Level level)
        {
            switch (level)
            {
                case vultra::Logger::Level::eTrace:
                    return ICON_MDI_MESSAGE_TEXT;
                case vultra::Logger::Level::eInfo:
                    return ICON_MDI_INFORMATION;
                case vultra::Logger::Level::eWarn:
                    return ICON_MDI_ALERT;
                case vultra::Logger::Level::eError:
                    return ICON_MDI_CLOSE_OCTAGON;
                case vultra::Logger::Level::eCritical:
                    return ICON_MDI_ALERT_OCTAGRAM;
                default:
                    return ICON_MDI_ALERT_OCTAGRAM;
            }
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
                    return {0.75f, 0.75f, 0.75f, 1.0f};
                case vultra::Logger::Level::eInfo:
                    return {0.40f, 0.70f, 1.0f, 1.0f};
                case vultra::Logger::Level::eWarn:
                    return {1.0f, 0.82f, 0.22f, 1.0f};
                case vultra::Logger::Level::eError:
                    return {1.0f, 0.30f, 0.25f, 1.0f};
                case vultra::Logger::Level::eCritical:
                    return {0.95f, 0.32f, 1.0f, 1.0f};
                default:
                    return {1.0f, 1.0f, 1.0f, 1.0f};
            }
        }
    } // namespace

    ConsoleWindow::ConsoleWindow() : EditorWindow("Console", ICON_MDI_CONSOLE) {}

    void ConsoleWindow::draw(EditorContext& ctx)
    {
        subscribeLogger();

        ImGui::Begin(title().c_str(), &m_Open);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ICON_MDI_MAGNIFY);
        ImGui::SameLine();

        const float levelButtonWidth =
            ImGui::CalcTextSize(levelIcon(vultra::Logger::Level::eInfo)).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float levelButtonsWidth = (levelButtonWidth + ImGui::GetStyle().ItemSpacing.x) *
                                        static_cast<float>(vultra::Logger::Level::eMaxLevels);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        m_Filter.Draw("##ConsoleFilter", ImGui::GetContentRegionAvail().x - levelButtonsWidth - 112.0f);
        ImGui::PopStyleColor();

        for (int i = 0; i < static_cast<int>(vultra::Logger::Level::eMaxLevels); ++i)
        {
            const auto level   = static_cast<vultra::Logger::Level>(i);
            const auto bit     = levelBit(level);
            const bool enabled = (m_LevelMask & bit) != 0;
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, enabled ? levelColor(level) : ImVec4(0.5f, 0.5f, 0.5f, 0.55f));
            if (ui::iconButton(levelIcon(level), levelName(level), enabled))
                m_LevelMask ^= bit;
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_DELETE_SWEEP " Clear"))
            m_Logs.clear();
        ImGui::SameLine();
        if (ui::iconButton(m_AutoScroll ? ICON_MDI_ARROW_DOWN_BOLD_BOX : ICON_MDI_ARROW_DOWN_BOLD_BOX_OUTLINE,
                           "Auto-scroll",
                           m_AutoScroll))
            m_AutoScroll = !m_AutoScroll;

        ImGui::Separator();
        if (!ctx.state.statusMessage.empty())
            ImGui::TextDisabled("%s %s", ICON_MDI_INFORMATION_OUTLINE, ctx.state.statusMessage.c_str());

        if (ImGui::BeginTable("ConsoleMessages",
                              2,
                              ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("Message");

            for (const auto& log : m_Logs)
            {
                if ((m_LevelMask & levelBit(log.level)) == 0)
                    continue;
                if (m_Filter.IsActive() && !m_Filter.PassFilter(log.message.c_str()))
                    continue;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, levelColor(log.level));
                ImGui::TextUnformatted(levelIcon(log.level));
                ImGui::PopStyleColor();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(log.message.c_str());
            }

            if (m_RequestScrollToBottom && ImGui::GetScrollMaxY() > 0.0f)
            {
                ImGui::SetScrollHereY(1.0f);
                m_RequestScrollToBottom = false;
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }

    void ConsoleWindow::subscribeLogger()
    {
        if (m_Subscribed)
            return;

        vultra::commonContext.logger.on<vultra::Logger::LogEvent>([this](const vultra::Logger::LogEvent& event, auto&) {
            if (event.region != vultra::Logger::Region::eClient)
                return;

            m_Logs.push_back(LogEntry {.region = event.region, .level = event.level, .message = event.msg});
            if (m_Logs.size() > 3500)
                m_Logs.erase(m_Logs.begin(), m_Logs.begin() + 500);
            if (m_AutoScroll)
                m_RequestScrollToBottom = true;
        });

        m_Subscribed = true;
    }
} // namespace vultra_app
