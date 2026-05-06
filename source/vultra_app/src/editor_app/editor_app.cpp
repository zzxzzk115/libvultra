#include "editor_app/editor_app.hpp"

#include "editor_app/ui/windows/asset_browser_window.hpp"
#include "editor_app/ui/windows/console_window.hpp"
#include "editor_app/ui/windows/game_view_window.hpp"
#include "editor_app/ui/windows/inspector_window.hpp"
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

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <entt/entity/entity.hpp>
#include <filesystem>

namespace vultra_app
{
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
        ensureInitialized();
        syncProjectRuntime(ctx);
        buildDefaultDockLayout();
        drawMainMenuBar(ctx);
        m_WindowManager.draw(ctx);
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
            scriptService->setPlaybackState(ctx.state.editorPlaying, ctx.state.editorPaused);

        m_PlaybackWasPlaying = ctx.state.editorPlaying;
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
    }

    void EditorApp::ensureInitialized()
    {
        if (m_Initialized)
            return;

        m_WindowManager.addWindow<SceneHierarchyWindow>();
        m_WindowManager.addWindow<SceneViewWindow>();
        m_WindowManager.addWindow<GameViewWindow>();
        m_WindowManager.addWindow<AssetBrowserWindow>();
        m_WindowManager.addWindow<ConsoleWindow>();
        m_WindowManager.addWindow<InspectorWindow>();
        m_Initialized = true;
    }

    void EditorApp::syncProjectRuntime(EditorContext& ctx)
    {
        if (!ctx.services)
            return;

        const auto projectRoot = ctx.state.currentProject.lexically_normal();
        if (projectRoot.empty())
        {
            m_SyncedProject.clear();
            m_PlayModeSnapshot.reset();
            m_PlaybackWasPlaying = false;
            return;
        }

        if (projectRoot == m_SyncedProject)
            return;

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        if (!assetService)
            return;

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

        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (sceneService && worldService && !ctx.state.currentDefaultScene.empty())
        {
            const auto root = sceneService->instantiateScene(
                worldService->world(), ctx.state.currentDefaultScene, entt::null, true);
            if (root != entt::null)
                ctx.state.statusMessage = "Loaded default scene: " + ctx.state.currentDefaultScene;
        }
    }

    void EditorApp::drawMainMenuBar(EditorContext& ctx)
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Blank Scene"))
                ctx.state.statusMessage = "Created an empty scene workspace.";
            if (ImGui::MenuItem("Back to Launcher"))
            {
                ctx.state.currentProject.clear();
                ctx.state.currentProjectName.clear();
                ctx.state.selectedSourceAsset.clear();
                ctx.state.currentAssetRoot    = "resources";
                ctx.state.currentDefaultScene = "res://scenes/main.vscn";
                ctx.state.editorPlaying       = false;
                ctx.state.editorPaused        = false;
                ctx.state.mode                = AppMode::Launcher;
                ctx.state.statusMessage       = "Returned to Project Launcher.";
                m_SyncedProject.clear();
                m_PlayModeSnapshot.reset();
                m_PlaybackWasPlaying = false;
                shutdown(ctx);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            for (const auto& window : m_WindowManager.windows())
                ImGui::MenuItem(window->name().c_str(), nullptr, &window->open());
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About Vultra Editor"))
                m_ShowAboutPopup = true;
            ImGui::EndMenu();
        }

        ImGui::TextUnformatted("VultraEngine Editor");
        ImGui::EndMainMenuBar();
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
        m_PlayModeSnapshot.reset();
        m_PlaybackWasPlaying = false;
    }

    void EditorApp::buildDefaultDockLayout()
    {
#ifdef IMGUI_HAS_DOCK
        if (m_DefaultLayoutBuilt)
            return;

        ImGuiID dockSpaceId = ImGui::GetID("DockSpace");
        if (ImGui::DockBuilderGetNode(dockSpaceId) == nullptr)
            return;

        m_DefaultLayoutBuilt = true;

        ImGui::DockBuilderRemoveNode(dockSpaceId);
        ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceId, ImGui::GetMainViewport()->Size);

        ImGuiID mainId   = dockSpaceId;
        ImGuiID leftId   = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 0.24f, nullptr, &mainId);
        ImGuiID rightId  = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 0.28f, nullptr, &mainId);
        ImGuiID bottomId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Down, 0.30f, nullptr, &mainId);

        ImGui::DockBuilderDockWindow("Scene Hierarchy", leftId);
        ImGui::DockBuilderDockWindow("Scene View", mainId);
        ImGui::DockBuilderDockWindow("Game View", mainId);
        ImGui::DockBuilderDockWindow("Inspector", rightId);
        ImGui::DockBuilderDockWindow("Assets", bottomId);
        ImGui::DockBuilderDockWindow("Console", bottomId);
        ImGui::DockBuilderFinish(dockSpaceId);
#endif
    }
} // namespace vultra_app
