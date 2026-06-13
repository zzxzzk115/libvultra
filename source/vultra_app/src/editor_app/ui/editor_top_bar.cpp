#include "editor_app/ui/editor_top_bar.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/i18n/i18n.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/imgui/imgui_dpi.hpp>
#include <vultra/function/imgui/imgui_theme.hpp>
#include <vultra/function/services/editor_extension_service.hpp>
#include <vultra/function/services/frame_debugger_service.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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

        void invokeMenuClick(const vultra::ScriptedEditorMenuItemDesc& item)
        {
            if (!item.onClick.valid())
                return;
            sol::protected_function_result r = item.onClick();
            if (!r.valid())
            {
                const sol::error err = r;
                VULTRA_CORE_ERROR("[EditorExtensions] menu item '{}' onClick error: {}", item.id, err.what());
            }
        }

        bool menuItemEnabled(const vultra::ScriptedEditorMenuItemDesc& item)
        {
            if (!item.enabledWhen.valid())
                return true;
            sol::protected_function_result r = item.enabledWhen();
            if (!r.valid())
                return true; // a faulty predicate should not lock the item out
            return r.get_type() == sol::type::boolean ? r.get<bool>() : true;
        }

        // Renders the plugin-contributed menu items under the Tools menu,
        // nesting by the "/"-separated path (e.g. "My Plugin/Rescan" ->
        // submenu "My Plugin" with item "Rescan"). `depth` is the path segment
        // currently being grouped.
        void drawScriptedMenuLevel(const std::vector<const vultra::ScriptedEditorMenuItemDesc*>& items, size_t depth)
        {
            // collect submenu groups (segment at this depth) in first-seen order
            std::vector<std::string> groupOrder;
            for (const auto* item : items)
            {
                std::string_view path = item->path;
                // find the segment at `depth`
                size_t seg = 0, start = 0, end = path.size();
                bool   isLeafHere = true;
                for (size_t i = 0; i <= path.size(); ++i)
                {
                    if (i == path.size() || path[i] == '/')
                    {
                        if (seg == depth)
                        {
                            start = (seg == 0) ? 0 : start;
                            end   = i;
                            isLeafHere = (i == path.size());
                            break;
                        }
                        ++seg;
                        start = i + 1;
                    }
                }
                const std::string segment {path.substr(start, end - start)};
                if (isLeafHere)
                {
                    // leaf at this level: render as MenuItem
                    const auto* leaf = item;
                    const bool  enabled = menuItemEnabled(*leaf);
                    const char* shortcut = leaf->shortcut.empty() ? nullptr : leaf->shortcut.c_str();
                    if (ImGui::MenuItem(segment.c_str(), shortcut, false, enabled))
                        invokeMenuClick(*leaf);
                }
                else
                {
                    if (std::find(groupOrder.begin(), groupOrder.end(), segment) == groupOrder.end())
                        groupOrder.push_back(segment);
                }
            }

            // render submenus (items deeper than this level), grouped by segment
            for (const auto& group : groupOrder)
            {
                std::vector<const vultra::ScriptedEditorMenuItemDesc*> children;
                for (const auto* item : items)
                {
                    std::string_view path = item->path;
                    size_t seg = 0, start = 0, end = 0;
                    for (size_t i = 0; i <= path.size(); ++i)
                    {
                        if (i == path.size() || path[i] == '/')
                        {
                            if (seg == depth) { end = i; break; }
                            ++seg;
                            start = i + 1;
                        }
                    }
                    if (std::string {path.substr(start, end - start)} == group && end < path.size())
                        children.push_back(item);
                }
                if (!children.empty() && ImGui::BeginMenu(group.c_str()))
                {
                    drawScriptedMenuLevel(children, depth + 1);
                    ImGui::EndMenu();
                }
            }
        }

        void drawScriptedToolsMenu(EditorContext& ctx)
        {
            auto* ext = ctx.services ? ctx.services->tryGet<vultra::IEditorExtensionService>() : nullptr;
            if (!ext || ext->menuItems().empty())
                return;
            ImGui::Separator();
            std::vector<const vultra::ScriptedEditorMenuItemDesc*> items;
            items.reserve(ext->menuItems().size());
            for (const auto& item : ext->menuItems())
                items.push_back(&item);
            drawScriptedMenuLevel(items, 0);
        }

        void drawEngineMark(const ImVec2 pos, const float radius)
        {
            auto* drawList = ImGui::GetWindowDrawList();
            drawList->AddCircleFilled(pos, radius, vultra::imgui_theme::u32(vultra::imgui_theme::backgroundDeep()), 40);
            drawList->AddCircle(pos,
                                radius,
                                vultra::imgui_theme::u32(vultra::imgui_theme::accentTransparent(210.0f / 255.0f)),
                                40,
                                vultra::ui::dp(1.5f));
            const char*  mark     = "V";
            const ImVec2 textSize = ImGui::CalcTextSize(mark);
            drawList->AddText(ImVec2 {pos.x - textSize.x * 0.5f, pos.y - textSize.y * 0.5f - vultra::ui::dp(1.0f)},
                              vultra::imgui_theme::u32(vultra::imgui_theme::text()),
                              mark);
        }

        void pushToolbarButtonStyle()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, vultra::ui::dp(4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {vultra::ui::dp(8.0f), vultra::ui::dp(5.0f)});
            ImGui::PushStyleColor(ImGuiCol_Button, vultra::imgui_theme::buttonTransparent(0.96f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vultra::imgui_theme::buttonHovered());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, vultra::imgui_theme::accentButton());
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
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {vultra::ui::dp(8.0f), vultra::ui::dp(4.0f)});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, vultra::ui::dp(3.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.0f, 0.0f, 0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  vultra::imgui_theme::withAlpha(vultra::imgui_theme::buttonHovered(), 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, vultra::imgui_theme::accentButton());
            const bool   pressed = ImGui::Button(label);
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
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, vultra::ui::dp(3.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {vultra::ui::dp(9.0f), vultra::ui::dp(4.0f)});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.0f, 0.0f, 0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  destructive ? vultra::imgui_theme::destructiveHovered() :
                                                vultra::imgui_theme::buttonHovered());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  destructive ? vultra::imgui_theme::destructiveActive() :
                                                vultra::imgui_theme::accentButton());
            const bool pressed = ImGui::Button(label);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            setTooltip(tooltip);
            return pressed;
        }

        float relativeLuminance(const ImVec4& color) { return color.x * 0.299f + color.y * 0.587f + color.z * 0.114f; }

        ImVec4 contrastTextFor(const ImVec4& fill)
        {
            return relativeLuminance(fill) > 0.56f ? ImVec4 {0.055f, 0.070f, 0.090f, 1.0f} :
                                                     ImVec4 {0.960f, 0.980f, 1.000f, 1.0f};
        }

        bool playbackButton(const char* label,
                            const char* tooltip,
                            const bool  enabled,
                            const bool  active,
                            const bool  accent = false)
        {
            const ImVec2 buttonSize {vultra::ui::dp(38.0f), vultra::ui::dp(32.0f)};
            if (!enabled)
                ImGui::BeginDisabled();

            const ImVec2 pos     = ImGui::GetCursorScreenPos();
            const bool   pressed = ImGui::InvisibleButton(label, buttonSize) && enabled;
            const bool   hovered = enabled && ImGui::IsItemHovered();
            const bool   held    = enabled && ImGui::IsItemActive();

            const ImVec4 baseColor  = accent ? vultra::imgui_theme::success() :
                                      active ? vultra::imgui_theme::accentButton() :
                                               vultra::imgui_theme::buttonTransparent(0.96f);
            const ImVec4 hoverColor = accent ? vultra::imgui_theme::successHovered() :
                                      active ? vultra::imgui_theme::accentButtonHovered() :
                                               vultra::imgui_theme::buttonHovered();
            const ImVec4 downColor  = accent ? vultra::imgui_theme::successActive() :
                                      active ? vultra::imgui_theme::accentButtonActive() :
                                               vultra::imgui_theme::accentButton();

            ImVec4 fill = held ? downColor : hovered ? hoverColor : baseColor;
            ImVec4 text = (accent || active) ? contrastTextFor(fill) : vultra::imgui_theme::text();
            if (!enabled)
            {
                fill.w *= 0.48f;
                text = vultra::imgui_theme::textMuted();
                text.w *= 0.42f;
            }

            auto*        drawList = ImGui::GetWindowDrawList();
            const ImVec2 max {pos.x + buttonSize.x, pos.y + buttonSize.y};
            drawList->AddRectFilled(pos, max, ImGui::GetColorU32(fill), vultra::ui::dp(4.0f));
            drawList->AddRect(pos,
                              max,
                              ImGui::GetColorU32(vultra::imgui_theme::withAlpha(vultra::imgui_theme::border(),
                                                                                enabled ? 0.82f : 0.38f)),
                              vultra::ui::dp(4.0f));

            ImFont*      font     = ImGui::GetFont();
            const float  iconSize = ImGui::GetFontSize() * 1.18f;
            const ImVec2 textSize = font->CalcTextSizeA(iconSize, 1000.0f, 0.0f, label);
            const ImVec2 textPos {pos.x + (buttonSize.x - textSize.x) * 0.5f,
                                  pos.y + (buttonSize.y - textSize.y) * 0.5f - vultra::ui::dp(1.0f)};
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

            if (playbackButton(
                    ICON_MDI_PLAY, playing && paused ? vultra::tr("playback.resume") : vultra::tr("playback.play"),
                    true, playing && !paused, true))
            {
                ctx.state.editorPlaying = true;
                ctx.state.editorPaused  = false;
            }

            ImGui::SameLine(0.0f, 0.0f);
            if (playbackButton(ICON_MDI_PAUSE, vultra::tr("playback.pause"), playing, paused))
                ctx.state.editorPaused = true;

            ImGui::SameLine(0.0f, 0.0f);
            if (playbackButton(ICON_MDI_STOP, vultra::tr("playback.stop"), playing, false))
            {
                ctx.state.editorPlaying       = false;
                ctx.state.editorPaused        = false;
                ctx.state.editorStepRequested = false;
            }

            ImGui::SameLine(0.0f, 0.0f);
            if (playbackButton(ICON_MDI_STEP_FORWARD, vultra::tr("playback.stepFrame"), true, false))
            {
                ctx.state.editorPlaying       = true;
                ctx.state.editorPaused        = true;
                ctx.state.editorStepRequested = true;
            }

            ImGui::SameLine(0.0f, 0.0f);
            if (playbackButton(ICON_MDI_DOTS_VERTICAL, vultra::tr("playback.options"), true, false))
                ImGui::OpenPopup("PlaybackOptions");
            if (ImGui::BeginPopup("PlaybackOptions"))
            {
                if (ImGui::MenuItem(vultra::tr("playback.stepFrame")))
                {
                    ctx.state.editorPlaying       = true;
                    ctx.state.editorPaused        = true;
                    ctx.state.editorStepRequested = true;
                }
                ImGui::MenuItem(vultra::tr("playback.resetState"), nullptr, false, false);
                ImGui::EndPopup();
            }

            ImGui::PopStyleVar();
            ImGui::PopID();
        }

        void drawRenderDocMenu(EditorContext& ctx)
        {
            auto*      frameDebugger = ctx.services ? ctx.services->tryGet<vultra::IFrameDebuggerService>() : nullptr;
            const bool enabled       = frameDebugger && frameDebugger->isRenderDocEnabled();
            const bool available     = enabled && frameDebugger->isAvailable();

            if (ImGui::BeginMenu(vultra::trId("menu.renderdoc.title", "menuRenderDoc"), enabled))
            {
                if (!available)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem(vultra::tr("menu.renderdoc.capture"), "F12") && frameDebugger)
                    frameDebugger->captureSingleFrame();

                if (!available)
                    ImGui::EndDisabled();

                ImGui::Separator();
                ImGui::MenuItem(available ? vultra::tr("menu.renderdoc.available") : vultra::tr("menu.renderdoc.unavailable"),
                                nullptr,
                                false,
                                false);
                if (frameDebugger)
                {
                    ImGui::MenuItem(frameDebugger->isFrameCapturing() ? vultra::tr("menu.renderdoc.capturing") :
                                                                        vultra::tr("menu.renderdoc.idle"),
                                    nullptr,
                                    false,
                                    false);
                    const std::string captureCount =
                        vultra::trf("menu.renderdoc.captures", frameDebugger->getCaptureCount());
                    ImGui::MenuItem(captureCount.c_str(), nullptr, false, false);
                }
                ImGui::EndMenu();
            }
        }
    } // namespace

    void drawEditorTopBar(EditorContext&                                    ctx,
                          const std::vector<std::unique_ptr<EditorWindow>>& windows,
                          const EditorTopBarActions&                        actions)
    {
        ImGuiViewport*   viewport = ImGui::GetMainViewport();
        ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;

        if (ImGui::BeginViewportSideBar(
                "##VultraEditorTitleBar", viewport, ImGuiDir_Up, vultra::ui::dp(kTitleBarHeight), barFlags))
        {
            const ImVec2 start           = ImGui::GetCursorScreenPos();
            auto*        windowService   = ctx.services ? ctx.services->tryGet<IWindowService>() : nullptr;
            const bool   decoratedWindow = windowService && windowService->window().isDecorated();

            drawEngineMark(ImVec2 {start.x + vultra::ui::dp(28.0f), start.y + vultra::ui::dp(kTitleLogoY)},
                           vultra::ui::dp(20.0f));

            ImGui::SetCursorScreenPos(ImVec2 {start.x + vultra::ui::dp(64.0f), start.y + vultra::ui::dp(kTitleTextY)});
            ImGui::TextUnformatted(vultra::tr("topbar.title"));

            ImGui::SetCursorScreenPos(ImVec2 {start.x + vultra::ui::dp(12.0f), start.y + vultra::ui::dp(kMenuRowY)});
            if (titleMenuButton(vultra::trId("menu.file.title", "menuFile")))
            {
                if (ImGui::MenuItem(vultra::tr("menu.file.newScene")) && actions.newBlankScene)
                    actions.newBlankScene(ctx);
                if (ImGui::MenuItem(vultra::tr("menu.file.saveScene"), "Ctrl+S") && actions.saveScene)
                    actions.saveScene(ctx);
                if (ImGui::MenuItem(vultra::tr("menu.file.exportRun"), "F5") && actions.buildAndRun)
                    actions.buildAndRun(ctx);
                if (ImGui::MenuItem(vultra::tr("menu.file.exportSettings")))
                    ctx.state.buildSettingsOpen = true;
                if (ImGui::MenuItem(vultra::tr("menu.file.backToLauncher")) && actions.backToLauncher)
                    actions.backToLauncher(ctx);
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, vultra::ui::dp(2.0f));
            if (titleMenuButton(vultra::trId("menu.edit.title", "menuEdit")))
            {
                if (ImGui::MenuItem(vultra::tr("menu.edit.projectSettings")))
                    ctx.state.projectSettingsOpen = true;
                if (ImGui::MenuItem(vultra::tr("menu.edit.editorSettings")))
                    ctx.state.editorSettingsOpen = true;
                ImGui::Separator();
                if (ImGui::MenuItem(vultra::tr("menu.edit.resetLayout")) && actions.resetLayout)
                    actions.resetLayout(ctx);
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, vultra::ui::dp(2.0f));
            if (titleMenuButton(vultra::trId("menu.window.title", "menuWindow")))
            {
                if (ImGui::MenuItem(vultra::tr("menu.window.resetLayout")) && actions.resetLayout)
                    actions.resetLayout(ctx);
                ImGui::Separator();

                for (const auto& window : windows)
                    ImGui::MenuItem(window->displayName().c_str(), nullptr, &window->open());
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, vultra::ui::dp(2.0f));
            if (titleMenuButton(vultra::trId("menu.tools.title", "menuTools")))
            {
                ImGui::MenuItem(vultra::tr("menu.tools.import"), nullptr, false, false);
                ImGui::MenuItem(vultra::tr("menu.tools.saveAll"), nullptr, false, false);
                ImGui::Separator();
                if (ImGui::MenuItem(vultra::tr("menu.tools.profiler")))
                    ctx.state.profilerWindowOpenRequested = true;
                if (ImGui::MenuItem(vultra::tr("menu.tools.frameDebugger")))
                    ctx.state.frameDebuggerWindowOpenRequested = true;
                if (ImGui::MenuItem(vultra::tr("menu.tools.runtimeFrameGraph")))
                    ctx.state.runtimeFrameGraphViewerOpenRequested = true;
                if (ImGui::MenuItem(vultra::tr("menu.tools.worldViewer")))
                    ctx.state.editorWindowFocusRequested = "World Viewer";
                drawRenderDocMenu(ctx);
                drawScriptedToolsMenu(ctx); // plugin / Lua-registered menu items
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, vultra::ui::dp(2.0f));
            if (titleMenuButton(vultra::trId("menu.help.title", "menuHelp")))
            {
                if (ImGui::MenuItem(vultra::tr("menu.help.about")) && actions.showAbout)
                    actions.showAbout(ctx);
                ImGui::EndPopup();
            }

            if (!decoratedWindow)
            {
                ImGui::SetCursorScreenPos(
                    ImVec2 {start.x + ImGui::GetWindowWidth() - vultra::ui::dp(116.0f), start.y + vultra::ui::dp(10.0f)});
                if (titleBarWindowButton(ICON_MDI_WINDOW_MINIMIZE, vultra::tr("window.minimize")) && windowService)
                    windowService->window().minimize();
                ImGui::SameLine(0.0f, 0.0f);
                const bool maximized =
                    windowService && (windowService->window().isFullscreen() || windowService->window().isMaximized());
                if (titleBarWindowButton(maximized ? ICON_MDI_WINDOW_RESTORE : ICON_MDI_WINDOW_MAXIMIZE,
                                         maximized ? vultra::tr("window.restore") : vultra::tr("window.maximize")) &&
                    windowService)
                {
                    if (maximized)
                    {
                        // Drop any legacy fullscreen state first, then un-maximize back to the windowed rect.
                        if (windowService->window().isFullscreen())
                            windowService->window().setFullscreen(false);
                        windowService->window().restore();
                    }
                    else
                    {
                        // Real maximize (not a fullscreen hack): SDL sizes a borderless window to the
                        // monitor work area, so the OS task bar stays visible.
                        windowService->window().maximize();
                    }
                }
                ImGui::SameLine(0.0f, 0.0f);
                if (titleBarWindowButton(ICON_MDI_CLOSE, vultra::tr("window.close"), true) && windowService)
                    windowService->window().close();
            }

            ImGui::End();
        }

        if (ImGui::BeginViewportSideBar(
                "##VultraEditorToolBar", viewport, ImGuiDir_Up, vultra::ui::dp(kToolBarHeight), barFlags))
        {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2 {start.x + vultra::ui::dp(12.0f), start.y + vultra::ui::dp(7.0f)});

            toolbarButton(ICON_MDI_VIEW_DASHBOARD, vultra::tr("toolbar.contentDrawer"), ImVec2 {vultra::ui::dp(30.0f), 0.0f});
            ImGui::SameLine(0.0f, vultra::ui::dp(6.0f));
            toolbarButton(ICON_MDI_ENGINE_OUTLINE, vultra::tr("toolbar.editorTools"), ImVec2 {vultra::ui::dp(30.0f), 0.0f});
            ImGui::SameLine(0.0f, vultra::ui::dp(10.0f));

            const std::string projectLabel =
                ctx.state.currentProjectName.empty() ? vultra::tr("toolbar.noProject") : ctx.state.currentProjectName;
            ImGui::SetNextItemWidth(vultra::ui::dp(158.0f));
            if (ImGui::BeginCombo("##ProjectSelector", projectLabel.c_str(), ImGuiComboFlags_NoArrowButton))
            {
                ImGui::TextDisabled("%s",
                                    ctx.state.currentProject.empty() ?
                                        vultra::tr("toolbar.noProjectLoaded") :
                                        ctx.state.currentProject.generic_string().c_str());
                if (ImGui::Selectable(vultra::tr("toolbar.backToLauncher")) && actions.backToLauncher)
                    actions.backToLauncher(ctx);
                ImGui::EndCombo();
            }

            ImGui::SameLine(0.0f, vultra::ui::dp(12.0f));
            toolbarButton(ICON_MDI_COG_TRANSFER_OUTLINE " " ICON_MDI_MENU_DOWN,
                          vultra::tr("toolbar.editorTools"),
                          ImVec2 {vultra::ui::dp(46.0f), 0.0f});
            ImGui::SameLine(0.0f, vultra::ui::dp(6.0f));
            toolbarButton(ICON_MDI_SOURCE_BRANCH " " ICON_MDI_MENU_DOWN,
                          vultra::tr("toolbar.graphTools"),
                          ImVec2 {vultra::ui::dp(46.0f), 0.0f});
            ImGui::SameLine(0.0f, vultra::ui::dp(12.0f));
            drawPlaybackControls(ctx);

            ImGui::SameLine(0.0f, vultra::ui::dp(12.0f));
            const std::string exportRunLabel = std::string {ICON_MDI_ROCKET_LAUNCH "  "} + vultra::tr("toolbar.exportRun");
            if (toolbarButton(
                    exportRunLabel.c_str(), vultra::tr("toolbar.exportRunTooltip"), ImVec2 {vultra::ui::dp(126.0f), 0.0f}) &&
                actions.buildAndRun)
            {
                actions.buildAndRun(ctx);
            }

            ImGui::SameLine(0.0f, vultra::ui::dp(12.0f));
            const std::string platformLabel =
                std::string {ICON_MDI_MONITOR "  "} + vultra::tr("toolbar.platforms") + " " ICON_MDI_MENU_DOWN;
            const std::string settingsLabel = std::string {ICON_MDI_COG "  "} + vultra::tr("toolbar.settings");
            const float       platformWidth = ImGui::CalcTextSize(platformLabel.c_str()).x + vultra::ui::dp(26.0f);
            const float       settingsWidth = ImGui::CalcTextSize(settingsLabel.c_str()).x + vultra::ui::dp(26.0f);
            const float       settingsStart = ImGui::GetWindowWidth() - settingsWidth - vultra::ui::dp(18.0f);
            if (ImGui::GetCursorPosX() + platformWidth + vultra::ui::dp(20.0f) < settingsStart)
            {
                toolbarButton(platformLabel.c_str(), vultra::tr("toolbar.targetPlatform"), ImVec2 {platformWidth, 0.0f});
                ImGui::SameLine(0.0f, vultra::ui::dp(8.0f));
            }
            if (settingsStart > ImGui::GetCursorPosX())
                ImGui::SetCursorPosX(settingsStart);
            if (toolbarButton(settingsLabel.c_str(), vultra::tr("toolbar.settings"), ImVec2 {settingsWidth, 0.0f}))
                ImGui::OpenPopup("SettingsMenu");
            if (ImGui::BeginPopup("SettingsMenu"))
            {
                if (ImGui::MenuItem(vultra::tr("menu.edit.projectSettings")))
                    ctx.state.projectSettingsOpen = true;
                if (ImGui::MenuItem(vultra::tr("menu.edit.editorSettings")))
                    ctx.state.editorSettingsOpen = true;
                if (ImGui::MenuItem(vultra::tr("menu.file.exportSettings")))
                    ctx.state.buildSettingsOpen = true;
                ImGui::EndPopup();
            }

            ImGui::End();
        }
    }
} // namespace vultra_app
