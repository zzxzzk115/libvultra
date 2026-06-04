#include "editor_app/editor_app.hpp"

#include "editor_app/editor_settings_persistence.hpp"
#include "editor_app/project_asset_utils.hpp"
#include "editor_app/ui/settings_widgets.hpp"
#include "vproject.hpp"

#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/render_service.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace vultra_app
{
    namespace
    {
        template<std::size_t N>
        void setBuffer(std::array<char, N>& buffer, const std::string& value)
        {
            std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
        }

        template<std::size_t N>
        std::string bufferString(const std::array<char, N>& buffer)
        {
            return std::string {buffer.data()};
        }

        std::string rendererKeyFromRenderGraphUri(std::string_view uri)
        {
            auto                       filename = std::filesystem::path(std::string(uri)).filename().generic_string();
            constexpr std::string_view suffix   = ".vrg.json";
            if (filename.ends_with(suffix))
                filename.resize(filename.size() - suffix.size());
            if (filename.empty())
                filename = "custom";
            for (auto& ch : filename)
            {
                if (ch == '-' || ch == ' ')
                    ch = '_';
                else
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return filename;
        }

        void applyProjectSettingsFromBuffers(EditorContext&               ctx,
                                             const std::array<char, 128>& nameBuffer,
                                             const std::array<char, 256>& assetRootBuffer,
                                             const std::array<char, 256>& defaultSceneBuffer,
                                             const std::array<char, 256>& editingRenderGraphBuffer)
        {
            const auto previousAssetRoot   = ctx.state.currentAssetRoot;
            const auto previousRenderGraph = ctx.state.currentEditingRenderGraph;

            ctx.state.currentProjectName        = bufferString(nameBuffer);
            ctx.state.currentAssetRoot          = bufferString(assetRootBuffer);
            ctx.state.currentDefaultScene       = bufferString(defaultSceneBuffer);
            ctx.state.currentEditingRenderGraph = bufferString(editingRenderGraphBuffer);
            ctx.state.buildSettings.projectName = ctx.state.currentProjectName;

            if (ctx.state.currentAssetRoot != previousAssetRoot)
                ++ctx.state.projectGeneration;

            if (ctx.state.currentEditingRenderGraph != previousRenderGraph)
            {
                if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
                {
                    const auto rendererKey = rendererKeyFromRenderGraphUri(ctx.state.currentEditingRenderGraph);
                    renderService->reloadRenderPipeline(ctx.state.currentEditingRenderGraph, rendererKey);
                }
            }
        }

        std::string currentHostPlatform()
        {
#if defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#elif defined(__linux__)
            return "Linux";
#else
            return "Unknown";
#endif
        }

        const char* platformIcon(const std::string& platform)
        {
            if (platform == "Windows")
                return ICON_MDI_MICROSOFT_WINDOWS;
            if (platform == "Linux")
                return ICON_MDI_LINUX;
            if (platform == "Android")
                return ICON_MDI_ANDROID;
            if (platform == "macOS")
                return ICON_MDI_APPLE;
            if (platform == "WebGPU")
                return ICON_MDI_WEB;
            return ICON_MDI_MONITOR;
        }

        std::string platformLabel(const std::string& platform)
        {
            return std::string(platformIcon(platform)) + "  " + platform;
        }

        std::vector<std::string> collectProjectRenderGraphUris(const std::filesystem::path& projectRoot,
                                                               const std::string&           assetRootName)
        {
            return collectProjectAssetUrisWithSuffix(projectRoot, assetRootName, ".vrg.json");
        }

        std::vector<std::string> collectAssetUrisWithExtension(const std::filesystem::path& projectRoot,
                                                               const std::string&           assetRootName,
                                                               const std::string&           extension)
        {
            return collectProjectAssetUrisWithExtension(projectRoot, assetRootName, extension);
        }

        std::string projectRelativePath(const std::filesystem::path& projectRoot, const std::string& value)
        {
            if (projectRoot.empty() || value.empty())
                return value;

            std::filesystem::path path {value};
            if (!path.is_absolute())
                return path.generic_string();

            std::error_code ec;
            const auto rel = std::filesystem::relative(path.lexically_normal(), projectRoot.lexically_normal(), ec);
            return ec || rel.empty() ? path.generic_string() : rel.generic_string();
        }

        void reindexBuildScenes(std::vector<VBuildScene>& scenes)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(scenes.size()); ++i)
                scenes[i].index = i;
        }

        bool buildSceneContainsUri(const std::vector<VBuildScene>& scenes, const std::string& uri)
        {
            return std::any_of(scenes.begin(), scenes.end(), [&](const VBuildScene& scene) {
                return scene.uri == uri;
            });
        }

    } // namespace

    void EditorApp::drawProjectSettingsPopup(EditorContext& ctx)
    {
        static int selectedPage = 0;
        if (ctx.state.projectSettingsOpen)
        {
            setBuffer(m_ProjectNameBuffer, ctx.state.currentProjectName);
            setBuffer(m_ProjectAssetRootBuffer, ctx.state.currentAssetRoot);
            setBuffer(m_ProjectDefaultSceneBuffer, ctx.state.currentDefaultScene);
            setBuffer(m_ProjectEditingRenderGraphBuffer, ctx.state.currentEditingRenderGraph);
            ImGui::OpenPopup("Project Settings");
            ctx.state.projectSettingsOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        ImGui::SetNextWindowSize(ImVec2 {760.0f, 520.0f}, ImGuiCond_Appearing);
        bool popupOpen = true;
        if (!ImGui::BeginPopupModal("Project Settings", &popupOpen))
            return;
        if (!popupOpen)
        {
            ImGui::EndPopup();
            return;
        }

        ImGui::SetNextItemWidth(-1.0f);
        static char search[128] {};
        ImGui::InputTextWithHint(
            "##ProjectSettingsSearch", ICON_MDI_MAGNIFY " Search settings...", search, sizeof(search));
        ImGui::Separator();

        ImGui::BeginChild("ProjectSettingsNav", ImVec2 {180.0f, -42.0f}, true);
        ImGui::TextUnformatted("Project");
        if (ui::settingsNavItem("Project Info", selectedPage == 0))
            selectedPage = 0;
        if (ui::settingsNavItem("Render Settings", selectedPage == 1))
            selectedPage = 1;
        if (ui::settingsNavItem("Build Scenes", selectedPage == 2))
            selectedPage = 2;
        if (ui::settingsNavItem("Packaging", selectedPage == 3))
            selectedPage = 3;
        ImGui::Spacing();
        ImGui::TextUnformatted("Engine");
        ImGui::BeginDisabled();
        ui::settingsNavItem("General", false);
        ui::settingsNavItem("Rendering", false);
        ui::settingsNavItem("Materials", false);
        ui::settingsNavItem("Scripting", false);
        ImGui::EndDisabled();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ProjectSettingsContent", ImVec2 {0.0f, -42.0f}, true);
        bool projectSettingsChanged = false;
        if (selectedPage == 0)
        {
            ui::drawSettingsSectionHeader("Project Info");
            ui::beginSettingsRow("Project Name");
            projectSettingsChanged |=
                ImGui::InputText("##ProjectName", m_ProjectNameBuffer.data(), m_ProjectNameBuffer.size());
            ui::endSettingsRow();
            ui::beginSettingsRow("Asset Root");
            if (m_ProjectAssetRootDialog.drawBrowseOnly(
                    "", m_ProjectAssetRootBuffer.data(), m_ProjectAssetRootBuffer.size()))
            {
                setBuffer(m_ProjectAssetRootBuffer,
                          projectRelativePath(ctx.state.currentProject, bufferString(m_ProjectAssetRootBuffer)));
                projectSettingsChanged = true;
            }
            ui::endSettingsRow();

            auto sceneUris = collectAssetUrisWithExtension(
                ctx.state.currentProject, bufferString(m_ProjectAssetRootBuffer), ".vscn");
            if (!bufferString(m_ProjectDefaultSceneBuffer).empty() &&
                std::find(sceneUris.begin(), sceneUris.end(), bufferString(m_ProjectDefaultSceneBuffer)) ==
                    sceneUris.end())
                sceneUris.push_back(bufferString(m_ProjectDefaultSceneBuffer));
            std::sort(sceneUris.begin(), sceneUris.end());
            ui::beginSettingsRow("Default Scene");
            if (ImGui::BeginCombo(
                    "##DefaultScene",
                    bufferString(m_ProjectDefaultSceneBuffer).empty() ? "(none)" : m_ProjectDefaultSceneBuffer.data()))
            {
                for (const auto& uri : sceneUris)
                {
                    const bool selected = uri == bufferString(m_ProjectDefaultSceneBuffer);
                    if (ImGui::Selectable(uri.c_str(), selected))
                    {
                        setBuffer(m_ProjectDefaultSceneBuffer, uri);
                        projectSettingsChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ui::endSettingsRow();
            ui::drawInfoRegion(ctx.state.currentProject.empty() ? "No project loaded" :
                                                                  ctx.state.currentProject.generic_string().c_str());
        }
        else if (selectedPage == 1)
        {
            ui::drawSettingsSectionHeader("Render Settings");
            auto renderGraphUris =
                collectProjectRenderGraphUris(ctx.state.currentProject, bufferString(m_ProjectAssetRootBuffer));
            if (!bufferString(m_ProjectEditingRenderGraphBuffer).empty() &&
                std::find(renderGraphUris.begin(),
                          renderGraphUris.end(),
                          bufferString(m_ProjectEditingRenderGraphBuffer)) == renderGraphUris.end())
                renderGraphUris.push_back(bufferString(m_ProjectEditingRenderGraphBuffer));
            std::sort(renderGraphUris.begin(), renderGraphUris.end());
            ui::beginSettingsRow("Editing Render Graph");
            if (ImGui::BeginCombo("##EditingRenderGraph",
                                  bufferString(m_ProjectEditingRenderGraphBuffer).empty() ?
                                      "(none)" :
                                      m_ProjectEditingRenderGraphBuffer.data()))
            {
                for (const auto& uri : renderGraphUris)
                {
                    const bool selected = uri == bufferString(m_ProjectEditingRenderGraphBuffer);
                    if (ImGui::Selectable(uri.c_str(), selected))
                    {
                        setBuffer(m_ProjectEditingRenderGraphBuffer, uri);
                        projectSettingsChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ui::endSettingsRow();

            if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
            {
                auto& outline = renderService->builtinRenderSettings().selectionOutline;
                ImGui::Spacing();
                ui::drawSettingsSectionHeader("Editor Selection");
                ImGui::Checkbox("Selection Outline", &outline.enabled);
                ImGui::ColorEdit3("Outline Color", &outline.color.x);
                ImGui::SliderFloat("Outline Thickness", &outline.thickness, 1.0f, 8.0f, "%.0f px");
                ImGui::SliderFloat("Fill Opacity", &outline.fillOpacity, 0.0f, 0.25f, "%.2f");
                ImGui::SliderFloat("Edge Opacity", &outline.edgeOpacity, 0.0f, 1.0f, "%.2f");
            }
        }
        else if (selectedPage == 2)
        {
            ui::drawSettingsSectionHeader("Build Scenes");
            auto sceneUris = collectAssetUrisWithExtension(
                ctx.state.currentProject, bufferString(m_ProjectAssetRootBuffer), ".vscn");
            const auto defaultScene = bufferString(m_ProjectDefaultSceneBuffer);
            if (!defaultScene.empty() && std::find(sceneUris.begin(), sceneUris.end(), defaultScene) == sceneUris.end())
                sceneUris.push_back(defaultScene);
            std::sort(sceneUris.begin(), sceneUris.end());

            if (ctx.state.currentBuildScenes.empty() && !defaultScene.empty())
                ctx.state.currentBuildScenes =
                    normalizedBuildScenes(defaultScene, ctx.state.currentBuildScenes);

            if (ImGui::Button(ICON_MDI_PLUS "  Add Default", ImVec2 {128.0f, 0.0f}))
            {
                if (!defaultScene.empty() && !buildSceneContainsUri(ctx.state.currentBuildScenes, defaultScene))
                {
                    ctx.state.currentBuildScenes.push_back(VBuildScene {
                        .index   = static_cast<uint32_t>(ctx.state.currentBuildScenes.size()),
                        .uri     = defaultScene,
                        .enabled = true,
                    });
                    projectSettingsChanged = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_MDI_PLUS "  Add All", ImVec2 {112.0f, 0.0f}))
            {
                for (const auto& uri : sceneUris)
                {
                    if (buildSceneContainsUri(ctx.state.currentBuildScenes, uri))
                        continue;
                    ctx.state.currentBuildScenes.push_back(VBuildScene {
                        .index   = static_cast<uint32_t>(ctx.state.currentBuildScenes.size()),
                        .uri     = uri,
                        .enabled = true,
                    });
                    projectSettingsChanged = true;
                }
            }

            ImGui::Spacing();
            if (ctx.state.currentBuildScenes.empty())
            {
                ui::drawInfoRegion("No build scenes configured. Add scenes to control package roots and runtime scene indices.");
            }
            else if (ImGui::BeginTable("BuildScenesTable",
                                       7,
                                       ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f);
                ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.26f);
                ImGui::TableSetupColumn("Alias", ImGuiTableColumnFlags_WidthStretch, 0.26f);
                ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch, 0.48f);
                ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 62.0f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
                ImGui::TableHeadersRow();

                int removeIndex = -1;
                int moveFrom    = -1;
                int moveTo      = -1;
                for (int i = 0; i < static_cast<int>(ctx.state.currentBuildScenes.size()); ++i)
                {
                    auto& scene = ctx.state.currentBuildScenes[static_cast<size_t>(i)];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", scene.index);

                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::Checkbox("##BuildSceneEnabled", &scene.enabled))
                        projectSettingsChanged = true;

                    // Name is locked to the scene's filename stem (not user-editable).
                    ImGui::TableSetColumnIndex(2);
                    std::array<char, 128> nameBuffer {};
                    setBuffer(nameBuffer, buildSceneName(scene.uri));
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputText("##BuildSceneName",
                                     nameBuffer.data(),
                                     nameBuffer.size(),
                                     ImGuiInputTextFlags_ReadOnly);

                    // Alias is the optional, player-defined handle.
                    ImGui::TableSetColumnIndex(3);
                    std::array<char, 128> aliasBuffer {};
                    setBuffer(aliasBuffer, scene.alias);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##BuildSceneAlias", aliasBuffer.data(), aliasBuffer.size()))
                    {
                        scene.alias            = bufferString(aliasBuffer);
                        projectSettingsChanged = true;
                    }

                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::BeginCombo("##BuildSceneUri", scene.uri.empty() ? "(none)" : scene.uri.c_str()))
                    {
                        for (const auto& uri : sceneUris)
                        {
                            const bool selected = uri == scene.uri;
                            if (ImGui::Selectable(uri.c_str(), selected))
                            {
                                scene.uri              = uri;
                                projectSettingsChanged = true;
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::TableSetColumnIndex(5);
                    if (i == 0)
                        ImGui::BeginDisabled();
                    if (ImGui::SmallButton(ICON_MDI_ARROW_UP))
                    {
                        moveFrom = i;
                        moveTo   = i - 1;
                    }
                    if (i == 0)
                        ImGui::EndDisabled();
                    ImGui::SameLine(0.0f, 4.0f);
                    if (i + 1 == static_cast<int>(ctx.state.currentBuildScenes.size()))
                        ImGui::BeginDisabled();
                    if (ImGui::SmallButton(ICON_MDI_ARROW_DOWN))
                    {
                        moveFrom = i;
                        moveTo   = i + 1;
                    }
                    if (i + 1 == static_cast<int>(ctx.state.currentBuildScenes.size()))
                        ImGui::EndDisabled();

                    ImGui::TableSetColumnIndex(6);
                    if (ImGui::SmallButton(ICON_MDI_DELETE_OUTLINE))
                        removeIndex = i;
                    ImGui::PopID();
                }

                ImGui::EndTable();

                if (removeIndex >= 0)
                {
                    ctx.state.currentBuildScenes.erase(ctx.state.currentBuildScenes.begin() + removeIndex);
                    reindexBuildScenes(ctx.state.currentBuildScenes);
                    projectSettingsChanged = true;
                }
                if (moveFrom >= 0 && moveTo >= 0)
                {
                    std::swap(ctx.state.currentBuildScenes[static_cast<size_t>(moveFrom)],
                              ctx.state.currentBuildScenes[static_cast<size_t>(moveTo)]);
                    reindexBuildScenes(ctx.state.currentBuildScenes);
                    projectSettingsChanged = true;
                }
            }
            ui::drawInfoRegion("Enabled build scenes are exported as dependency roots in scene-index order.");
        }
        else
        {
            ui::drawSettingsSectionHeader("Packaging");
            ui::beginSettingsRow("Package Name");
            ImGui::TextUnformatted(m_ProjectNameBuffer.data());
            ui::endSettingsRow();
            ui::beginSettingsRow("Package Manifest");
            ImGui::TextUnformatted(kVPackageManifestPath);
            ui::endSettingsRow();
            ui::drawInfoRegion("Runtime packages use a same-name executable and VPK.");
        }
        ImGui::EndChild();
        if (projectSettingsChanged)
        {
            applyProjectSettingsFromBuffers(ctx,
                                            m_ProjectNameBuffer,
                                            m_ProjectAssetRootBuffer,
                                            m_ProjectDefaultSceneBuffer,
                                            m_ProjectEditingRenderGraphBuffer);
            ctx.state.statusMessage = "Project settings changed.";
        }

        if (ImGui::Button("Reset to Defaults", ImVec2 {132.0f, 0.0f}))
        {
            setBuffer(m_ProjectAssetRootBuffer, "resources");
            setBuffer(m_ProjectDefaultSceneBuffer, "");
            setBuffer(m_ProjectEditingRenderGraphBuffer, "res://render/default.vrg.json");
            ctx.state.currentBuildScenes.clear();
            applyProjectSettingsFromBuffers(ctx,
                                            m_ProjectNameBuffer,
                                            m_ProjectAssetRootBuffer,
                                            m_ProjectDefaultSceneBuffer,
                                            m_ProjectEditingRenderGraphBuffer);
            ctx.state.statusMessage = "Project settings reset.";
        }
        ui::alignSettingsButtonGroup(2);
        if (ImGui::Button("Save", ImVec2 {82.0f, 0.0f}))
        {
            VProject project {
                .projectDir         = ctx.state.currentProject,
                .name               = ctx.state.currentProjectName,
                .assetRoot          = ctx.state.currentAssetRoot,
                .defaultScene       = ctx.state.currentDefaultScene,
                .buildScenes        = normalizedBuildScenes(ctx.state.currentDefaultScene, ctx.state.currentBuildScenes),
                .editingRenderGraph = ctx.state.currentEditingRenderGraph,
            };
            std::string error;
            if (saveVProject(project, &error))
                ctx.state.statusMessage = "Saved project settings.";
            else
                ctx.state.statusMessage = "Project settings save failed: " + error;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2 {82.0f, 0.0f}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void EditorApp::drawEditorSettingsPopup(EditorContext& ctx)
    {
        static int selectedPage = 0;
        if (ctx.state.editorSettingsOpen)
        {
            setBuffer(m_ExternalEditorBuffer, ctx.state.editorSettings.externalEditor);
            setBuffer(m_AgentMcpServerNameBuffer, ctx.state.editorSettings.mcpServerName);
            setBuffer(m_AgentMcpHostBuffer, ctx.state.editorSettings.mcpHost);
            setBuffer(m_AgentEndpointBuffer, ctx.state.editorSettings.agentEndpoint);
            setBuffer(m_AgentModelBuffer, ctx.state.editorSettings.agentModel);
            ImGui::OpenPopup("Editor Settings");
            ctx.state.editorSettingsOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        ImGui::SetNextWindowSize(ImVec2 {760.0f, 520.0f}, ImGuiCond_Appearing);
        bool popupOpen = true;
        if (!ImGui::BeginPopupModal("Editor Settings", &popupOpen))
            return;
        if (!popupOpen)
        {
            ImGui::EndPopup();
            return;
        }

        ImGui::SetNextItemWidth(-1.0f);
        static char search[128] {};
        ImGui::InputTextWithHint(
            "##EditorSettingsSearch", ICON_MDI_MAGNIFY " Search settings...", search, sizeof(search));
        ImGui::Separator();

        ImGui::BeginChild("EditorSettingsNav", ImVec2 {180.0f, -42.0f}, true);
        ImGui::TextUnformatted("General");
        if (ui::settingsNavItem("Appearance", selectedPage == 0))
            selectedPage = 0;
        if (ui::settingsNavItem("Fonts", selectedPage == 1))
            selectedPage = 1;
        if (ui::settingsNavItem("External Editor", selectedPage == 2))
            selectedPage = 2;
        if (ui::settingsNavItem("AI Agent", selectedPage == 3))
            selectedPage = 3;
        ImGui::Spacing();
        ImGui::TextUnformatted("Advanced");
        ImGui::BeginDisabled();
        ui::settingsNavItem("Files & Paths", false);
        ui::settingsNavItem("Console", false);
        ui::settingsNavItem("Privacy", false);
        ImGui::EndDisabled();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("EditorSettingsContent", ImVec2 {0.0f, -42.0f}, true);
        auto& settings = ctx.state.editorSettings;
        if (selectedPage == 0)
        {
            ui::drawSettingsSectionHeader("Appearance");
            const char* themes[]   = {"Dark", "Graphite", "Light", "Custom"};
            int         themeIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(themes); ++i)
            {
                if (settings.theme == themes[i])
                {
                    themeIndex = i;
                    break;
                }
            }
            ui::beginSettingsRow("Color Theme");
            if (ImGui::Combo("##ColorTheme", &themeIndex, themes, IM_ARRAYSIZE(themes)))
            {
                settings.theme          = themes[themeIndex];
                ctx.state.statusMessage = "Editor theme changed: " + settings.theme;
            }
            ui::endSettingsRow();
            if (settings.theme == "Custom")
            {
                ui::beginSettingsRow("Background");
                ImGui::ColorEdit3("##CustomThemeBackground", &settings.customThemeBackground.x);
                ui::endSettingsRow();
                ui::beginSettingsRow("Panel");
                ImGui::ColorEdit3("##CustomThemePanel", &settings.customThemePanel.x);
                ui::endSettingsRow();
                ui::beginSettingsRow("Text");
                ImGui::ColorEdit3("##CustomThemeText", &settings.customThemeText.x);
                ui::endSettingsRow();
                ui::beginSettingsRow("Accent");
                ImGui::ColorEdit3("##CustomThemeAccent", &settings.customThemeAccent.x);
                ui::endSettingsRow();
            }
            ui::beginSettingsRow("Application Scale");
            ImGui::SliderFloat("##ApplicationScale", &settings.applicationScale, 0.75f, 2.0f, "%.2fx");
            ui::endSettingsRow();
            ui::beginSettingsRow("Text Scale");
            ImGui::SliderFloat("##TextScale", &settings.textScale, 0.75f, 2.0f, "%.2fx");
            ui::endSettingsRow();
            ImGui::Checkbox("Show Splash Screen on Startup", &settings.showSplashOnStartup);
            ImGui::Checkbox("Enable Animations", &settings.enableAnimations);
        }
        else if (selectedPage == 1)
        {
            ui::drawSettingsSectionHeader("Fonts");
            ui::beginSettingsRow("Interface Font");
            ImGui::TextUnformatted(settings.interfaceFont.c_str());
            ui::endSettingsRow();
            ui::beginSettingsRow("Interface Font Size");
            ImGui::SliderInt("##InterfaceFontSize", &settings.interfaceFontSize, 10, 24, "%d px");
            ui::endSettingsRow();
            ui::beginSettingsRow("Monospace Font");
            ImGui::TextUnformatted(settings.monospaceFont.c_str());
            ui::endSettingsRow();
            ui::beginSettingsRow("Monospace Font Size");
            ImGui::SliderInt("##MonospaceFontSize", &settings.monospaceFontSize, 10, 24, "%d px");
            ui::endSettingsRow();
            ImGui::Checkbox("Use System Fonts", &settings.useSystemFonts);
        }
        else if (selectedPage == 2)
        {
            ui::drawSettingsSectionHeader("External Editor");
            ui::beginSettingsRow("Executable");
            if (m_ExternalEditorDialog.drawBrowseOnly("", m_ExternalEditorBuffer.data(), m_ExternalEditorBuffer.size()))
                ctx.state.editorSettings.externalEditor = bufferString(m_ExternalEditorBuffer);
            ui::endSettingsRow();
            ui::drawInfoRegion("Used by source asset actions when an external editor command is available.");
        }
        else
        {
            ui::drawSettingsSectionHeader("AI Agent");
            ImGui::Checkbox("Enable Agent Panel", &settings.enableAgent);
            ImGui::Checkbox("Auto-start MCP Server", &settings.autoStartMcp);
            ui::beginSettingsRow("MCP Server Name");
            if (ImGui::InputText(
                    "##McpServerName", m_AgentMcpServerNameBuffer.data(), m_AgentMcpServerNameBuffer.size()))
                settings.mcpServerName = bufferString(m_AgentMcpServerNameBuffer);
            ui::endSettingsRow();
            ui::beginSettingsRow("MCP Host");
            if (ImGui::InputText("##McpHost", m_AgentMcpHostBuffer.data(), m_AgentMcpHostBuffer.size()))
                settings.mcpHost = bufferString(m_AgentMcpHostBuffer);
            ui::endSettingsRow();
            ui::beginSettingsRow("MCP Port");
            ImGui::InputInt("##McpPort", &settings.mcpPort);
            settings.mcpPort = std::clamp(settings.mcpPort, 1, 65535);
            ui::endSettingsRow();
            ImGui::Spacing();
            ui::drawSettingsSectionHeader("Agent Client");
            ui::beginSettingsRow("Endpoint");
            if (ImGui::InputText("##AgentEndpoint", m_AgentEndpointBuffer.data(), m_AgentEndpointBuffer.size()))
                settings.agentEndpoint = bufferString(m_AgentEndpointBuffer);
            ui::endSettingsRow();
            ui::beginSettingsRow("Model");
            if (ImGui::InputText("##AgentModel", m_AgentModelBuffer.data(), m_AgentModelBuffer.size()))
                settings.agentModel = bufferString(m_AgentModelBuffer);
            ui::endSettingsRow();
            ImGui::Spacing();
            ui::drawSettingsSectionHeader("Operation Guardrails");
            ImGui::Checkbox("Allow Project Operations", &settings.allowAgentProjectOperations);
            ImGui::Checkbox("Allow Engine Operations", &settings.allowAgentEngineOperations);
            ImGui::Checkbox("Require Confirmation Before Writes", &settings.requireAgentConfirmation);
            ui::drawInfoRegion("This page only stores agent and MCP preferences. Runtime startup, chat, tool calls, "
                               "and editor operations should live in a dedicated agent service.");
        }
        ImGui::EndChild();

        if (ImGui::Button("Reset to Defaults", ImVec2 {132.0f, 0.0f}))
        {
            ctx.state.editorSettings = AppState::EditorSettings {};
            setBuffer(m_ExternalEditorBuffer, {});
            setBuffer(m_AgentMcpServerNameBuffer, ctx.state.editorSettings.mcpServerName);
            setBuffer(m_AgentMcpHostBuffer, ctx.state.editorSettings.mcpHost);
            setBuffer(m_AgentEndpointBuffer, {});
            setBuffer(m_AgentModelBuffer, {});
            ctx.state.statusMessage = "Editor settings reset.";
        }
        ui::alignSettingsButtonGroup(2);
        if (ImGui::Button("Save", ImVec2 {82.0f, 0.0f}))
        {
            ctx.state.editorSettings.externalEditor = bufferString(m_ExternalEditorBuffer);
            ctx.state.editorSettings.mcpServerName  = bufferString(m_AgentMcpServerNameBuffer);
            ctx.state.editorSettings.mcpHost        = bufferString(m_AgentMcpHostBuffer);
            ctx.state.editorSettings.agentEndpoint  = bufferString(m_AgentEndpointBuffer);
            ctx.state.editorSettings.agentModel     = bufferString(m_AgentModelBuffer);
            std::string error;
            if (saveEditorSettings(ctx.state.editorSettingsFile, ctx.state.editorSettings, &error))
                ctx.state.statusMessage = "Saved editor settings.";
            else
                ctx.state.statusMessage = "Editor settings save failed: " + error;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2 {82.0f, 0.0f}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void EditorApp::drawBuildSettingsPopup(EditorContext& ctx)
    {
        if (ctx.state.buildSettingsOpen)
        {
            if (ctx.state.buildSettings.projectName.empty())
                ctx.state.buildSettings.projectName = ctx.state.currentProjectName;
            setBuffer(m_BuildOutputFolderBuffer, ctx.state.buildSettings.outputDirectory);
            setBuffer(m_BuildProjectNameBuffer, ctx.state.buildSettings.projectName);
            setBuffer(m_ExportTemplateBuffer, ctx.state.buildSettings.exportTemplatePath);
            setBuffer(m_BuildExtraArgsBuffer, ctx.state.buildSettings.additionalCommandLineArguments);
            ImGui::OpenPopup("Export Settings");
            ctx.state.buildSettingsOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        ImGui::SetNextWindowSize(ImVec2 {920.0f, 430.0f}, ImGuiCond_Appearing);
        bool popupOpen = true;
        if (!ImGui::BeginPopupModal("Export Settings", &popupOpen))
            return;
        if (!popupOpen)
        {
            ImGui::EndPopup();
            return;
        }

        auto& settings = ctx.state.buildSettings;
        ImGui::BeginChild("BuildPlatformNav", ImVec2 {190.0f, -1.0f}, true);
        ImGui::TextUnformatted("Platform");
        const char* platforms[] = {"Windows", "macOS", "Linux", "Android", "WebGPU"};
        for (const char* platform : platforms)
        {
            const bool selected = settings.targetPlatform == platform;
            const auto label    = platformLabel(platform);
            if (ui::settingsNavItem(label.c_str(), selected))
                settings.targetPlatform = platform;
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("BuildSettingsContent", ImVec2 {470.0f, -1.0f}, true);
        ui::drawSettingsSectionHeader(settings.targetPlatform.c_str());
        const char* architectures[] = {"x64", "arm64"};
        int         archIndex       = settings.architecture == "arm64" ? 1 : 0;
        ui::beginSettingsRow("Architecture");
        if (ImGui::Combo("##Architecture", &archIndex, architectures, IM_ARRAYSIZE(architectures)))
            settings.architecture = architectures[archIndex];
        ui::endSettingsRow();
        const char* configs[]   = {"Development", "Release"};
        int         configIndex = settings.configuration == "Release" ? 1 : 0;
        ui::beginSettingsRow("Build Configuration");
        if (ImGui::Combo("##BuildConfiguration", &configIndex, configs, IM_ARRAYSIZE(configs)))
            settings.configuration = configs[configIndex];
        ui::endSettingsRow();
        const bool sameHost = settings.targetPlatform == currentHostPlatform();
        ui::beginSettingsRow("Export Template");
        m_ExportTemplateDialog.drawBrowseOnly("", m_ExportTemplateBuffer.data(), m_ExportTemplateBuffer.size());
        ui::endSettingsRow();
        ImGui::Indent(150.0f);
        ui::drawInfoRegion(sameHost ? "Host platform export can use the current editor executable when empty." :
                                      "Cross-platform export requires a target runtime executable.");
        ImGui::Unindent(150.0f);
        ui::beginSettingsRow("Output Directory");
        m_BuildSettingsOutputDialog.setDefaultPath(ctx.state.currentProject);
        m_BuildSettingsOutputDialog.drawBrowseOnly(
            "", m_BuildOutputFolderBuffer.data(), m_BuildOutputFolderBuffer.size());
        ui::endSettingsRow();
        ui::beginSettingsRow("Project Name");
        ImGui::InputText("##ProjectName", m_BuildProjectNameBuffer.data(), m_BuildProjectNameBuffer.size());
        ui::endSettingsRow();
        ImGui::Checkbox("Include Debug Symbols", &settings.includeDebugSymbols);
        ImGui::Checkbox("Compress Content", &settings.compressContent);
        ImGui::Checkbox("Use VPK Files", &settings.usePakFiles);
        ui::beginSettingsRow("Additional Arguments");
        ImGui::InputText("##AdditionalArguments", m_BuildExtraArgsBuffer.data(), m_BuildExtraArgsBuffer.size());
        ui::endSettingsRow();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("BuildActions", ImVec2 {0.0f, -1.0f}, true);
        ui::drawSettingsSectionHeader("Build");
        const std::string templatePath = bufferString(m_ExportTemplateBuffer);
        std::string       exportBlockReason;
        if (m_BuildRunActive)
            exportBlockReason = "Export is already running.";
        else if (m_BuildOutputFolderBuffer[0] == '\0')
            exportBlockReason = "Output directory is required.";
        else if (ctx.state.currentProject.empty())
            exportBlockReason = "No project is loaded.";
        else if (ctx.state.currentDefaultScene.empty())
            exportBlockReason = "No default scene is selected.";
        else if (ctx.state.editorPlaying)
            exportBlockReason = "Stop Play Mode before export.";
        else if (!sameHost && templatePath.empty())
            exportBlockReason = "Missing export template for " + settings.targetPlatform + ".";
        else if (!templatePath.empty())
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(std::filesystem::path {templatePath}, ec))
                exportBlockReason = "Export template does not exist.";
        }

        if (!exportBlockReason.empty())
            ImGui::TextColored(ImVec4 {1.0f, 0.32f, 0.28f, 1.0f}, "%s", exportBlockReason.c_str());

        const bool canBuild           = exportBlockReason.empty();
        auto       applyBuildSettings = [&]() {
            settings.outputDirectory                = bufferString(m_BuildOutputFolderBuffer);
            settings.projectName                    = bufferString(m_BuildProjectNameBuffer);
            settings.exportTemplatePath             = bufferString(m_ExportTemplateBuffer);
            settings.additionalCommandLineArguments = bufferString(m_BuildExtraArgsBuffer);
            if (settings.projectName.empty())
                settings.projectName = ctx.state.currentProjectName;
        };
        auto prepareBuild = [&]() -> bool {
            if (ctx.state.currentProject.empty())
            {
                ctx.state.statusMessage = "Export failed: no project is loaded.";
                return false;
            }
            if (ctx.state.currentDefaultScene.empty())
            {
                ctx.state.statusMessage = "Export failed: no default scene is selected.";
                return false;
            }
            if (ctx.state.editorPlaying)
            {
                ctx.state.statusMessage = "Stop Play Mode before export.";
                return false;
            }

            const bool sceneWasDirty = ctx.state.sceneDirty;
            saveCurrentScene(ctx);
            if (sceneWasDirty && ctx.state.sceneDirty)
            {
                ctx.state.statusMessage = "Export stopped: save the current scene first.";
                return false;
            }
            applyBuildSettings();
            return true;
        };
        if (!canBuild)
            ImGui::BeginDisabled();
        if (ImGui::Button("Export", ImVec2 {-1.0f, 0.0f}))
        {
            if (prepareBuild())
            {
                beginBuildAndRun(ctx, std::filesystem::path {settings.outputDirectory}, false);
                ImGui::CloseCurrentPopup();
            }
        }
        if (ImGui::Button(ICON_MDI_PLAY "  Export & Run", ImVec2 {-1.0f, 0.0f}))
        {
            if (prepareBuild())
            {
                beginBuildAndRun(ctx, std::filesystem::path {settings.outputDirectory}, true);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!canBuild)
            ImGui::EndDisabled();
        ImGui::Spacing();
        ui::drawSettingsSectionHeader("Export Status");
        ImGui::Text("Status: %s", m_BuildRunActive ? "Running" : settings.lastBuildStatus.c_str());
        ImGui::Text("Last Build: %s", settings.lastBuildTime.c_str());
        if (!settings.buildLog.empty())
            ImGui::TextWrapped("%s", settings.buildLog.c_str());
        ImGui::EndChild();
        ImGui::EndPopup();
    }

} // namespace vultra_app
