#include "editor_app/ui/editor_top_bar.hpp"

#include <vultra/core/services/window_service.hpp>
#include <vultra/function/services/frame_debugger_service.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace vultra_app
{
    namespace
    {
        constexpr float kTitleBarHeight = 76.0f;
        constexpr float kToolBarHeight  = 46.0f;
        constexpr float kTitleLogoY     = 24.0f;
        constexpr float kTitleTextY     = 17.0f;
        constexpr float kMenuRowY       = 48.0f;

        void setTooltip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", text);
        }

        void drawEngineMark(const ImVec2 pos, const float radius)
        {
            auto* drawList = ImGui::GetWindowDrawList();
            drawList->AddCircleFilled(pos, radius, IM_COL32(6, 10, 15, 255), 40);
            drawList->AddCircle(pos, radius, IM_COL32(54, 150, 220, 210), 40, 1.5f);
            const char* mark = "V";
            const ImVec2 textSize = ImGui::CalcTextSize(mark);
            drawList->AddText(ImVec2 {pos.x - textSize.x * 0.5f, pos.y - textSize.y * 0.5f - 1.0f},
                              IM_COL32(225, 238, 250, 255),
                              mark);
        }

        void pushToolbarButtonStyle()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {8.0f, 5.0f});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.070f, 0.090f, 0.115f, 0.96f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.115f, 0.155f, 0.200f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4 {0.065f, 0.275f, 0.500f, 1.0f});
        }

        void popToolbarButtonStyle()
        {
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        bool toolbarButton(const char* label, const char* tooltip = nullptr, const ImVec2 size = ImVec2 {0.0f, 0.0f})
        {
            pushToolbarButtonStyle();
            const bool pressed = ImGui::Button(label, size);
            popToolbarButtonStyle();
            if (tooltip)
                setTooltip(tooltip);
            return pressed;
        }

        bool titleMenuButton(const char* label)
        {
            const ImVec2 menuPos = ImGui::GetCursorScreenPos();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {8.0f, 4.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.0f, 0.0f, 0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.120f, 0.150f, 0.185f, 0.95f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4 {0.075f, 0.220f, 0.390f, 1.0f});
            const bool pressed = ImGui::Button(label);
            const ImVec2 popupPos {menuPos.x, ImGui::GetItemRectMax().y};
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            if (pressed)
                ImGui::OpenPopup(label);
            ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
            return ImGui::BeginPopup(label);
        }

        bool titleBarWindowButton(const char* label, const char* tooltip, bool destructive = false)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {9.0f, 4.0f});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.0f, 0.0f, 0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  destructive ? ImVec4 {0.570f, 0.120f, 0.120f, 1.0f} :
                                                ImVec4 {0.135f, 0.165f, 0.200f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  destructive ? ImVec4 {0.720f, 0.140f, 0.140f, 1.0f} :
                                                ImVec4 {0.075f, 0.220f, 0.390f, 1.0f});
            const bool pressed = ImGui::Button(label);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            setTooltip(tooltip);
            return pressed;
        }

        bool playbackButton(const char* label,
                            const char* tooltip,
                            const bool  enabled,
                            const bool  active,
                            const bool  accent = false)
        {
            constexpr ImVec2 buttonSize {38.0f, 32.0f};
            if (!enabled)
                ImGui::BeginDisabled();

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const bool   pressed = ImGui::InvisibleButton(label, buttonSize) && enabled;
            const bool   hovered = enabled && ImGui::IsItemHovered();
            const bool   held    = enabled && ImGui::IsItemActive();

            const ImVec4 baseColor =
                accent ? ImVec4 {0.145f, 0.430f, 0.145f, 1.0f} :
                active ? ImVec4 {0.055f, 0.260f, 0.470f, 1.0f} :
                         ImVec4 {0.070f, 0.090f, 0.115f, 0.96f};
            const ImVec4 hoverColor =
                accent ? ImVec4 {0.205f, 0.610f, 0.180f, 1.0f} :
                active ? ImVec4 {0.075f, 0.335f, 0.600f, 1.0f} :
                         ImVec4 {0.115f, 0.155f, 0.200f, 1.0f};
            const ImVec4 downColor =
                accent ? ImVec4 {0.105f, 0.360f, 0.115f, 1.0f} : ImVec4 {0.050f, 0.220f, 0.395f, 1.0f};

            ImVec4 fill = held ? downColor : hovered ? hoverColor : baseColor;
            ImVec4 text = accent ? ImVec4 {0.720f, 1.000f, 0.600f, 1.0f} : ImVec4 {0.820f, 0.875f, 0.925f, 1.0f};
            if (!enabled)
            {
                fill.w *= 0.48f;
                text.w *= 0.38f;
            }

            auto*       drawList = ImGui::GetWindowDrawList();
            const ImVec2 max {pos.x + buttonSize.x, pos.y + buttonSize.y};
            drawList->AddRectFilled(pos, max, ImGui::GetColorU32(fill), 4.0f);
            drawList->AddRect(pos, max, IM_COL32(38, 48, 60, enabled ? 180 : 90), 4.0f);

            ImFont*      font      = ImGui::GetFont();
            const float  iconSize  = ImGui::GetFontSize() * 1.18f;
            const ImVec2 textSize  = font->CalcTextSizeA(iconSize, 1000.0f, 0.0f, label);
            const ImVec2 textPos {pos.x + (buttonSize.x - textSize.x) * 0.5f,
                                  pos.y + (buttonSize.y - textSize.y) * 0.5f - 1.0f};
            drawList->AddText(font, iconSize, textPos, ImGui::GetColorU32(text), label);

            setTooltip(tooltip);
            if (!enabled)
                ImGui::EndDisabled();
            return enabled && pressed;
        }

        void drawPlaybackControls(EditorContext& ctx)
        {
            ImGui::PushID("PlaybackControls");
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {0.0f, 0.0f});

            const bool playing = ctx.state.editorPlaying;
            const bool paused  = ctx.state.editorPaused;

            if (playbackButton(ICON_MDI_PLAY, playing && paused ? "Resume" : "Play", true, playing && !paused, true))
            {
                ctx.state.editorPlaying = true;
                ctx.state.editorPaused  = false;
            }

            ImGui::SameLine(0.0f, 0.0f);
            if (playbackButton(ICON_MDI_PAUSE, "Pause", playing, paused))
                ctx.state.editorPaused = true;

            ImGui::SameLine(0.0f, 0.0f);
            if (playbackButton(ICON_MDI_STOP, "Stop", playing, false))
            {
                ctx.state.editorPlaying       = false;
                ctx.state.editorPaused        = false;
                ctx.state.editorStepRequested = false;
            }

            ImGui::SameLine(0.0f, 0.0f);
            if (playbackButton(ICON_MDI_STEP_FORWARD, "Step Frame", true, false))
            {
                ctx.state.editorPlaying       = true;
                ctx.state.editorPaused        = true;
                ctx.state.editorStepRequested = true;
            }

            ImGui::SameLine(0.0f, 0.0f);
            if (playbackButton(ICON_MDI_DOTS_VERTICAL, "Playback options", true, false))
                ImGui::OpenPopup("PlaybackOptions");
            if (ImGui::BeginPopup("PlaybackOptions"))
            {
                if (ImGui::MenuItem("Step Frame"))
                {
                    ctx.state.editorPlaying       = true;
                    ctx.state.editorPaused        = true;
                    ctx.state.editorStepRequested = true;
                }
                ImGui::MenuItem("Reset Play State", nullptr, false, false);
                ImGui::EndPopup();
            }

            ImGui::PopStyleVar();
            ImGui::PopID();
        }

        void drawRenderDocMenu(EditorContext& ctx)
        {
            auto* frameDebugger = ctx.services ? ctx.services->tryGet<vultra::IFrameDebuggerService>() : nullptr;
            const bool enabled  = frameDebugger && frameDebugger->isRenderDocEnabled();
            const bool available = enabled && frameDebugger->isAvailable();

            if (ImGui::BeginMenu("RenderDoc", enabled))
            {
                if (!available)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Capture Next Frame", "F12") && frameDebugger)
                    frameDebugger->captureSingleFrame();

                if (!available)
                    ImGui::EndDisabled();

                ImGui::Separator();
                ImGui::MenuItem(available ? "Available" : "Unavailable", nullptr, false, false);
                if (frameDebugger)
                {
                    ImGui::MenuItem(frameDebugger->isFrameCapturing() ? "Capturing" : "Idle", nullptr, false, false);
                    const std::string captureCount = "Captures: " + std::to_string(frameDebugger->getCaptureCount());
                    ImGui::MenuItem(captureCount.c_str(), nullptr, false, false);
                }
                ImGui::EndMenu();
            }
        }
    } // namespace

    void drawEditorTopBar(EditorContext&                                      ctx,
                          const std::vector<std::unique_ptr<EditorWindow>>& windows,
                          const EditorTopBarActions&                         actions)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoDocking;

        if (ImGui::BeginViewportSideBar("##VultraEditorTitleBar", viewport, ImGuiDir_Up, kTitleBarHeight, barFlags))
        {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            auto* windowService = ctx.services ? ctx.services->tryGet<IWindowService>() : nullptr;
            const bool decoratedWindow = windowService && windowService->window().isDecorated();

            drawEngineMark(ImVec2 {start.x + 28.0f, start.y + kTitleLogoY}, 20.0f);

            ImGui::SetCursorScreenPos(ImVec2 {start.x + 64.0f, start.y + kTitleTextY});
            ImGui::TextUnformatted("VultraEngine Editor");

            ImGui::SetCursorScreenPos(ImVec2 {start.x + 12.0f, start.y + kMenuRowY});
            if (titleMenuButton("File"))
            {
                if (ImGui::MenuItem("New Blank Scene") && actions.newBlankScene)
                    actions.newBlankScene(ctx);
                if (ImGui::MenuItem("Save Scene", "Ctrl+S") && actions.saveScene)
                    actions.saveScene(ctx);
                if (ImGui::MenuItem("Back to Launcher") && actions.backToLauncher)
                    actions.backToLauncher(ctx);
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, 2.0f);
            if (titleMenuButton("Edit"))
            {
                if (ImGui::MenuItem("Reset Layout") && actions.resetLayout)
                    actions.resetLayout(ctx);
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, 2.0f);
            if (titleMenuButton("Window"))
            {
                if (ImGui::MenuItem("Reset Layout") && actions.resetLayout)
                    actions.resetLayout(ctx);
                ImGui::Separator();

                for (const auto& window : windows)
                    ImGui::MenuItem(window->displayName().c_str(), nullptr, &window->open());
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, 2.0f);
            if (titleMenuButton("Tools"))
            {
                ImGui::MenuItem("Import", nullptr, false, false);
                ImGui::MenuItem("Save All", nullptr, false, false);
                ImGui::Separator();
                drawRenderDocMenu(ctx);
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, 2.0f);
            if (titleMenuButton("Help"))
            {
                if (ImGui::MenuItem("About Vultra Editor") && actions.showAbout)
                    actions.showAbout(ctx);
                ImGui::EndPopup();
            }

            if (!decoratedWindow)
            {
                ImGui::SetCursorScreenPos(ImVec2 {start.x + ImGui::GetWindowWidth() - 116.0f, start.y + 10.0f});
                if (titleBarWindowButton(ICON_MDI_WINDOW_MINIMIZE, "Minimize") && windowService)
                    windowService->window().minimize();
                ImGui::SameLine(0.0f, 0.0f);
                const bool maximized = windowService && windowService->window().isMaximized();
                if (titleBarWindowButton(maximized ? ICON_MDI_WINDOW_RESTORE : ICON_MDI_WINDOW_MAXIMIZE,
                                         maximized ? "Restore" : "Maximize") &&
                    windowService)
                {
                    if (maximized)
                        windowService->window().restore();
                    else
                        windowService->window().maximize();
                }
                ImGui::SameLine(0.0f, 0.0f);
                if (titleBarWindowButton(ICON_MDI_CLOSE, "Close", true) && windowService)
                    windowService->window().close();
            }

            ImGui::End();
        }

        if (ImGui::BeginViewportSideBar("##VultraEditorToolBar", viewport, ImGuiDir_Up, kToolBarHeight, barFlags))
        {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2 {start.x + 12.0f, start.y + 7.0f});

            toolbarButton(ICON_MDI_VIEW_DASHBOARD, "Content drawer", ImVec2 {30.0f, 0.0f});
            ImGui::SameLine(0.0f, 6.0f);
            toolbarButton(ICON_MDI_ENGINE_OUTLINE, "Editor tools", ImVec2 {30.0f, 0.0f});
            ImGui::SameLine(0.0f, 10.0f);

            const std::string projectLabel =
                ctx.state.currentProjectName.empty() ? "No Project" : ctx.state.currentProjectName;
            ImGui::SetNextItemWidth(158.0f);
            if (ImGui::BeginCombo("##ProjectSelector", projectLabel.c_str(), ImGuiComboFlags_NoArrowButton))
            {
                ImGui::TextDisabled("%s", ctx.state.currentProject.empty() ?
                                             "No project loaded" :
                                             ctx.state.currentProject.generic_string().c_str());
                if (ImGui::Selectable("Back to Launcher") && actions.backToLauncher)
                    actions.backToLauncher(ctx);
                ImGui::EndCombo();
            }

            ImGui::SameLine(0.0f, 12.0f);
            toolbarButton(ICON_MDI_COG_TRANSFER_OUTLINE " " ICON_MDI_MENU_DOWN, "Editor tools", ImVec2 {46.0f, 0.0f});
            ImGui::SameLine(0.0f, 6.0f);
            toolbarButton(ICON_MDI_SOURCE_BRANCH " " ICON_MDI_MENU_DOWN, "Graph tools", ImVec2 {46.0f, 0.0f});
            ImGui::SameLine(0.0f, 12.0f);
            drawPlaybackControls(ctx);

            ImGui::SameLine(0.0f, 12.0f);
            const char* platformLabel = ICON_MDI_MONITOR "  Platforms " ICON_MDI_MENU_DOWN;
            const float platformWidth = ImGui::CalcTextSize(platformLabel).x + 26.0f;
            const float settingsWidth = ImGui::CalcTextSize(ICON_MDI_COG "  Settings").x + 26.0f;
            const float settingsStart = ImGui::GetWindowWidth() - settingsWidth - 18.0f;
            if (ImGui::GetCursorPosX() + platformWidth + 20.0f < settingsStart)
            {
                toolbarButton(platformLabel, "Target platform", ImVec2 {platformWidth, 0.0f});
                ImGui::SameLine(0.0f, 8.0f);
            }
            if (settingsStart > ImGui::GetCursorPosX())
                ImGui::SetCursorPosX(settingsStart);
            toolbarButton(ICON_MDI_COG "  Settings", "Editor settings", ImVec2 {settingsWidth, 0.0f});

            ImGui::End();
        }
    }
} // namespace vultra_app
