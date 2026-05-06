#include "editor_app/editor_app.hpp"

#include "editor_app/ui/windows/asset_browser_window.hpp"
#include "editor_app/ui/windows/console_window.hpp"
#include "editor_app/ui/windows/inspector_window.hpp"
#include "editor_app/ui/windows/scene_hierarchy_window.hpp"
#include "editor_app/ui/windows/scene_view_window.hpp"
#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/function/asset/asset_system.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>

#include <imgui.h>
#include <imgui_internal.h>

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

        if (m_ShowAboutPopup)
        {
            ImGui::OpenPopup("About VultraEngine Editor");
            m_ShowAboutPopup = false;
        }

        if (ImGui::BeginPopupModal("About VultraEngine Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("VultraEngine Editor");
            ImGui::Separator();
            ImGui::TextUnformatted("Integrated editor shell migrated from the Vultra-dev reference.");
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void EditorApp::ensureInitialized()
    {
        if (m_Initialized)
            return;

        m_WindowManager.addWindow<SceneHierarchyWindow>();
        m_WindowManager.addWindow<SceneViewWindow>();
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
                ctx.state.mode                = AppMode::Launcher;
                ctx.state.statusMessage       = "Returned to Project Launcher.";
                m_SyncedProject.clear();
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
            if (ImGui::MenuItem("About"))
                m_ShowAboutPopup = true;
            ImGui::EndMenu();
        }

        ImGui::TextUnformatted("VultraEngine Editor");
        ImGui::EndMainMenuBar();
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
        ImGui::DockBuilderDockWindow("Inspector", rightId);
        ImGui::DockBuilderDockWindow("Assets", bottomId);
        ImGui::DockBuilderDockWindow("Console", bottomId);
        ImGui::DockBuilderFinish(dockSpaceId);
#endif
    }
} // namespace vultra_app
