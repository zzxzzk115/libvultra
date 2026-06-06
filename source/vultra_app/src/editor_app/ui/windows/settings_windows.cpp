#include "editor_app/editor_app.hpp"

#include "editor_app/editor_settings_persistence.hpp"
#include "editor_app/project_asset_utils.hpp"
#include "editor_app/ui/settings_widgets.hpp"
#include "vproject.hpp"

#include <vultra/core/i18n/i18n.hpp>
#include <vultra/core/services/i18n_service.hpp>
#include <vultra/function/imgui/imgui_dpi.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/plugin/plugin_manifest.hpp>
#include <vultra/function/services/plugin_service.hpp>
#include <vultra/function/services/render_service.hpp>

#include <algorithm>

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
        static int                      selectedPage = 0;
        static std::vector<std::string> s_EnabledPlugins;
        if (ctx.state.projectSettingsOpen)
        {
            setBuffer(m_ProjectNameBuffer, ctx.state.currentProjectName);
            setBuffer(m_ProjectAssetRootBuffer, ctx.state.currentAssetRoot);
            setBuffer(m_ProjectDefaultSceneBuffer, ctx.state.currentDefaultScene);
            setBuffer(m_ProjectEditingRenderGraphBuffer, ctx.state.currentEditingRenderGraph);
            s_EnabledPlugins.clear();
            if (auto project = loadVProject(ctx.state.currentProject); project.has_value())
                s_EnabledPlugins = project->enabledPlugins;
            ImGui::OpenPopup(vultra::trId("projectSettings.title", "Project Settings"));
            ctx.state.projectSettingsOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        ImGui::SetNextWindowSize(ImVec2 {vultra::ui::dp(760.0f), vultra::ui::dp(520.0f)}, ImGuiCond_Appearing);
        bool popupOpen = true;
        if (!ImGui::BeginPopupModal(vultra::trId("projectSettings.title", "Project Settings"), &popupOpen))
            return;
        if (!popupOpen)
        {
            ImGui::EndPopup();
            return;
        }

        ImGui::SetNextItemWidth(-1.0f);
        static char search[128] {};
        ImGui::InputTextWithHint("##ProjectSettingsSearch",
                                 (std::string {ICON_MDI_MAGNIFY " "} + vultra::tr("projectSettings.searchHint")).c_str(),
                                 search,
                                 sizeof(search));
        ImGui::Separator();

        ImGui::BeginChild("ProjectSettingsNav", ImVec2 {vultra::ui::dp(180.0f), vultra::ui::dp(-42.0f)}, true);
        ImGui::TextUnformatted(vultra::tr("projectSettings.nav.project"));
        if (ui::settingsNavItem(vultra::trId("projectSettings.nav.projectInfo", "Project Info"), selectedPage == 0))
            selectedPage = 0;
        if (ui::settingsNavItem(vultra::trId("projectSettings.nav.renderSettings", "Render Settings"),
                                selectedPage == 1))
            selectedPage = 1;
        if (ui::settingsNavItem(vultra::trId("projectSettings.nav.buildScenes", "Build Scenes"), selectedPage == 2))
            selectedPage = 2;
        if (ui::settingsNavItem(vultra::trId("projectSettings.nav.packaging", "Packaging"), selectedPage == 3))
            selectedPage = 3;
        if (ui::settingsNavItem(vultra::trId("projectSettings.nav.plugins", "Plugins"), selectedPage == 4))
            selectedPage = 4;
        ImGui::Spacing();
        ImGui::TextUnformatted(vultra::tr("projectSettings.nav.engine"));
        ImGui::BeginDisabled();
        ui::settingsNavItem(vultra::trId("projectSettings.nav.general", "General"), false);
        ui::settingsNavItem(vultra::trId("projectSettings.nav.rendering", "Rendering"), false);
        ui::settingsNavItem(vultra::trId("projectSettings.nav.materials", "Materials"), false);
        ui::settingsNavItem(vultra::trId("projectSettings.nav.scripting", "Scripting"), false);
        ImGui::EndDisabled();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ProjectSettingsContent", ImVec2 {0.0f, vultra::ui::dp(-42.0f)}, true);
        bool projectSettingsChanged = false;
        if (selectedPage == 0)
        {
            ui::drawSettingsSectionHeader(vultra::tr("projectSettings.info.header"));
            ui::beginSettingsRow(vultra::tr("projectSettings.info.projectName"));
            projectSettingsChanged |=
                ImGui::InputText("##ProjectName", m_ProjectNameBuffer.data(), m_ProjectNameBuffer.size());
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("projectSettings.info.assetRoot"));
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
            ui::beginSettingsRow(vultra::tr("projectSettings.info.defaultScene"));
            if (ImGui::BeginCombo("##DefaultScene",
                                  bufferString(m_ProjectDefaultSceneBuffer).empty() ?
                                      vultra::tr("projectSettings.noneParen") :
                                      m_ProjectDefaultSceneBuffer.data()))
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
            ui::drawInfoRegion(ctx.state.currentProject.empty() ? vultra::tr("projectSettings.info.noProjectLoaded") :
                                                                  ctx.state.currentProject.generic_string().c_str());
        }
        else if (selectedPage == 1)
        {
            ui::drawSettingsSectionHeader(vultra::tr("projectSettings.render.header"));
            auto renderGraphUris =
                collectProjectRenderGraphUris(ctx.state.currentProject, bufferString(m_ProjectAssetRootBuffer));
            if (!bufferString(m_ProjectEditingRenderGraphBuffer).empty() &&
                std::find(renderGraphUris.begin(),
                          renderGraphUris.end(),
                          bufferString(m_ProjectEditingRenderGraphBuffer)) == renderGraphUris.end())
                renderGraphUris.push_back(bufferString(m_ProjectEditingRenderGraphBuffer));
            std::sort(renderGraphUris.begin(), renderGraphUris.end());
            ui::beginSettingsRow(vultra::tr("projectSettings.render.editingRenderGraph"));
            if (ImGui::BeginCombo("##EditingRenderGraph",
                                  bufferString(m_ProjectEditingRenderGraphBuffer).empty() ?
                                      vultra::tr("projectSettings.noneParen") :
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
                ui::drawSettingsSectionHeader(vultra::tr("projectSettings.render.editorSelection"));
                ImGui::Checkbox(vultra::tr("projectSettings.render.selectionOutline"), &outline.enabled);
                ImGui::ColorEdit3(vultra::tr("projectSettings.render.outlineColor"), &outline.color.x);
                ImGui::SliderFloat(
                    vultra::tr("projectSettings.render.outlineThickness"), &outline.thickness, 1.0f, 8.0f, "%.0f px");
                ImGui::SliderFloat(
                    vultra::tr("projectSettings.render.fillOpacity"), &outline.fillOpacity, 0.0f, 0.25f, "%.2f");
                ImGui::SliderFloat(
                    vultra::tr("projectSettings.render.edgeOpacity"), &outline.edgeOpacity, 0.0f, 1.0f, "%.2f");
            }
        }
        else if (selectedPage == 2)
        {
            ui::drawSettingsSectionHeader(vultra::tr("projectSettings.buildScenes.header"));
            auto sceneUris = collectAssetUrisWithExtension(
                ctx.state.currentProject, bufferString(m_ProjectAssetRootBuffer), ".vscn");
            const auto defaultScene = bufferString(m_ProjectDefaultSceneBuffer);
            if (!defaultScene.empty() && std::find(sceneUris.begin(), sceneUris.end(), defaultScene) == sceneUris.end())
                sceneUris.push_back(defaultScene);
            std::sort(sceneUris.begin(), sceneUris.end());

            if (ctx.state.currentBuildScenes.empty() && !defaultScene.empty())
                ctx.state.currentBuildScenes =
                    normalizedBuildScenes(defaultScene, ctx.state.currentBuildScenes);

            if (ImGui::Button((std::string {ICON_MDI_PLUS "  "} + vultra::tr("projectSettings.buildScenes.addDefault"))
                                  .c_str(),
                              ImVec2 {vultra::ui::dp(128.0f), 0.0f}))
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
            if (ImGui::Button(
                    (std::string {ICON_MDI_PLUS "  "} + vultra::tr("projectSettings.buildScenes.addAll")).c_str(),
                    ImVec2 {vultra::ui::dp(112.0f), 0.0f}))
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
                ui::drawInfoRegion(vultra::tr("projectSettings.buildScenes.noScenesInfo"));
            }
            else if (ImGui::BeginTable("BuildScenesTable",
                                       7,
                                       ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, vultra::ui::dp(34.0f));
                ImGui::TableSetupColumn(
                    vultra::tr("common.enabled"), ImGuiTableColumnFlags_WidthFixed, vultra::ui::dp(64.0f));
                ImGui::TableSetupColumn(vultra::tr("common.name"), ImGuiTableColumnFlags_WidthStretch, 0.26f);
                ImGui::TableSetupColumn(
                    vultra::tr("projectSettings.buildScenes.alias"), ImGuiTableColumnFlags_WidthStretch, 0.26f);
                ImGui::TableSetupColumn(
                    vultra::tr("projectSettings.buildScenes.scene"), ImGuiTableColumnFlags_WidthStretch, 0.48f);
                ImGui::TableSetupColumn(
                    vultra::tr("projectSettings.buildScenes.order"), ImGuiTableColumnFlags_WidthFixed, vultra::ui::dp(62.0f));
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, vultra::ui::dp(34.0f));
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
                    if (ImGui::BeginCombo("##BuildSceneUri",
                                          scene.uri.empty() ? vultra::tr("projectSettings.noneParen") :
                                                              scene.uri.c_str()))
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
                    ImGui::SameLine(0.0f, vultra::ui::dp(4.0f));
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
            ui::drawInfoRegion(vultra::tr("projectSettings.buildScenes.exportInfo"));
        }
        else if (selectedPage == 3)
        {
            ui::drawSettingsSectionHeader(vultra::tr("projectSettings.packaging.header"));
            ui::beginSettingsRow(vultra::tr("projectSettings.packaging.packageName"));
            ImGui::TextUnformatted(m_ProjectNameBuffer.data());
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("projectSettings.packaging.packageManifest"));
            ImGui::TextUnformatted(kVPackageManifestPath);
            ui::endSettingsRow();
            ui::drawInfoRegion(vultra::tr("projectSettings.packaging.info"));
        }
        else if (selectedPage == 4)
        {
            ui::drawSettingsSectionHeader(vultra::tr("projectSettings.plugins.header"));
            const auto pluginsDir =
                (ctx.state.currentProject / ctx.state.currentAssetRoot / "plugins").lexically_normal();
            ui::drawInfoRegion(vultra::tr("projectSettings.plugins.info"));

            const auto manifests = vultra::discoverPlugins(pluginsDir);
            if (manifests.empty())
                ImGui::TextDisabled(
                    "%s", vultra::trf("projectSettings.plugins.noneFound", pluginsDir.generic_string()).c_str());

            for (const auto& manifest : manifests)
            {
                ImGui::PushID(manifest.id.c_str());
                const bool wasEnabled =
                    std::find(s_EnabledPlugins.begin(), s_EnabledPlugins.end(), manifest.id) != s_EnabledPlugins.end();
                const bool supported = manifest.supportsCurrentPlatform();

                bool enabled = wasEnabled;
                if (!supported)
                    ImGui::BeginDisabled();
                if (ImGui::Checkbox(manifest.name.empty() ? manifest.id.c_str() : manifest.name.c_str(), &enabled))
                {
                    if (enabled && !wasEnabled)
                    {
                        s_EnabledPlugins.push_back(manifest.id);
                        if (auto* plugins = ctx.services ? ctx.services->tryGet<vultra::IPluginService>() : nullptr)
                            plugins->loadPlugin(manifest);
                    }
                    else if (!enabled && wasEnabled)
                        s_EnabledPlugins.erase(
                            std::remove(s_EnabledPlugins.begin(), s_EnabledPlugins.end(), manifest.id),
                            s_EnabledPlugins.end());
                }
                if (!supported)
                    ImGui::EndDisabled();

                ImGui::SameLine();
                ImGui::TextDisabled("v%s%s%s",
                                    manifest.version.empty() ? "?" : manifest.version.c_str(),
                                    manifest.author.empty() ? "" : "  \xc2\xb7  ",
                                    manifest.author.c_str());

                ImGui::Indent();
                ImGui::TextDisabled("%s", manifest.id.c_str());
                if (!manifest.description.empty())
                    ImGui::TextWrapped("%s", manifest.description.c_str());
                if (!manifest.repository.empty())
                    ImGui::TextDisabled("%s", manifest.repository.c_str());
                std::string capabilities;
                if (!manifest.native.empty())
                    capabilities += "native ";
                if (!manifest.entry.empty())
                    capabilities += "lua";
                if (!capabilities.empty())
                    ImGui::TextDisabled("%s",
                                        vultra::trf("projectSettings.plugins.provides", capabilities).c_str());
                if (!supported)
                    ImGui::TextColored(
                        ImVec4 {1.0f, 0.7f, 0.2f, 1.0f},
                        "%s",
                        vultra::trf("projectSettings.plugins.notSupported", std::string(vultra::currentPluginPlatform()))
                            .c_str());
                ImGui::Unindent();
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        if (projectSettingsChanged)
        {
            applyProjectSettingsFromBuffers(ctx,
                                            m_ProjectNameBuffer,
                                            m_ProjectAssetRootBuffer,
                                            m_ProjectDefaultSceneBuffer,
                                            m_ProjectEditingRenderGraphBuffer);
            ctx.state.statusMessage = vultra::tr("projectSettings.status.changed");
        }

        if (ImGui::Button(vultra::tr("projectSettings.resetToDefaults"), ImVec2 {vultra::ui::dp(132.0f), 0.0f}))
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
            ctx.state.statusMessage = vultra::tr("projectSettings.status.reset");
        }
        ui::alignSettingsButtonGroup(2);
        if (ImGui::Button(vultra::tr("common.save"), ImVec2 {vultra::ui::dp(82.0f), 0.0f}))
        {
            VProject project {
                .projectDir         = ctx.state.currentProject,
                .name               = ctx.state.currentProjectName,
                .assetRoot          = ctx.state.currentAssetRoot,
                .defaultScene       = ctx.state.currentDefaultScene,
                .buildScenes        = normalizedBuildScenes(ctx.state.currentDefaultScene, ctx.state.currentBuildScenes),
                .editingRenderGraph = ctx.state.currentEditingRenderGraph,
                .enabledPlugins     = s_EnabledPlugins,
            };
            std::string error;
            if (saveVProject(project, &error))
                ctx.state.statusMessage = vultra::tr("projectSettings.status.saved");
            else
                ctx.state.statusMessage = vultra::trf("projectSettings.status.saveFailed", error);
        }
        ImGui::SameLine();
        if (ImGui::Button(vultra::tr("common.close"), ImVec2 {vultra::ui::dp(82.0f), 0.0f}))
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
            setBuffer(m_AgentCliPathBuffer, ctx.state.editorSettings.agentCliPath);
            ImGui::OpenPopup(vultra::trId("editorSettings.title", "Editor Settings"));
            ctx.state.editorSettingsOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        ImGui::SetNextWindowSize(ImVec2 {vultra::ui::dp(760.0f), vultra::ui::dp(520.0f)}, ImGuiCond_Appearing);
        bool popupOpen = true;
        if (!ImGui::BeginPopupModal(vultra::trId("editorSettings.title", "Editor Settings"), &popupOpen))
            return;
        if (!popupOpen)
        {
            ImGui::EndPopup();
            return;
        }

        ImGui::SetNextItemWidth(-1.0f);
        static char search[128] {};
        ImGui::InputTextWithHint("##EditorSettingsSearch",
                                 (std::string {ICON_MDI_MAGNIFY " "} + vultra::tr("editorSettings.searchHint")).c_str(),
                                 search,
                                 sizeof(search));
        ImGui::Separator();

        ImGui::BeginChild("EditorSettingsNav", ImVec2 {vultra::ui::dp(180.0f), vultra::ui::dp(-42.0f)}, true);
        ImGui::TextUnformatted(vultra::tr("editorSettings.nav.general"));
        if (ui::settingsNavItem(vultra::trId("editorSettings.nav.appearance", "Appearance"), selectedPage == 0))
            selectedPage = 0;
        if (ui::settingsNavItem(vultra::trId("editorSettings.nav.fonts", "Fonts"), selectedPage == 1))
            selectedPage = 1;
        if (ui::settingsNavItem(vultra::trId("editorSettings.nav.externalEditor", "External Editor"),
                                selectedPage == 2))
            selectedPage = 2;
        if (ui::settingsNavItem(vultra::trId("editorSettings.nav.aiAgent", "AI Agent"), selectedPage == 3))
            selectedPage = 3;
        ImGui::Spacing();
        ImGui::TextUnformatted(vultra::tr("editorSettings.nav.advanced"));
        ImGui::BeginDisabled();
        ui::settingsNavItem(vultra::trId("editorSettings.nav.filesPaths", "Files & Paths"), false);
        ui::settingsNavItem(vultra::trId("editorSettings.nav.console", "Console"), false);
        ui::settingsNavItem(vultra::trId("editorSettings.nav.privacy", "Privacy"), false);
        ImGui::EndDisabled();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("EditorSettingsContent", ImVec2 {0.0f, vultra::ui::dp(-42.0f)}, true);
        auto& settings = ctx.state.editorSettings;
        if (selectedPage == 0)
        {
            ui::drawSettingsSectionHeader(vultra::tr("editorSettings.appearance.header"));

            // Language (UI locale). Switching is live: the catalog swaps and every window re-renders
            // next frame with the new strings (no font/atlas rebuild -- CJK glyphs are already merged).
            if (auto* i18n = ctx.services ? ctx.services->tryGet<vultra::II18nService>() : nullptr)
            {
                ui::beginSettingsRow(vultra::tr("settings.language"));
                const std::string current = i18n->displayName(i18n->currentLanguage());
                if (ImGui::BeginCombo("##UiLanguage", current.c_str()))
                {
                    for (const auto& locale : i18n->availableLanguages())
                    {
                        const bool selected = locale == i18n->currentLanguage();
                        if (ImGui::Selectable(i18n->displayName(locale).c_str(), selected) && !selected)
                        {
                            settings.language = locale;
                            i18n->setLanguage(locale);
                            ctx.state.statusMessage =
                            vultra::trf("editorSettings.status.languageChanged", i18n->displayName(locale));
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ui::endSettingsRow();
            }

            const char* themes[]      = {"Dark", "Graphite", "Light", "Custom"};
            const char* themeLabels[] = {vultra::tr("editorSettings.theme.dark"),
                                         vultra::tr("editorSettings.theme.graphite"),
                                         vultra::tr("editorSettings.theme.light"),
                                         vultra::tr("editorSettings.theme.custom")};
            int         themeIndex    = 0;
            for (int i = 0; i < IM_ARRAYSIZE(themes); ++i)
            {
                if (settings.theme == themes[i])
                {
                    themeIndex = i;
                    break;
                }
            }
            ui::beginSettingsRow(vultra::tr("editorSettings.appearance.colorTheme"));
            if (ImGui::Combo("##ColorTheme", &themeIndex, themeLabels, IM_ARRAYSIZE(themeLabels)))
            {
                settings.theme          = themes[themeIndex];
                ctx.state.statusMessage = vultra::trf("editorSettings.status.themeChanged", settings.theme);
            }
            ui::endSettingsRow();
            if (settings.theme == "Custom")
            {
                ui::beginSettingsRow(vultra::tr("editorSettings.appearance.background"));
                ImGui::ColorEdit3("##CustomThemeBackground", &settings.customThemeBackground.x);
                ui::endSettingsRow();
                ui::beginSettingsRow(vultra::tr("editorSettings.appearance.panel"));
                ImGui::ColorEdit3("##CustomThemePanel", &settings.customThemePanel.x);
                ui::endSettingsRow();
                ui::beginSettingsRow(vultra::tr("editorSettings.appearance.text"));
                ImGui::ColorEdit3("##CustomThemeText", &settings.customThemeText.x);
                ui::endSettingsRow();
                ui::beginSettingsRow(vultra::tr("editorSettings.appearance.accent"));
                ImGui::ColorEdit3("##CustomThemeAccent", &settings.customThemeAccent.x);
                ui::endSettingsRow();
            }
            ui::beginSettingsRow(vultra::tr("editorSettings.appearance.applicationScale"));
            ImGui::SliderFloat("##ApplicationScale", &settings.applicationScale, 0.75f, 2.0f, "%.2fx");
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("editorSettings.appearance.textScale"));
            ImGui::SliderFloat("##TextScale", &settings.textScale, 0.75f, 2.0f, "%.2fx");
            ui::endSettingsRow();
            ImGui::Checkbox(vultra::tr("editorSettings.appearance.showSplash"), &settings.showSplashOnStartup);
            ImGui::Checkbox(vultra::tr("editorSettings.appearance.enableAnimations"), &settings.enableAnimations);
        }
        else if (selectedPage == 1)
        {
            ui::drawSettingsSectionHeader(vultra::tr("editorSettings.fonts.header"));
            ui::beginSettingsRow(vultra::tr("editorSettings.fonts.interfaceFont"));
            ImGui::TextUnformatted(settings.interfaceFont.c_str());
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("editorSettings.fonts.interfaceFontSize"));
            ImGui::SliderInt("##InterfaceFontSize", &settings.interfaceFontSize, 10, 24, "%d px");
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("editorSettings.fonts.monospaceFont"));
            ImGui::TextUnformatted(settings.monospaceFont.c_str());
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("editorSettings.fonts.monospaceFontSize"));
            ImGui::SliderInt("##MonospaceFontSize", &settings.monospaceFontSize, 10, 24, "%d px");
            ui::endSettingsRow();
            ImGui::Checkbox(vultra::tr("editorSettings.fonts.useSystemFonts"), &settings.useSystemFonts);
        }
        else if (selectedPage == 2)
        {
            ui::drawSettingsSectionHeader(vultra::tr("editorSettings.externalEditor.header"));
            ui::beginSettingsRow(vultra::tr("editorSettings.externalEditor.executable"));
            if (m_ExternalEditorDialog.drawBrowseOnly("", m_ExternalEditorBuffer.data(), m_ExternalEditorBuffer.size()))
                ctx.state.editorSettings.externalEditor = bufferString(m_ExternalEditorBuffer);
            ui::endSettingsRow();
            ui::drawInfoRegion(vultra::tr("editorSettings.externalEditor.info"));
        }
        else
        {
            ui::drawSettingsSectionHeader(vultra::tr("editorSettings.aiAgent.header"));
            ImGui::Checkbox(vultra::tr("editorSettings.aiAgent.enablePanel"), &settings.enableAgent);
            ImGui::Checkbox(vultra::tr("editorSettings.aiAgent.autoStartMcp"), &settings.autoStartMcp);
            ui::beginSettingsRow(vultra::tr("editorSettings.aiAgent.mcpServerName"));
            if (ImGui::InputText(
                    "##McpServerName", m_AgentMcpServerNameBuffer.data(), m_AgentMcpServerNameBuffer.size()))
                settings.mcpServerName = bufferString(m_AgentMcpServerNameBuffer);
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("editorSettings.aiAgent.mcpHost"));
            if (ImGui::InputText("##McpHost", m_AgentMcpHostBuffer.data(), m_AgentMcpHostBuffer.size()))
                settings.mcpHost = bufferString(m_AgentMcpHostBuffer);
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("editorSettings.aiAgent.mcpPort"));
            ImGui::InputInt("##McpPort", &settings.mcpPort);
            settings.mcpPort = std::clamp(settings.mcpPort, 1, 65535);
            ui::endSettingsRow();
            ImGui::Spacing();
            ui::drawSettingsSectionHeader(vultra::tr("editorSettings.aiAgent.clientHeader"));
            ui::beginSettingsRow(vultra::tr("editorSettings.aiAgent.endpoint"));
            if (ImGui::InputText("##AgentEndpoint", m_AgentEndpointBuffer.data(), m_AgentEndpointBuffer.size()))
                settings.agentEndpoint = bufferString(m_AgentEndpointBuffer);
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("editorSettings.aiAgent.model"));
            if (ImGui::InputText("##AgentModel", m_AgentModelBuffer.data(), m_AgentModelBuffer.size()))
                settings.agentModel = bufferString(m_AgentModelBuffer);
            ui::endSettingsRow();
            ui::beginSettingsRow(vultra::tr("editorSettings.aiAgent.cliPath"));
            if (ImGui::InputText("##AgentCliPath", m_AgentCliPathBuffer.data(), m_AgentCliPathBuffer.size()))
                settings.agentCliPath = bufferString(m_AgentCliPathBuffer);
            ui::endSettingsRow();
            ui::drawInfoRegion(vultra::tr("editorSettings.aiAgent.cliInfo"));
            ImGui::Spacing();
            ui::drawSettingsSectionHeader(vultra::tr("editorSettings.aiAgent.guardrailsHeader"));
            ImGui::Checkbox(vultra::tr("editorSettings.aiAgent.allowProjectOps"), &settings.allowAgentProjectOperations);
            ImGui::Checkbox(vultra::tr("editorSettings.aiAgent.allowEngineOps"), &settings.allowAgentEngineOperations);
            ImGui::Checkbox(vultra::tr("editorSettings.aiAgent.requireConfirmation"), &settings.requireAgentConfirmation);
            ui::drawInfoRegion(vultra::tr("editorSettings.aiAgent.guardrailsInfo"));
        }
        ImGui::EndChild();

        if (ImGui::Button(vultra::tr("editorSettings.resetToDefaults"), ImVec2 {vultra::ui::dp(132.0f), 0.0f}))
        {
            ctx.state.editorSettings = AppState::EditorSettings {};
            setBuffer(m_ExternalEditorBuffer, {});
            setBuffer(m_AgentMcpServerNameBuffer, ctx.state.editorSettings.mcpServerName);
            setBuffer(m_AgentMcpHostBuffer, ctx.state.editorSettings.mcpHost);
            setBuffer(m_AgentEndpointBuffer, {});
            setBuffer(m_AgentModelBuffer, {});
            setBuffer(m_AgentCliPathBuffer, {});
            ctx.state.statusMessage = vultra::tr("editorSettings.status.reset");
        }
        ui::alignSettingsButtonGroup(2);
        if (ImGui::Button(vultra::tr("common.save"), ImVec2 {vultra::ui::dp(82.0f), 0.0f}))
        {
            ctx.state.editorSettings.externalEditor = bufferString(m_ExternalEditorBuffer);
            ctx.state.editorSettings.mcpServerName  = bufferString(m_AgentMcpServerNameBuffer);
            ctx.state.editorSettings.mcpHost        = bufferString(m_AgentMcpHostBuffer);
            ctx.state.editorSettings.agentEndpoint  = bufferString(m_AgentEndpointBuffer);
            ctx.state.editorSettings.agentModel     = bufferString(m_AgentModelBuffer);
            ctx.state.editorSettings.agentCliPath   = bufferString(m_AgentCliPathBuffer);
            std::string error;
            if (saveEditorSettings(ctx.state.editorSettingsFile, ctx.state.editorSettings, &error))
                ctx.state.statusMessage = vultra::tr("editorSettings.status.saved");
            else
                ctx.state.statusMessage = vultra::trf("editorSettings.status.saveFailed", error);
        }
        ImGui::SameLine();
        if (ImGui::Button(vultra::tr("common.close"), ImVec2 {vultra::ui::dp(82.0f), 0.0f}))
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
            ImGui::OpenPopup(vultra::trId("exportSettings.title", "Export Settings"));
            ctx.state.buildSettingsOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        ImGui::SetNextWindowSize(ImVec2 {vultra::ui::dp(920.0f), vultra::ui::dp(430.0f)}, ImGuiCond_Appearing);
        bool popupOpen = true;
        if (!ImGui::BeginPopupModal(vultra::trId("exportSettings.title", "Export Settings"), &popupOpen))
            return;
        if (!popupOpen)
        {
            ImGui::EndPopup();
            return;
        }

        auto& settings = ctx.state.buildSettings;
        ImGui::BeginChild("BuildPlatformNav", ImVec2 {vultra::ui::dp(190.0f), -1.0f}, true);
        ImGui::TextUnformatted(vultra::tr("exportSettings.platform"));
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
        ImGui::BeginChild("BuildSettingsContent", ImVec2 {vultra::ui::dp(470.0f), -1.0f}, true);
        ui::drawSettingsSectionHeader(settings.targetPlatform.c_str());
        const char* architectures[] = {"x64", "arm64"};
        int         archIndex       = settings.architecture == "arm64" ? 1 : 0;
        ui::beginSettingsRow(vultra::tr("exportSettings.architecture"));
        if (ImGui::Combo("##Architecture", &archIndex, architectures, IM_ARRAYSIZE(architectures)))
            settings.architecture = architectures[archIndex];
        ui::endSettingsRow();
        const char* configs[]      = {"Development", "Release"};
        const char* configLabels[] = {vultra::tr("exportSettings.config.development"),
                                       vultra::tr("exportSettings.config.release")};
        int         configIndex    = settings.configuration == "Release" ? 1 : 0;
        ui::beginSettingsRow(vultra::tr("exportSettings.buildConfiguration"));
        if (ImGui::Combo("##BuildConfiguration", &configIndex, configLabels, IM_ARRAYSIZE(configLabels)))
            settings.configuration = configs[configIndex];
        ui::endSettingsRow();
        const bool sameHost = settings.targetPlatform == currentHostPlatform();
        ui::beginSettingsRow(vultra::tr("exportSettings.exportTemplate"));
        m_ExportTemplateDialog.drawBrowseOnly("", m_ExportTemplateBuffer.data(), m_ExportTemplateBuffer.size());
        ui::endSettingsRow();
        ImGui::Indent(vultra::ui::dp(150.0f));
        ui::drawInfoRegion(sameHost ? vultra::tr("exportSettings.hostExportInfo") :
                                      vultra::tr("exportSettings.crossExportInfo"));
        ImGui::Unindent(vultra::ui::dp(150.0f));
        ui::beginSettingsRow(vultra::tr("exportSettings.outputDirectory"));
        m_BuildSettingsOutputDialog.setDefaultPath(ctx.state.currentProject);
        m_BuildSettingsOutputDialog.drawBrowseOnly(
            "", m_BuildOutputFolderBuffer.data(), m_BuildOutputFolderBuffer.size());
        ui::endSettingsRow();
        ui::beginSettingsRow(vultra::tr("exportSettings.projectName"));
        ImGui::InputText("##ProjectName", m_BuildProjectNameBuffer.data(), m_BuildProjectNameBuffer.size());
        ui::endSettingsRow();
        ImGui::Checkbox(vultra::tr("exportSettings.includeDebugSymbols"), &settings.includeDebugSymbols);
        ImGui::Checkbox(vultra::tr("exportSettings.compressContent"), &settings.compressContent);
        ImGui::Checkbox(vultra::tr("exportSettings.useVpkFiles"), &settings.usePakFiles);
        ui::beginSettingsRow(vultra::tr("exportSettings.additionalArguments"));
        ImGui::InputText("##AdditionalArguments", m_BuildExtraArgsBuffer.data(), m_BuildExtraArgsBuffer.size());
        ui::endSettingsRow();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("BuildActions", ImVec2 {0.0f, -1.0f}, true);
        ui::drawSettingsSectionHeader(vultra::tr("exportSettings.build.header"));
        const std::string templatePath = bufferString(m_ExportTemplateBuffer);
        std::string       exportBlockReason;
        if (m_BuildRunActive)
            exportBlockReason = vultra::tr("exportSettings.block.alreadyRunning");
        else if (m_BuildOutputFolderBuffer[0] == '\0')
            exportBlockReason = vultra::tr("exportSettings.block.outputRequired");
        else if (ctx.state.currentProject.empty())
            exportBlockReason = vultra::tr("exportSettings.block.noProject");
        else if (ctx.state.currentDefaultScene.empty())
            exportBlockReason = vultra::tr("exportSettings.block.noDefaultScene");
        else if (ctx.state.editorPlaying)
            exportBlockReason = vultra::tr("exportSettings.block.stopPlayMode");
        else if (!sameHost && templatePath.empty())
            exportBlockReason = vultra::trf("exportSettings.block.missingTemplate", settings.targetPlatform);
        else if (!templatePath.empty())
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(std::filesystem::path {templatePath}, ec))
                exportBlockReason = vultra::tr("exportSettings.block.templateMissing");
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
                ctx.state.statusMessage = vultra::tr("exportSettings.status.failedNoProject");
                return false;
            }
            if (ctx.state.currentDefaultScene.empty())
            {
                ctx.state.statusMessage = vultra::tr("exportSettings.status.failedNoScene");
                return false;
            }
            if (ctx.state.editorPlaying)
            {
                ctx.state.statusMessage = vultra::tr("exportSettings.block.stopPlayMode");
                return false;
            }

            const bool sceneWasDirty = ctx.state.sceneDirty;
            saveCurrentScene(ctx);
            if (sceneWasDirty && ctx.state.sceneDirty)
            {
                ctx.state.statusMessage = vultra::tr("exportSettings.status.saveSceneFirst");
                return false;
            }
            applyBuildSettings();
            return true;
        };
        if (!canBuild)
            ImGui::BeginDisabled();
        if (ImGui::Button(vultra::tr("exportSettings.export"), ImVec2 {-1.0f, 0.0f}))
        {
            if (prepareBuild())
            {
                beginBuildAndRun(ctx, std::filesystem::path {settings.outputDirectory}, false);
                ImGui::CloseCurrentPopup();
            }
        }
        if (ImGui::Button((std::string {ICON_MDI_PLAY "  "} + vultra::tr("exportSettings.exportAndRun")).c_str(),
                          ImVec2 {-1.0f, 0.0f}))
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
        ui::drawSettingsSectionHeader(vultra::tr("exportSettings.statusHeader"));
        ImGui::TextUnformatted(
            vultra::trf("exportSettings.statusLine",
                        m_BuildRunActive ? vultra::tr("exportSettings.running") : settings.lastBuildStatus.c_str())
                .c_str());
        ImGui::TextUnformatted(vultra::trf("exportSettings.lastBuild", settings.lastBuildTime).c_str());
        if (!settings.buildLog.empty())
            ImGui::TextWrapped("%s", settings.buildLog.c_str());
        ImGui::EndChild();
        ImGui::EndPopup();
    }

} // namespace vultra_app
