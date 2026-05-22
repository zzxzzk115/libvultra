#include "editor_app/editor_app.hpp"

#include "editor_app/ui/editor_top_bar.hpp"
#include "editor_app/ui/windows/code_editor_window.hpp"
#include "editor_app/ui/windows/content_browser_window.hpp"
#include "editor_app/ui/windows/console_window.hpp"
#include "editor_app/ui/windows/game_view_window.hpp"
#include "editor_app/ui/windows/inspector_window.hpp"
#include "editor_app/ui/windows/render_graph_window.hpp"
#include "editor_app/ui/windows/scene_hierarchy_window.hpp"
#include "editor_app/ui/windows/scene_view_window.hpp"
#include "editor_app/selection.hpp"
#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/function/asset/asset_system.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/script_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/name_component.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <entt/entity/entity.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace vultra_app
{
    namespace
    {
        ImGuiID dockSpaceId()
        {
            return ImHashStr("VultraDockSpace");
        }
    } // namespace

    void EditorApp::configureProject(vultra::Engine& engine, const LaunchOptions& options)
    {
        if (options.projectPath.empty())
            return;

        configureProject(engine, options.projectPath);
    }

    void EditorApp::configureProject(vultra::Engine& engine, const std::filesystem::path& projectPath)
    {
        if (auto project = loadVProject(projectPath); project.has_value())
        {
            engine.ctx().config.asset.loadFromVPK = false;
            engine.ctx().config.asset.assetRoot =
                (project->projectDir / project->assetRoot).lexically_normal().generic_string();
            engine.ctx().config.render.renderPipelineAsset       = project->renderPipeline;
            engine.ctx().config.render.renderPipelineRendererKey = "project";
            return;
        }

        engine.ctx().config.asset.loadFromVPK = false;
        engine.ctx().config.asset.assetRoot = (projectPath / "resources").lexically_normal().generic_string();
    }

    void EditorApp::logStartup(const LaunchOptions& options)
    {
        VULTRA_CLIENT_INFO("[VultraEditor] Editor active. project='{}'", options.projectPath);
    }

    void EditorApp::draw(EditorContext& ctx)
    {
        if (updateProjectLoading(ctx))
        {
            drawLoadingOverlay();
            return;
        }

        ensureInitialized();
        ctx.state.gameViewVisibleLastFrame = ctx.state.gameViewVisible;
        ctx.state.gameViewVisible          = false;
        drawEditorTopBar(ctx,
                         m_WindowManager.windows(),
                         EditorTopBarActions {
                             .newBlankScene = [](EditorContext& topBarCtx)
                             {
                                 auto* worldService =
                                     topBarCtx.services ? topBarCtx.services->tryGet<vultra::IWorldService>() : nullptr;
                                 if (!worldService)
                                 {
                                     topBarCtx.state.statusMessage = "New scene failed: world service unavailable.";
                                     return;
                                 }

                                 auto& world = worldService->world();
                                 world.clear();
                                 const auto root = world.createEntity();
                                 auto& reg = world.registry();
                                 reg.get_or_emplace<vultra::NameComponent>(root).name = "SceneRoot";
                                 Selection::select(SelectionCategory::Entity, reg.get<vultra::IDComponent>(root).uuid);
                                 topBarCtx.state.sceneDirty = true;
                                 topBarCtx.state.statusMessage = "Created an empty scene workspace.";
                             },
                             .saveScene = [this](EditorContext& topBarCtx) { saveCurrentScene(topBarCtx); },
                             .backToLauncher = [this](EditorContext& topBarCtx)
                             {
                                 topBarCtx.state.currentProject.clear();
                                 topBarCtx.state.currentProjectName.clear();
                                 topBarCtx.state.selectedSourceAsset.clear();
                                 topBarCtx.state.codeEditorPath.clear();
                                 topBarCtx.state.currentAssetRoot    = "resources";
                                 topBarCtx.state.currentDefaultScene = "res://scenes/test.vscn";
                                 topBarCtx.state.editorPlaying       = false;
                                 topBarCtx.state.editorPaused        = false;
                                 topBarCtx.state.editorStepRequested = false;
                                 topBarCtx.state.codeEditorOpenRequested = false;
                                 topBarCtx.state.sceneDirty          = false;
                                 topBarCtx.state.mode                = AppMode::Launcher;
                                 topBarCtx.state.statusMessage       = "Returned to Project Launcher.";
                                 m_SyncedProject.clear();
                                 m_Loading = {};
                                 m_PlayModeSnapshot.reset();
                                 m_PlaybackWasPlaying = false;
                                 shutdown(topBarCtx);
                             },
                             .resetLayout = [this](EditorContext&) { resetDefaultDockLayout(); },
                             .showAbout   = [this](EditorContext&) { m_ShowAboutPopup = true; },
                         });
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            saveCurrentScene(ctx);
        beginDockSpace();
        buildDefaultDockLayout();
        m_WindowManager.draw(ctx);
        endDockSpace();
        syncPlaybackState(ctx);

        if (m_ShowAboutPopup)
        {
            ImGui::OpenPopup("About Vultra Editor");
            m_ShowAboutPopup = false;
        }

        if (ImGui::BeginPopupModal("About Vultra Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Vultra Editor");
            ImGui::Separator();
            ImGui::Text("Version: %s", "0.1.0");
            ImGui::TextUnformatted("Contributors: Lazy_V (Kexuan Zhang)");
            ImGui::TextUnformatted("License: MIT");
            ImGui::TextLinkOpenURL(ICON_MDI_GITHUB " GitHub", "https://github.com/zzxzzk115/Vultra");
            ImGui::Spacing();
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void EditorApp::syncPlaybackState(EditorContext& ctx)
    {
        if (!ctx.services)
            return;

        auto* scriptService = ctx.services->tryGet<vultra::IScriptService>();

        if (ctx.state.editorPlaying && !m_PlaybackWasPlaying)
            capturePlayModeSnapshot(ctx);

        if (!ctx.state.editorPlaying && m_PlaybackWasPlaying)
        {
            if (scriptService)
                scriptService->setPlaybackState(false, false);
            restorePlayModeSnapshot(ctx);
        }

        if (scriptService)
        {
            scriptService->setPlaybackState(ctx.state.editorPlaying, ctx.state.editorPaused);
            if (ctx.state.editorStepRequested)
            {
                scriptService->requestSingleStep();
                ctx.state.editorStepRequested = false;
            }
        }

        m_PlaybackWasPlaying = ctx.state.editorPlaying;
    }

    void EditorApp::saveCurrentScene(EditorContext& ctx)
    {
        if (!ctx.services)
            return;
        if (ctx.state.editorPlaying)
        {
            ctx.state.statusMessage = "Stop Play Mode before saving the scene.";
            return;
        }
        if (ctx.state.currentDefaultScene.empty())
        {
            ctx.state.statusMessage = "No scene path is selected for saving.";
            return;
        }

        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!sceneService || !worldService)
        {
            ctx.state.statusMessage = "Scene save failed: scene/world service unavailable.";
            return;
        }

        if (sceneService->saveWorldAsSceneSync(ctx.state.currentDefaultScene, worldService->world(), entt::null))
        {
            ctx.state.sceneDirty    = false;
            ctx.state.statusMessage = "Saved scene: " + ctx.state.currentDefaultScene;
        }
        else
        {
            ctx.state.statusMessage = "Scene save failed: " + ctx.state.currentDefaultScene;
        }
    }

    void EditorApp::capturePlayModeSnapshot(EditorContext& ctx)
    {
        if (!ctx.services)
            return;

        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!sceneService || !worldService)
            return;

        auto snapshot = sceneService->captureWorldAsScene(worldService->world(), entt::null);
        if (!snapshot.root)
        {
            ctx.state.statusMessage = "Play mode snapshot failed: scene has no serializable root.";
            ctx.state.editorPlaying = false;
            ctx.state.editorPaused  = false;
            return;
        }

        m_PlayModeSnapshot     = std::move(snapshot);
        ctx.state.statusMessage = "Entered Play Mode.";
    }

    void EditorApp::restorePlayModeSnapshot(EditorContext& ctx)
    {
        if (!ctx.services || !m_PlayModeSnapshot)
            return;

        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!sceneService || !worldService)
            return;

        sceneService->instantiateSceneDocument(worldService->world(), *m_PlayModeSnapshot, entt::null, true);
        m_PlayModeSnapshot.reset();
        Selection::clear(SelectionCategory::Entity);
        ctx.state.statusMessage = "Exited Play Mode. Scene state restored.";
        ctx.state.editorStepRequested = false;
    }

    void EditorApp::ensureInitialized()
    {
        if (m_Initialized)
            return;

        m_WindowManager.addWindow<SceneHierarchyWindow>();
        m_WindowManager.addWindow<SceneViewWindow>();
        m_WindowManager.addWindow<GameViewWindow>();
        m_WindowManager.addWindow<ContentBrowserWindow>();
        m_WindowManager.addWindow<CodeEditorWindow>();
        m_WindowManager.addWindow<ConsoleWindow>();
        m_WindowManager.addWindow<RenderGraphWindow>();
        m_WindowManager.addWindow<InspectorWindow>();
        m_Initialized = true;
    }

    void EditorApp::startProjectLoading(const std::filesystem::path& projectRoot)
    {
        m_Loading.projectRoot = projectRoot.lexically_normal();
        m_Loading.phase       = LoadingPhase::Pending;
        m_Loading.progress    = 0.04f;
        m_Loading.message     = "Preparing editor workspace...";
    }

    bool EditorApp::updateProjectLoading(EditorContext& ctx)
    {
        if (!ctx.services)
            return false;

        const auto projectRoot = ctx.state.currentProject.lexically_normal();
        if (projectRoot.empty())
        {
            m_SyncedProject.clear();
            m_Loading = {};
            m_PlayModeSnapshot.reset();
            m_PlaybackWasPlaying = false;
            return false;
        }

        if (projectRoot != m_SyncedProject && m_Loading.phase == LoadingPhase::Idle)
        {
            // Start on a light frame so the splash can be presented before heavy loading work runs.
            startProjectLoading(projectRoot);
            return true;
        }

        if (m_Loading.phase == LoadingPhase::Idle)
            return false;

        if (projectRoot != m_Loading.projectRoot)
        {
            startProjectLoading(projectRoot);
            return true;
        }

        switch (m_Loading.phase)
        {
            case LoadingPhase::Pending:
                m_Loading.phase    = LoadingPhase::ConfigureAssets;
                m_Loading.progress = 0.12f;
                m_Loading.message  = "Configuring project assets...";
                return true;

            case LoadingPhase::ConfigureAssets:
            {
                auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
                if (!assetService)
                {
                    m_Loading = {};
                    ctx.state.statusMessage = "Project load failed: asset service unavailable.";
                    return false;
                }

                vultra::AssetSystemDesc desc;
                desc.assetRoot      = (projectRoot / ctx.state.currentAssetRoot).lexically_normal().generic_string();
                desc.importedFolder = "imported";
                desc.registryFile   = "asset_registry.tsv";
                desc.scheme         = "res";
                desc.keepCpuCopy    = true;
                assetService->configure(desc);

                m_SyncedProject         = projectRoot;
                m_PlayModeSnapshot.reset();
                m_PlaybackWasPlaying    = false;
                ctx.state.statusMessage = "Loaded project assets: " + desc.assetRoot;

                m_Loading.phase    = LoadingPhase::LoadScene;
                m_Loading.progress = 0.55f;
                m_Loading.message  = ctx.state.currentDefaultScene.empty()
                                         ? "Preparing editor windows..."
                                         : "Loading scene " + ctx.state.currentDefaultScene + "...";
                return true;
            }

            case LoadingPhase::LoadScene:
            {
                auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
                auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
                if (sceneService && worldService && !ctx.state.currentDefaultScene.empty())
                {
                    const auto root = sceneService->instantiateScene(
                        worldService->world(), ctx.state.currentDefaultScene, entt::null, true);
                    if (root != entt::null)
                    {
                        ctx.state.sceneDirty = false;
                        ctx.state.statusMessage = "Loaded default scene: " + ctx.state.currentDefaultScene;
                    }
                }

                m_Loading.phase    = LoadingPhase::Finalize;
                m_Loading.progress = 0.88f;
                m_Loading.message  = "Opening editor...";
                return true;
            }

            case LoadingPhase::Finalize:
                m_Loading.progress = 1.0f;
                m_Loading.message  = "Ready.";
                m_Loading          = {};
                return false;

            case LoadingPhase::Idle:
                return false;
        }

        return false;
    }

    void EditorApp::drawLoadingOverlay() const
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
            return;

        ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
        const ImVec2 min     = viewport->WorkPos;
        const ImVec2 max {viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y};
        drawList->AddRectFilled(min, max, IM_COL32(10, 13, 17, 120));

        const ImVec2 windowSize {460.0f, 156.0f};
        const ImVec2 windowPos {
            viewport->WorkPos.x + (viewport->WorkSize.x - windowSize.x) * 0.5f,
            viewport->WorkPos.y + (viewport->WorkSize.y - windowSize.y) * 0.5f,
        };

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 {22.0f, 18.0f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4 {0.055f, 0.071f, 0.090f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4 {0.22f, 0.30f, 0.38f, 0.95f});

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoNavFocus;
        if (ImGui::Begin("##VultraEditorLoading", nullptr, flags))
        {
            ImGui::TextUnformatted("VultraEngine Editor");
            ImGui::Spacing();
            ImGui::TextDisabled("%s", m_Loading.projectRoot.empty()
                                          ? "Loading project..."
                                          : m_Loading.projectRoot.filename().generic_string().c_str());
            ImGui::Spacing();
            ImGui::TextWrapped("%s", m_Loading.message.empty() ? "Loading..." : m_Loading.message.c_str());
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4 {0.25f, 0.62f, 0.94f, 1.0f});
            ImGui::ProgressBar(std::clamp(m_Loading.progress, 0.0f, 1.0f), ImVec2 {-1.0f, 10.0f}, "");
            ImGui::PopStyleColor();
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void EditorApp::shutdown(EditorContext& ctx)
    {
        if (ctx.services)
        {
            if (auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>())
                backendService->renderDevice().waitIdle();
        }

        m_WindowManager.destroy(ctx);
        m_Initialized        = false;
        m_DefaultLayoutBuilt = false;
        m_SyncedProject.clear();
        m_Loading = {};
        m_PlayModeSnapshot.reset();
        m_PlaybackWasPlaying = false;
        ctx.state.editorStepRequested = false;
    }

    void EditorApp::beginDockSpace()
    {
#ifdef IMGUI_HAS_DOCK
        static bool dockSpaceOpen = true;
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 {0.0f, 0.0f});

        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                       ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("VultraDockSpace", &dockSpaceOpen, windowFlags);
        ImGui::PopStyleVar(3);
#endif
    }

    void EditorApp::endDockSpace()
    {
#ifdef IMGUI_HAS_DOCK
        ImGui::End();
#endif
    }

    void EditorApp::buildDefaultDockLayout()
    {
#ifdef IMGUI_HAS_DOCK
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport || viewport->WorkSize.x < 64.0f || viewport->WorkSize.y < 64.0f)
            return;

        const ImGuiID id = dockSpaceId();
        ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(id);
        const bool missingNode = dockNode == nullptr;
        if (!m_DefaultLayoutBuilt || missingNode)
        {
            m_DefaultLayoutBuilt = true;

            ImGui::DockBuilderRemoveNode(id);
            ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);

            ImGuiID mainId   = id;
            ImGuiID leftId   = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 0.24f, nullptr, &mainId);
            ImGuiID rightId  = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 0.28f, nullptr, &mainId);
            ImGuiID bottomId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Down, 0.30f, nullptr, &mainId);

            const auto dockWindow = [&](const char* name, const ImGuiID target) {
                for (const auto& window : m_WindowManager.windows())
                {
                    if (window->name() == name)
                    {
                        ImGui::DockBuilderDockWindow(window->title().c_str(), target);
                        return;
                    }
                }
                ImGui::DockBuilderDockWindow(name, target);
            };

            dockWindow("Scene Hierarchy", leftId);
            dockWindow("Scene View", mainId);
            dockWindow("Game View", mainId);
            dockWindow("Code Editor", mainId);
            dockWindow("Render Graph", mainId);
            dockWindow("Inspector", rightId);
            dockWindow("Content Browser", bottomId);
            dockWindow("Console", bottomId);
            ImGui::DockBuilderFinish(id);
        }

        ImGui::DockSpace(id, ImVec2 {0.0f, 0.0f}, ImGuiDockNodeFlags_None);
#endif
    }

    void EditorApp::resetDefaultDockLayout()
    {
#ifdef IMGUI_HAS_DOCK
        ImGui::DockBuilderRemoveNode(dockSpaceId());
        m_DefaultLayoutBuilt = false;
#endif
    }
} // namespace vultra_app
