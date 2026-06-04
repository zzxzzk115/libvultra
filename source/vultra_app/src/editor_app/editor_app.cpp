#include "editor_app/editor_app.hpp"

#include "common/system_memory.hpp"
#include "editor_app/project_asset_utils.hpp"
#include "editor_app/selection.hpp"
#include "editor_app/ui/editor_top_bar.hpp"
#include "editor_app/ui/settings_widgets.hpp"
#include "editor_app/ui/windows/code_editor_window.hpp"
#include "editor_app/ui/windows/console_window.hpp"
#include "editor_app/ui/windows/content_browser_window.hpp"
#include "editor_app/ui/windows/frame_debugger_window.hpp"
#include "editor_app/ui/windows/game_view_window.hpp"
#include "editor_app/ui/windows/history_window.hpp"
#include "editor_app/ui/windows/inspector_window.hpp"
#include "editor_app/ui/windows/animator_graph_window.hpp"
#include "editor_app/ui/windows/material_graph_window.hpp"
#include "editor_app/ui/windows/profiler_window.hpp"
#include "editor_app/ui/windows/render_graph_window.hpp"
#include "editor_app/ui/windows/scene_hierarchy_window.hpp"
#include "editor_app/ui/windows/scene_view_window.hpp"
#include "editor_app/ui/windows/world_viewer_window.hpp"
#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/asset/asset_system.hpp>
#include <vultra/function/imgui/imgui_theme.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/animation_service.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/job_service.hpp>
#include <vultra/function/services/physics_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/script_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

#include <IconsMaterialDesignIcons.h>
#ifdef VULTRA_HAS_VASSET_IMPORT
#include <builtin_shaders.hpp>
#include <vasset/vasset_importers.hpp>
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <entt/entity/entity.hpp>
#include <filesystem>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <ranges>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef VULTRA_HAS_VASSET_IMPORT
namespace
{
    vasset::VAssetImporter::ImportOptions makeEditorAssetImportOptions()
    {
        vasset::VAssetImporter::ImportOptions options;
        options.shaderVirtualIncludes.reserve(builtin_shader_include_sources_count);
        for (size_t i = 0; i < builtin_shader_include_sources_count; ++i)
        {
            const auto& source = builtin_shader_include_sources[i];
            options.shaderVirtualIncludes.push_back({
                .virtualPath = source.path,
                .sourceText  = std::string(reinterpret_cast<const char*>(source.data), source.size),
            });
        }
        return options;
    }
} // namespace
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#ifdef VULTRA_HAS_VASSET_IMPORT
#include <vasset/tool_cli.hpp>
#include <vasset/vasset_importers.hpp>
#include <vasset/vasset_registry.hpp>
#endif

namespace vultra_app
{
    namespace
    {
        ImGuiID dockSpaceId() { return ImHashStr("VultraDockSpace"); }

        std::string lowerString(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool hasSuffix(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        bool isShaderSource(const std::filesystem::path& path)
        {
            const auto name = lowerString(path.filename().generic_string());
            const auto ext  = lowerString(path.extension().generic_string());
            return ext == ".vshader" || ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp" ||
                   ext == ".hlsl" || hasSuffix(name, ".vert.vshader") || hasSuffix(name, ".frag.vshader") ||
                   hasSuffix(name, ".comp.vshader");
        }

        bool isMaterialGraphSource(const std::filesystem::path& path)
        {
            const auto name = lowerString(path.filename().generic_string());
            const auto ext  = lowerString(path.extension().generic_string());
            return ext == ".vmatgraph" || hasSuffix(name, ".vmatgraph.json");
        }

        bool isMaterialSource(const std::filesystem::path& path)
        {
            return hasSuffix(lowerString(path.filename().generic_string()), ".vmat.json");
        }

        bool isRenderPipelineSource(const std::filesystem::path& path)
        {
            const auto name = lowerString(path.filename().generic_string());
            const auto ext  = lowerString(path.extension().generic_string());
            return isShaderSource(path) || ext == ".json" || hasSuffix(name, ".vfeature.lua") ||
                   hasSuffix(name, ".vsrp.lua") || hasSuffix(name, ".vshaderlib.lua") || isMaterialGraphSource(path) ||
                   isMaterialSource(path);
        }

        std::filesystem::path assetRootPath(const EditorContext& ctx)
        {
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        std::string pathToResUri(const EditorContext& ctx, const std::filesystem::path& path)
        {
            const auto      root = assetRootPath(ctx);
            std::error_code ec;
            const auto      rel     = std::filesystem::relative(path.lexically_normal(), root, ec);
            const auto      relText = rel.generic_string();
            if (ec || rel.empty() || relText == ".." || relText.starts_with("../"))
                return {};
            return "res://" + relText;
        }

        std::vector<std::filesystem::path> shaderLibraryManifests(const EditorContext& ctx)
        {
            std::vector<std::filesystem::path> manifests;
            const auto                         root = assetRootPath(ctx);
            std::error_code                    ec;
            for (auto it = std::filesystem::recursive_directory_iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator {};
                 it.increment(ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }
                if (!it->is_regular_file(ec) || ec)
                {
                    ec.clear();
                    continue;
                }
                if (hasSuffix(lowerString(it->path().filename().generic_string()), ".vshaderlib.lua"))
                    manifests.push_back(it->path().lexically_normal());
            }
            return manifests;
        }

        std::vector<std::filesystem::path> collectImportSourceFiles(const std::vector<std::filesystem::path>& paths)
        {
            std::vector<std::filesystem::path> files;
            for (const auto& path : paths)
            {
                std::error_code ec;
                const auto      normalized = path.lexically_normal();
                if (std::filesystem::is_regular_file(normalized, ec) && !ec)
                {
                    files.push_back(normalized);
                    continue;
                }
                ec.clear();
                if (!std::filesystem::is_directory(normalized, ec) || ec)
                    continue;

                for (auto it = std::filesystem::recursive_directory_iterator(
                         normalized, std::filesystem::directory_options::skip_permission_denied, ec);
                     it != std::filesystem::recursive_directory_iterator {};
                     it.increment(ec))
                {
                    if (ec)
                    {
                        ec.clear();
                        continue;
                    }
                    if (it->is_regular_file(ec) && !ec)
                        files.push_back(it->path().lexically_normal());
                    else
                        ec.clear();
                }
            }
            std::ranges::sort(files);
            files.erase(std::ranges::unique(files).begin(), files.end());
            return files;
        }

        void refreshImportedEditorSources(EditorContext& ctx, const std::vector<std::filesystem::path>& sourceFiles)
        {
            if (sourceFiles.empty() || !ctx.services)
                return;

            auto* assets = ctx.services->tryGet<vultra::IAssetService>();
            auto* render = ctx.services->tryGet<vultra::IRenderService>();
            if (!assets && !render)
                return;

            bool hasMaterialGraph = false;
            bool hasShaderSource  = false;
            bool reloadPipeline   = false;
            for (const auto& path : sourceFiles)
            {
                const auto uri = assets ? pathToResUri(ctx, path) : std::string {};
                if (assets && isMaterialGraphSource(path) && !uri.empty())
                {
                    assets->clearTextAssetOverride(uri);
                    hasMaterialGraph = true;
                }
                hasShaderSource = hasShaderSource || isShaderSource(path);
                reloadPipeline  = reloadPipeline || isRenderPipelineSource(path);
            }

            if (render && hasShaderSource)
            {
                for (const auto& manifest : shaderLibraryManifests(ctx))
                {
                    const auto uri = pathToResUri(ctx, manifest);
                    if (!uri.empty())
                        render->reloadProjectShaderLibrary(uri);
                }
            }
            if (render && (reloadPipeline || hasMaterialGraph))
                render->reloadRenderPipeline();
        }

        glm::mat4 localTransformMatrix(const vultra::TransformComponent& transform)
        {
            return glm::translate(glm::mat4(1.0f), transform.position) * glm::mat4_cast(transform.rotation) *
                   glm::scale(glm::mat4(1.0f), transform.scale);
        }

        glm::mat4 worldTransformMatrix(vultra::World& world, entt::entity entity)
        {
            auto& reg = world.registry();
            glm::mat4 result(1.0f);
            for (auto current = entity; current != entt::null && reg.valid(current);)
            {
                if (const auto* transform = reg.try_get<vultra::TransformComponent>(current))
                    result = localTransformMatrix(*transform) * result;
                const auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(current);
                current               = hierarchy ? hierarchy->parent : entt::null;
            }
            return result;
        }

        void requestSceneViewAlignToPrimaryCamera(EditorContext& ctx, vultra::World& world)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::TransformComponent, vultra::CameraComponent>();

            entt::entity best         = entt::null;
            int          bestPriority = std::numeric_limits<int>::min();
            for (auto entity : view)
            {
                const auto& camera = view.get<vultra::CameraComponent>(entity);
                if (!camera.primary)
                    continue;
                if (best == entt::null || camera.priority >= bestPriority)
                {
                    best         = entity;
                    bestPriority = camera.priority;
                }
            }

            if (best == entt::null)
                return;

            const auto& camera         = reg.get<vultra::CameraComponent>(best);
            const auto  worldTransform = worldTransformMatrix(world, best);
            auto&       request        = ctx.state.sceneCameraAlignRequest;
            request.pending            = true;
            request.position           = glm::vec3(worldTransform[3]);
            request.rotation           = glm::normalize(glm::quat_cast(worldTransform));
            request.fovYDegrees        = camera.fovYDegrees;
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

        std::vector<std::string> collectProjectRenderGraphUris(const std::filesystem::path& projectRoot,
                                                               const std::string&           assetRootName)
        {
            return collectProjectAssetUrisWithSuffix(projectRoot, assetRootName, ".vrg.json");
        }

        std::string formatBytes(const uint64_t bytes)
        {
            constexpr const char* kUnits[] = {"B", "KB", "MB", "GB"};
            double                value    = static_cast<double>(bytes);
            size_t                unit     = 0;
            while (value >= 1024.0 && unit + 1 < (sizeof(kUnits) / sizeof(kUnits[0])))
            {
                value /= 1024.0;
                ++unit;
            }

            char buffer[64] {};
            if (unit == 0)
                std::snprintf(buffer, sizeof(buffer), "%.0f %s", value, kUnits[unit]);
            else
                std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, kUnits[unit]);
            return buffer;
        }

        void resetWindowModeForShellState(IWindowService& windowService)
        {
            auto& window = windowService.window();
            if (window.isFullscreen())
                window.setFullscreen(false);
            if (window.isMaximized())
                window.restore();
        }

        bool viewportMatchesWindow(EditorContext& ctx)
        {
            if (!ctx.services)
                return true;

            auto* windowService = ctx.services->tryGet<IWindowService>();
            if (!windowService)
                return true;

            const auto* viewport = ImGui::GetMainViewport();
            if (!viewport)
                return true;

            const auto extent = windowService->window().getExtent();
            return viewport->WorkSize.x >= static_cast<float>(std::max(extent.x, 1)) - 2.0f &&
                   viewport->WorkSize.y >= static_cast<float>(std::max(extent.y, 1)) - 2.0f;
        }

        uint32_t selectedEntityPickingId(EditorContext& ctx)
        {
            if (Selection::lastCategory() != SelectionCategory::Entity)
                return 0u;

            const auto selectedId = Selection::lastId();
            if (!selectedId.valid() || !ctx.services)
                return 0u;

            auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
            if (!worldService)
                return 0u;

            bool entityExists = false;
            auto view = worldService->world().registry().view<vultra::IDComponent>();
            for (auto e : view)
            {
                if (view.get<vultra::IDComponent>(e).uuid == selectedId)
                {
                    entityExists = true;
                    break;
                }
            }
            if (!entityExists)
                return 0u;

            return vultra::makeEntityPickingId(selectedId);
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
            engine.ctx().config.asset.enableImportScan = false;
            engine.ctx().config.render.renderPipelineAsset = project->editingRenderGraph;
            engine.ctx().config.render.renderPipelineRendererKey.clear();
            return;
        }

        engine.ctx().config.asset.loadFromVPK = false;
        engine.ctx().config.asset.assetRoot   = (projectPath / "resources").lexically_normal().generic_string();
        engine.ctx().config.asset.enableImportScan = false;
    }

    void EditorApp::logStartup(const LaunchOptions& options)
    {
        VULTRA_CLIENT_INFO("[VultraEditor] Editor active. project='{}'", options.projectPath);
    }

    void EditorApp::tick(EditorContext& ctx)
    {
        ctx.thumbnails       = &m_ThumbnailService;
        ctx.history          = &m_History;
        const auto assetRoot = ctx.state.currentProject.empty() ?
                                   std::filesystem::path {} :
                                   (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        m_FileWatcher.setRoot(assetRoot);
        if (m_FileWatcher.consumeChanged())
            ++ctx.state.assetFileGeneration;

        syncPlaybackState(ctx);
        updateRuntimeMcp(ctx);
        updateBuildAndRun(ctx);
        (void)updateProjectLoading(ctx);
        updateBackgroundAssetImport(ctx);
        updateBackgroundThumbnails(ctx);
        if (ctx.state.mode == AppMode::Editor && !isProjectLoading())
        {
            ensureInitialized();
            m_WindowManager.tick(ctx);
        }
    }

    bool EditorApp::isProjectLoading() const { return m_Loading.phase != LoadingPhase::Idle; }

    void EditorApp::updateRuntimeMcp(EditorContext& ctx)
    {
        m_RuntimeMcpServer.syncDesiredState(ctx);
        m_RuntimeMcpServer.executePending(ctx);
    }

    void EditorApp::draw(EditorContext& ctx)
    {
        vultra::RuntimeProfiler::ExternalScope perf {"EditorApp::draw"};
        ctx.thumbnails = &m_ThumbnailService;
        ctx.history    = &m_History;
        {
            vultra::RuntimeProfiler::ExternalScope scope {"EditorApp::applySettings"};
            ui::applyEditorSettingsRuntime(ctx.state.editorSettings);
        }
        if (m_Loading.phase == LoadingPhase::Complete)
        {
            applyEditorWindow(ctx);
            if (!viewportMatchesWindow(ctx))
            {
                drawLoadingOverlay(ctx);
                return;
            }
            m_Loading = {};
            m_DefaultLayoutBuilt = false;
        }

        if (isProjectLoading())
        {
            drawLoadingOverlay(ctx);
            return;
        }

        ensureInitialized();
        ctx.state.sceneViewVisibleLastFrame = ctx.state.sceneViewVisible;
        ctx.state.sceneViewVisible          = false;
        ctx.state.gameViewVisibleLastFrame = ctx.state.gameViewVisible;
        ctx.state.gameViewRenderTargetAvailableLastFrame = ctx.state.gameViewRenderTargetAvailable;
        ctx.state.gameViewVisible          = false;
        ctx.state.gameViewRenderTargetAvailable = false;
        {
            vultra::RuntimeProfiler::ExternalScope scope {"EditorApp::topBar"};
            drawEditorTopBar(ctx,
                             m_WindowManager.windows(),
                             EditorTopBarActions {
                             .newBlankScene =
                                 [this](EditorContext& topBarCtx) {
                                     (void)executeCommand(topBarCtx, "editor.new_scene", nlohmann::json::object());
                                 },
                             .saveScene   = [this](EditorContext& topBarCtx) {
                                 (void)executeCommand(topBarCtx, "editor.save_scene", nlohmann::json::object());
                             },
                             .buildAndRun = [this](EditorContext& topBarCtx) {
                                 (void)executeCommand(topBarCtx, "editor.build_and_run", nlohmann::json::object());
                             },
                             .backToLauncher =
                                 [this](EditorContext& topBarCtx) {
                                     (void)executeCommand(topBarCtx, "editor.back_to_launcher", nlohmann::json::object());
                                 },
                             .resetLayout = [this](EditorContext&) { resetDefaultDockLayout(); },
                             .showAbout   = [this](EditorContext&) { m_ShowAboutPopup = true; },
                             });
        }
        if (ctx.state.mode != AppMode::Editor)
            return;

        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            (void)executeCommand(ctx, "editor.save_scene", nlohmann::json::object());
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
            (void)executeCommand(ctx, "editor.redo", nlohmann::json::object());
        else if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
            (void)executeCommand(ctx, "editor.undo", nlohmann::json::object());
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5))
            (void)executeCommand(ctx, "editor.build_and_run", nlohmann::json::object());

        if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
        {
            renderService->setFrameGraphSnapshotCaptureEnabled(false);
            renderService->setFrameGraphTextureCaptureEnabled(false);
        }
        m_RuntimeMcpServer.executePending(ctx);

        {
            vultra::RuntimeProfiler::ExternalScope scope {"EditorApp::taskBar"};
            drawEditorTaskBar(ctx);
        }
        updateEditorGameClock(ctx);
        {
            vultra::RuntimeProfiler::ExternalScope scope {"EditorApp::dockSpace"};
            beginDockSpace();
            buildDefaultDockLayout();
        }
        {
            vultra::RuntimeProfiler::ExternalScope scope {"EditorApp::windowManager"};
            m_WindowManager.draw(ctx);
        }
        {
            vultra::RuntimeProfiler::ExternalScope scope {"EditorApp::endDockSpace"};
            endDockSpace();
        }
        {
            vultra::RuntimeProfiler::ExternalScope scope {"EditorApp::commandsAndHistory"};
            processEditorCommands(ctx);
            m_History.observeScene(ctx);
        }

        if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
            renderService->builtinRenderSettings().selectionOutline.selectedEntityId = selectedEntityPickingId(ctx);

        {
            vultra::RuntimeProfiler::ExternalScope scope {"EditorApp::playbackAndPopups"};
            syncPlaybackState(ctx);
            drawBuildRunConfigurePopup(ctx);
            drawImportProgressPopup();
            drawBuildRunPopup();
            drawProjectSettingsPopup(ctx);
            drawEditorSettingsPopup(ctx);
            drawBuildSettingsPopup(ctx);
            drawOpenSceneConfirmPopup(ctx);
        }

        if (m_ShowAboutPopup)
        {
            ImGui::OpenPopup("About Vultra Editor");
            m_ShowAboutPopup = false;
        }

        ui::centerNextModalInCurrentWindow();
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
        ctx.state.editorSteppingThisFrame = ctx.state.editorStepRequested;
        if (!ctx.services)
            return;

        auto* scriptService  = ctx.services->tryGet<vultra::IScriptService>();
        auto* physicsService = ctx.services->tryGet<vultra::IPhysicsService>();
        auto* animationService = ctx.services->tryGet<vultra::IAnimationService>();
        const bool stepRequested = ctx.state.editorStepRequested;

        if (ctx.state.editorPlaying && !m_PlaybackWasPlaying)
            capturePlayModeSnapshot(ctx);

        if (!ctx.state.editorPlaying && m_PlaybackWasPlaying)
        {
            if (animationService)
                animationService->setPlaybackState(false, false);
            if (scriptService)
                scriptService->setPlaybackState(false, false);
            if (physicsService)
                physicsService->setPlaybackState(false, false);
            restorePlayModeSnapshot(ctx);
        }

        if (physicsService)
        {
            physicsService->setPlaybackState(ctx.state.editorPlaying, ctx.state.editorPaused);
            if (stepRequested)
                physicsService->requestSingleStep();
        }

        if (animationService)
        {
            animationService->setPlaybackState(ctx.state.editorPlaying, ctx.state.editorPaused);
            if (stepRequested)
                animationService->requestSingleStep();
        }

        if (scriptService)
        {
            scriptService->setPlaybackState(ctx.state.editorPlaying, ctx.state.editorPaused);
            if (stepRequested)
            {
                scriptService->requestSingleStep();
            }
        }
        if (stepRequested)
            ctx.state.editorStepRequested = false;

        m_PlaybackWasPlaying = ctx.state.editorPlaying;
    }

    void EditorApp::updateEditorGameClock(EditorContext& ctx)
    {
        ctx.state.editorGameDeltaSeconds = 0.0f;
        if (!ctx.state.editorPlaying)
        {
            ctx.state.editorGameTimeSeconds = 0.0f;
            return;
        }

        if (!m_PlaybackWasPlaying)
        {
            ctx.state.editorGameTimeSeconds = 0.0f;
            return;
        }

        const bool stepRequested = ctx.state.editorStepRequested;
        if (ctx.state.editorPaused && !stepRequested)
            return;

        const float delta = stepRequested ? (1.0f / 60.0f) : std::max(ImGui::GetIO().DeltaTime, 0.0f);
        ctx.state.editorGameDeltaSeconds = delta;
        ctx.state.editorGameTimeSeconds  = std::max(0.0f, ctx.state.editorGameTimeSeconds + delta);
    }

    void EditorApp::processEditorCommands(EditorContext& ctx)
    {
        if (ctx.state.pendingEditorCommands.empty())
            return;

        std::vector<AppState::EditorCommand> commands;
        commands.swap(ctx.state.pendingEditorCommands);
        for (const auto& command : commands)
        {
            switch (command.type)
            {
                case AppState::EditorCommandType::OpenScene:
                    (void)executeCommand(ctx, "editor.open_scene", {{"uri", command.payload}});
                    break;
                case AppState::EditorCommandType::OpenMaterialGraph:
                    (void)executeCommand(ctx, "editor.open_material_graph", {{"uri", command.payload}});
                    break;
                case AppState::EditorCommandType::OpenRenderGraph:
                    (void)executeCommand(ctx, "editor.open_render_graph", {{"uri", command.payload}});
                    break;
            }
        }
    }

    bool EditorApp::openSceneFromCommand(EditorContext& ctx, const std::string& sceneUri)
    {
        if (!ctx.services)
        {
            ctx.state.statusMessage = "Open scene failed: services unavailable.";
            return false;
        }
        if (ctx.state.editorPlaying)
        {
            ctx.state.statusMessage = "Stop Play Mode before opening another scene.";
            return false;
        }

        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!sceneService || !worldService)
        {
            ctx.state.statusMessage = "Open scene failed: scene/world service unavailable.";
            return false;
        }

        auto doc = sceneService->loadSceneSync(sceneUri);
        if (!doc || !doc->root)
        {
            ctx.state.statusMessage = "Open scene failed: " + sceneUri;
            return false;
        }

        auto& world = worldService->world();
        saveCurrentSceneThumbnail(ctx);
        const bool emptySyntheticScene = doc->syntheticRoot && doc->root->children.empty();
        const auto root = emptySyntheticScene ?
                              entt::null :
                              sceneService->instantiateSceneDocument(world, *doc, entt::null, true);
        if (root == entt::null)
        {
            if (!emptySyntheticScene)
            {
                ctx.state.statusMessage = "Open scene failed: " + sceneUri;
                return false;
            }
            world.clear();
        }

        Selection::clear();
        ctx.state.selectedSourceAsset.clear();
        ctx.state.currentDefaultScene = sceneUri;
        ctx.state.sceneDirty          = false;
        ctx.state.statusMessage       = "Opened scene: " + sceneUri;
        requestSceneViewAlignToPrimaryCamera(ctx, world);
        m_History.reset(ctx, "Scene Opened");
        return true;
    }

    void EditorApp::drawOpenSceneConfirmPopup(EditorContext& ctx)
    {
        if (m_OpenSceneConfirmPopup)
        {
            ImGui::OpenPopup("Open Scene");
            m_OpenSceneConfirmPopup = false;
        }

        ui::centerNextModalInCurrentWindow();
        if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextWrapped("Discard unsaved changes and open this scene?");
            ImGui::TextWrapped("%s", m_PendingOpenSceneUri.c_str());
            if (ImGui::Button("Open", ImVec2(90.0f, 0.0f)))
            {
                (void)openSceneFromCommand(ctx, m_PendingOpenSceneUri);
                m_PendingOpenSceneUri.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
            {
                ctx.state.statusMessage = "Open scene cancelled.";
                m_PendingOpenSceneUri.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void EditorApp::drawImportProgressPopup()
    {
        if (!m_BackgroundAssetImport && !m_ImportProgressPopupPendingOpen)
            return;

        if (m_ImportProgressPopupPendingOpen)
        {
            ui::centerNextModalInCurrentWindow();
            ImGui::OpenPopup("Import Assets");
            m_ImportProgressPopupPendingOpen = false;
        }

        bool popupOpen = true;
        if (!ImGui::BeginPopupModal("Import Assets", &popupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        float       progress = 0.0f;
        std::string message  = "Preparing asset import...";
        std::string currentItem;
        size_t      processedItems = 0;
        size_t      totalItems     = 0;
        if (m_ImportProgress)
        {
            std::scoped_lock lock(m_ImportProgress->mutex);
            progress       = std::clamp(m_ImportProgress->progress, 0.0f, 1.0f);
            message        = m_ImportProgress->message.empty() ? message : m_ImportProgress->message;
            currentItem    = m_ImportProgress->currentItem;
            processedItems = m_ImportProgress->processedItems;
            totalItems     = m_ImportProgress->totalItems;
        }

        ImGui::TextUnformatted(ICON_MDI_FILE_IMPORT " Importing assets");
        ImGui::Spacing();
        ImGui::ProgressBar(progress, ImVec2 {420.0f, 0.0f});
        ImGui::TextWrapped("%s", message.c_str());
        if (!currentItem.empty())
        {
            ImGui::TextDisabled("Item");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", currentItem.c_str());
        }
        if (totalItems > 0)
            ImGui::TextDisabled("%zu / %zu", std::min(processedItems, totalItems), totalItems);

        ImGui::EndPopup();
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
            m_History.markCurrentClean(ctx);
        }
        else
        {
            ctx.state.statusMessage = "Scene save failed: " + ctx.state.currentDefaultScene;
        }
    }

    void EditorApp::saveCurrentSceneThumbnail(EditorContext& ctx)
    {
        if (ctx.state.currentProject.empty() || ctx.state.currentDefaultScene.empty())
            return;
        (void)m_WindowManager.saveSceneThumbnail(ctx, ctx.state.currentDefaultScene);
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
            m_PlayModeSceneDirtySnapshot = false;
            ctx.state.statusMessage      = "Play mode snapshot failed: scene has no serializable root.";
            ctx.state.editorPlaying      = false;
            ctx.state.editorPaused       = false;
            return;
        }

        m_PlayModeSceneDirtySnapshot = ctx.state.sceneDirty;
        m_PlayModeSnapshot           = std::move(snapshot);
        ctx.state.statusMessage      = "Entered Play Mode.";
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
        ctx.state.sceneDirty         = m_PlayModeSceneDirtySnapshot;
        m_PlayModeSceneDirtySnapshot = false;
        Selection::clear(SelectionCategory::Entity);
        m_History.syncCurrent(ctx);
        ctx.state.statusMessage       = "Exited Play Mode. Scene state restored.";
        ctx.state.editorStepRequested = false;
        ctx.state.editorGameTimeSeconds  = 0.0f;
        ctx.state.editorGameDeltaSeconds = 0.0f;
    }

    void EditorApp::releaseEditorStateForProjectLoad(EditorContext& ctx)
    {
        if (m_Loading.releasedEditorState)
            return;

        saveCurrentSceneThumbnail(ctx);

        if (ctx.services)
        {
            if (auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>())
                backendService->renderDevice().waitIdle();
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
            {
                renderService->resetSceneState();
                renderService->setFrameGraphTextureCaptureEnabled(false);
                renderService->clearFrameGraphTexturePreviewOverrides();
            }
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                worldService->world().clear();
            if (auto* windowService = ctx.services->tryGet<IWindowService>())
                (void)windowService->window().setVisible(false);
        }

        m_WindowManager.destroy(ctx);
        m_ThumbnailService.clear(&ctx);
        m_Initialized        = false;
        m_DefaultLayoutBuilt = false;
        Selection::clear();
        m_History.clear();
        m_PlayModeSnapshot.reset();
        m_PlayModeSceneDirtySnapshot  = false;
        m_PlaybackWasPlaying          = false;
        ctx.state.editorPlaying       = false;
        ctx.state.editorPaused        = false;
        ctx.state.editorStepRequested = false;
        ctx.state.editorGameTimeSeconds  = 0.0f;
        ctx.state.editorGameDeltaSeconds = 0.0f;
        ctx.state.pendingEditorCommands.clear();
        ctx.state.renderGraphOpenRequested   = false;
        ctx.state.runtimeFrameGraphViewerOpenRequested = false;
        ctx.state.materialGraphOpenRequested = false;
        ctx.state.sceneViewVisible          = false;
        ctx.state.sceneViewVisibleLastFrame = false;
        ctx.state.gameViewVisible           = false;
        ctx.state.gameViewVisibleLastFrame  = false;
        ctx.state.gameViewRenderTargetAvailable = false;
        ctx.state.gameViewRenderTargetAvailableLastFrame = false;
        ctx.state.gameViewLastRenderTargetWidth  = 1280;
        ctx.state.gameViewLastRenderTargetHeight = 720;
        ctx.state.sceneCamera.valid         = false;
        ctx.state.sceneCameraAlignRequest.pending = false;
        ctx.state.scenePicking             = {};
        m_Loading.releasedEditorState = true;
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
        m_WindowManager.addWindow<MaterialGraphWindow>();
        m_WindowManager.addWindow<AnimatorGraphWindow>();
        m_WindowManager.addWindow<FrameDebuggerWindow>();
        m_WindowManager.addWindow<ProfilerWindow>();
        m_WindowManager.addWindow<InspectorWindow>();
        m_WindowManager.addWindow<HistoryWindow>();
        m_WindowManager.addWindow<WorldViewerWindow>();
        m_Initialized = true;
    }

    void EditorApp::startProjectLoading(const std::filesystem::path& projectRoot)
    {
        waitForAssetImportTask();
        m_ImportProgress.reset();
        m_Loading.projectRoot = projectRoot.lexically_normal();
        m_Loading.phase       = LoadingPhase::Pending;
        m_Loading.progress    = 0.04f;
        m_Loading.message     = "Preparing editor workspace...";
        m_SplashWindowApplied = false;
        m_EditorWindowApplied = false;
    }

    void EditorApp::startAssetImportTask(const std::filesystem::path&        projectRoot,
                                         const std::string&                 assetRoot,
                                         std::vector<std::filesystem::path> importPaths,
                                         const bool                         forceReimport)
    {
        auto progress      = std::make_shared<ImportTaskProgress>();
        progress->message  = "Scanning project assets...";
        progress->progress = 0.08f;
        m_ImportProgress   = progress;

        const auto rootPath       = projectRoot.lexically_normal();
        const auto assetRootPath  = (rootPath / assetRoot).lexically_normal();
        const auto importedFolder = std::string {"imported"};
        const auto registryFile   = std::string {"asset_registry.tsv"};

        if (!m_ImportScheduler)
            m_ImportScheduler = std::make_unique<vtask::Scheduler>();

        m_ImportResult   = {};
        m_ImportTaskDone = false;
        {
            std::scoped_lock lock(m_ImportedThumbnailMutex);
            m_PendingImportedThumbnailPaths.clear();
        }
        for (auto& path : importPaths)
            path = path.lexically_normal();
        m_ImportTask     = std::make_unique<vtask::TaskSet>(
            1, 1, [this,
                   progress,
                   assetRootPath,
                   importedFolder,
                   registryFile,
                   importPaths = std::move(importPaths),
                   forceReimport](vtask::Range) {
                ImportTaskResult result;
                result.assetRoot    = assetRootPath.generic_string();
                result.registryPath = (assetRootPath / importedFolder / registryFile).generic_string();

#ifdef VULTRA_HAS_VASSET_IMPORT
                vasset::VAssetRegistry registry;
                registry.setAssetRootPath(result.assetRoot);
                registry.setImportedFolderName(importedFolder);

                if (std::filesystem::exists(result.registryPath))
                    registry.load(result.registryPath);

                vasset::VAssetImporter importer {registry};
                auto                   options = makeEditorAssetImportOptions();
                const size_t            targetedImportCount = importPaths.size();
                options.progress               = [progress, targetedImportCount](const vasset::VAssetImporter::ImportProgress& p) {
                    std::scoped_lock lock(progress->mutex);
                    const bool keepOuterBatchProgress = targetedImportCount > 1 && p.totalFiles <= 1;
                    if (!keepOuterBatchProgress)
                    {
                        progress->processedItems = p.processedFiles;
                        progress->totalItems     = p.totalFiles;
                    }
                    progress->currentItem    = p.currentPath;
                    switch (p.phase)
                    {
                        case vasset::VAssetImporter::ImportProgress::Phase::eScan:
                            if (!keepOuterBatchProgress)
                                progress->progress = 0.08f;
                            progress->message =
                                p.currentPath.empty() ? "Scanning project assets..." : "Scanning " + p.currentPath;
                            break;
                        case vasset::VAssetImporter::ImportProgress::Phase::eImport: {
                            const float amount = p.totalFiles > 0 ? static_cast<float>(p.processedFiles) /
                                                                        static_cast<float>(p.totalFiles) :
                                                                                  1.0f;
                            if (!keepOuterBatchProgress)
                                progress->progress = 0.12f + amount * 0.68f;
                            progress->message =
                                p.currentPath.empty() ? "Importing project assets..." : "Importing " + p.currentPath;
                            break;
                        }
                        case vasset::VAssetImporter::ImportProgress::Phase::eDone:
                            if (!keepOuterBatchProgress)
                                progress->progress = 0.82f;
                            progress->message  = "Finalizing asset database...";
                            break;
                    }
                };
                importer.setOptions(options);

                vbase::Result<void, vasset::AssetError> importResult =
                    vbase::Result<void, vasset::AssetError>::ok();
                if (importPaths.empty())
                {
                    importResult = importer.importOrReimportAssetFolder(result.assetRoot, forceReimport);
                }
                else
                {
                    {
                        std::scoped_lock lock(progress->mutex);
                        progress->progress = 0.12f;
                        progress->message  = "Importing changed assets...";
                        progress->currentItem.clear();
                        progress->processedItems = 0;
                        progress->totalItems     = importPaths.size();
                    }

                    size_t processed = 0;
                    for (const auto& importPath : importPaths)
                    {
                        std::error_code relEc;
                        const auto currentPath =
                            std::filesystem::relative(importPath, assetRootPath, relEc).generic_string();
                        {
                            std::scoped_lock lock(progress->mutex);
                            const float amount = importPaths.empty() ? 1.0f :
                                                                 static_cast<float>(processed) /
                                                                     static_cast<float>(importPaths.size());
                            progress->progress = 0.12f + amount * 0.68f;
                            progress->message  = currentPath.empty() ? "Importing changed assets..." :
                                                                     "Importing " + currentPath;
                            progress->currentItem    = currentPath;
                            progress->processedItems = processed;
                            progress->totalItems     = importPaths.size();
                        }

                        std::error_code ec;
                        if (!std::filesystem::exists(importPath, ec))
                        {
                            ++processed;
                            continue;
                        }

                        if (std::filesystem::is_directory(importPath, ec))
                            importResult = importer.importOrReimportAssetFolder(importPath.generic_string(), forceReimport);
                        else
                            importResult = importer.importOrReimportAsset(importPath.generic_string(), forceReimport);

                        if (!importResult && importResult.error() != vasset::AssetError::eNotSupported)
                            break;

                        if (importResult)
                        {
                            (void)registry.save(result.registryPath);
                            std::scoped_lock lock(m_ImportedThumbnailMutex);
                            m_PendingImportedThumbnailPaths.push_back(importPath);
                        }

                        ++processed;
                    }

                    {
                        std::scoped_lock lock(progress->mutex);
                        progress->progress = 0.82f;
                        progress->message  = "Finalizing asset database...";
                        progress->processedItems = importPaths.size();
                        progress->totalItems     = importPaths.size();
                    }
                }
                if (!importResult && importResult.error() != vasset::AssetError::eNotSupported)
                {
                    result.ok    = false;
                    result.error = "asset import failed";
                }
                else
                {
                    registry.cleanup();
                }

                if (result.error.empty() && !registry.save(result.registryPath))
                {
                    result.ok    = false;
                    result.error = "failed to save asset registry";
                }
                else
                {
                    result.ok = true;
                }
#else
                                      result.ok = std::filesystem::exists(result.registryPath);
                                      if (!result.ok)
                                          result.error = "vasset importer is unavailable and no registry exists";
#endif
                m_ImportResult = std::move(result);
                m_ImportTaskDone.store(true, std::memory_order_release);
            });
        m_ImportScheduler->run(*m_ImportTask);
    }

    void EditorApp::waitForAssetImportTask()
    {
        if (!m_ImportTask)
            return;

        if (m_ImportScheduler)
            m_ImportScheduler->wait(*m_ImportTask);
        m_ImportTask.reset();
        m_ImportTaskDone.store(true, std::memory_order_release);
    }

    void EditorApp::reloadRuntimeAssetRegistry(EditorContext& ctx)
    {
        if (!ctx.services || ctx.state.currentProject.empty())
            return;

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        if (!assetService)
            return;

        if (!assetService->reloadRegistry())
        {
            const auto projectRoot = ctx.state.currentProject.lexically_normal();
            vultra::AssetSystemDesc desc;
            desc.assetRoot        = (projectRoot / ctx.state.currentAssetRoot).lexically_normal().generic_string();
            desc.importedFolder   = "imported";
            desc.registryFile     = "asset_registry.tsv";
            desc.scheme           = "res";
            desc.keepCpuCopy      = true;
            desc.enableImportScan = false;
            assetService->configure(desc);
        }

        ++ctx.state.assetFileGeneration;
    }

    void EditorApp::updateBackgroundAssetImport(EditorContext& ctx)
    {
        if (isProjectLoading())
            return;

        if (ctx.state.pendingAssetImportRefresh && !m_BackgroundAssetImport && !m_ImportTask &&
            !ctx.state.currentProject.empty())
        {
            auto importPaths = std::move(ctx.state.pendingAssetImportPaths);
            const bool forceReimport = ctx.state.pendingAssetImportForceReimport;
            ctx.state.pendingAssetImportPaths.clear();
            ctx.state.pendingAssetImportRefresh = false;
            ctx.state.pendingAssetImportForceReimport = false;
            m_BackgroundAssetImportPaths = importPaths;
            startAssetImportTask(ctx.state.currentProject, ctx.state.currentAssetRoot, std::move(importPaths), forceReimport);
            m_BackgroundAssetImport = true;
            m_ImportProgressPopupPendingOpen = true;
            ctx.state.statusMessage = "Importing project assets...";
        }

        if (!m_BackgroundAssetImport)
            return;

        std::vector<std::filesystem::path> importedThumbnailPaths;
        {
            std::scoped_lock lock(m_ImportedThumbnailMutex);
            importedThumbnailPaths.swap(m_PendingImportedThumbnailPaths);
        }
        if (!importedThumbnailPaths.empty())
        {
            reloadRuntimeAssetRegistry(ctx);
            m_ThumbnailService.prewarmSourceThumbnails(ctx, importedThumbnailPaths);
        }

        if (m_ImportProgress)
        {
            std::scoped_lock lock(m_ImportProgress->mutex);
            if (!m_ImportProgress->message.empty())
                ctx.state.statusMessage = m_ImportProgress->message;
        }

        if (!m_ImportTaskDone.load(std::memory_order_acquire))
            return;

        waitForAssetImportTask();
        auto importResult = std::move(m_ImportResult);
        m_BackgroundAssetImport = false;

        if (!importResult.ok)
        {
            ctx.state.statusMessage = "Asset import failed: " + importResult.error;
            m_BackgroundAssetImportPaths.clear();
            std::scoped_lock lock(m_ImportedThumbnailMutex);
            m_PendingImportedThumbnailPaths.clear();
            return;
        }

        reloadRuntimeAssetRegistry(ctx);
        {
            std::scoped_lock lock(m_ImportedThumbnailMutex);
            importedThumbnailPaths.swap(m_PendingImportedThumbnailPaths);
        }
        auto importedSourceFiles = collectImportSourceFiles(m_BackgroundAssetImportPaths);
        refreshImportedEditorSources(ctx, importedSourceFiles);
        if (!importedThumbnailPaths.empty())
            m_ThumbnailService.prewarmSourceThumbnails(ctx, importedThumbnailPaths);
        m_ThumbnailService.prewarmSourceThumbnails(ctx, importedSourceFiles);
        m_BackgroundAssetImportPaths.clear();
        ctx.state.statusMessage = "Imported project assets: " + importResult.assetRoot;
    }

    void EditorApp::updateBackgroundThumbnails(EditorContext& ctx)
    {
        if (isProjectLoading())
            return;

        float       thumbnailProgress = 1.0f;
        std::string thumbnailMessage;
        if (m_ThumbnailService.processQueuedTextureThumbnail(ctx, thumbnailProgress, thumbnailMessage) &&
            !thumbnailMessage.empty())
        {
            ctx.state.statusMessage = thumbnailMessage;
        }

        const auto& queuedThumbnails = m_ThumbnailService.queuedRequests();
        const bool hasRenderThumbnail =
            std::any_of(queuedThumbnails.begin(), queuedThumbnails.end(), [](const auto& request) {
                return request.kind != ui::AssetThumbnailKind::Texture;
            });
        if (!hasRenderThumbnail && !m_BackgroundRenderThumbnailActive)
            return;

        auto* sceneService = ctx.services ? ctx.services->tryGet<vultra::ISceneService>() : nullptr;
        auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
        if (!sceneService || !worldService)
            return;

        if (!m_BackgroundRenderThumbnailActive)
        {
            m_BackgroundThumbnailWorldSnapshot = sceneService->captureWorldAsScene(worldService->world(), entt::null);
            m_BackgroundRenderThumbnailActive  = true;
        }

        if (m_ThumbnailService.processLoadingThumbnail(ctx, thumbnailProgress, thumbnailMessage))
        {
            if (!thumbnailMessage.empty())
                ctx.state.statusMessage = thumbnailMessage;
            return;
        }

        if (m_BackgroundThumbnailWorldSnapshot && m_BackgroundThumbnailWorldSnapshot->root)
            sceneService->instantiateSceneDocument(worldService->world(), *m_BackgroundThumbnailWorldSnapshot, entt::null, true);
        m_BackgroundThumbnailWorldSnapshot.reset();
        m_BackgroundRenderThumbnailActive = false;
    }

    void EditorApp::applySplashWindow(EditorContext& ctx)
    {
        if (m_SplashWindowApplied || !ctx.services)
            return;

        auto* windowService = ctx.services->tryGet<IWindowService>();
        if (!windowService)
            return;

        auto& window = windowService->window();
        resetWindowModeForShellState(*windowService);
        window.setTitle("VultraEngine")
            .setResizable(false)
            .setDecorated(false)
            .setExtent({640, 360})
            .centerOnScreen()
            .setVisible(true);
        m_SplashWindowApplied = true;
    }

    void EditorApp::applyEditorWindow(EditorContext& ctx)
    {
        if (m_EditorWindowApplied || !ctx.services)
            return;

        auto* windowService = ctx.services->tryGet<IWindowService>();
        if (!windowService)
            return;

        auto& window = windowService->window();
        resetWindowModeForShellState(*windowService);
        window.setTitle(kWindowTitle)
            .setDecorated(false)
            .setResizable(true)
            .setExtent({1280, 720})
            .centerOnScreen()
            .setVisible(true);
        m_EditorWindowApplied = true;
    }

    bool EditorApp::updateProjectLoading(EditorContext& ctx)
    {
        if (!ctx.services)
            return false;

        const auto projectRoot = ctx.state.currentProject.lexically_normal();
        if (projectRoot.empty())
        {
            m_SyncedProject.clear();
            m_SyncedProjectGeneration = std::numeric_limits<uint64_t>::max();
            m_Loading                 = {};
            m_PlayModeSnapshot.reset();
            m_PlayModeSceneDirtySnapshot = false;
            m_PlaybackWasPlaying         = false;
            return false;
        }

        const bool projectReloadRequested =
            projectRoot != m_SyncedProject || ctx.state.projectGeneration != m_SyncedProjectGeneration;
        if (projectReloadRequested && m_Loading.phase == LoadingPhase::Idle)
        {
            startProjectLoading(projectRoot);
        }

        if (m_Loading.phase == LoadingPhase::Idle)
            return false;

        if (projectRoot != m_Loading.projectRoot)
        {
            startProjectLoading(projectRoot);
        }

        switch (m_Loading.phase)
        {
            case LoadingPhase::Pending:
                releaseEditorStateForProjectLoad(ctx);
                applySplashWindow(ctx);
                m_Loading.phase    = LoadingPhase::ShowSplash;
                m_Loading.progress = 0.06f;
                m_Loading.message  = "Preparing project assets...";
                return true;

            case LoadingPhase::ShowSplash:
                startAssetImportTask(projectRoot, ctx.state.currentAssetRoot);
                m_Loading.phase    = LoadingPhase::ImportAssets;
                m_Loading.progress = 0.08f;
                m_Loading.message  = "Scanning project assets...";
                return true;

            case LoadingPhase::ImportAssets: {
                if (m_ImportProgress)
                {
                    std::scoped_lock lock(m_ImportProgress->mutex);
                    m_Loading.progress = m_ImportProgress->progress;
                    m_Loading.message  = m_ImportProgress->message;
                }

                if (!m_ImportTaskDone.load(std::memory_order_acquire))
                {
                    return true;
                }

                waitForAssetImportTask();
                auto importResult = std::move(m_ImportResult);
                if (!importResult.ok)
                {
                    ctx.state.statusMessage = "Project asset import failed: " + importResult.error;
                    m_Loading.message       = ctx.state.statusMessage;
                    m_Loading.progress      = 0.84f;
                    m_Loading.phase         = LoadingPhase::ConfigureAssets;
                    return true;
                }

                ctx.state.statusMessage = "Imported project assets: " + importResult.assetRoot;
                m_Loading.phase         = LoadingPhase::ConfigureAssets;
                m_Loading.progress      = 0.84f;
                m_Loading.message       = "Opening asset database...";
                return true;
            }

            case LoadingPhase::ConfigureAssets: {
                auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
                if (!assetService)
                {
                    m_Loading               = {};
                    ctx.state.statusMessage = "Project load failed: asset service unavailable.";
                    return false;
                }

                vultra::AssetSystemDesc desc;
                desc.assetRoot        = (projectRoot / ctx.state.currentAssetRoot).lexically_normal().generic_string();
                desc.importedFolder   = "imported";
                desc.registryFile     = "asset_registry.tsv";
                desc.scheme           = "res";
                desc.keepCpuCopy      = true;
                desc.enableImportScan = false;
                assetService->configure(desc);

                m_SyncedProject           = projectRoot;
                m_SyncedProjectGeneration = ctx.state.projectGeneration;
                m_PlayModeSnapshot.reset();
                m_PlayModeSceneDirtySnapshot = false;
                m_PlaybackWasPlaying         = false;
                ctx.state.statusMessage      = "Loaded project assets: " + desc.assetRoot;

                if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                {
                    for (const auto& uri : collectProjectRenderGraphUris(projectRoot, ctx.state.currentAssetRoot))
                        renderService->reloadRenderPipeline(uri, rendererKeyFromRenderGraphUri(uri));
                }

                m_ThumbnailService.prewarmProjectThumbnails(ctx);

                m_Loading.phase    = LoadingPhase::GenerateThumbnails;
                m_Loading.progress = 0.86f;
                m_Loading.message  = "Preparing asset thumbnails...";
                return true;
            }

            case LoadingPhase::GenerateThumbnails: {
                float       thumbnailProgress = 1.0f;
                std::string thumbnailMessage;
                if (m_ThumbnailService.processLoadingThumbnail(ctx, thumbnailProgress, thumbnailMessage))
                {
                    m_Loading.progress = 0.86f + thumbnailProgress * 0.08f;
                    m_Loading.message  = thumbnailMessage.empty() ? "Rendering asset thumbnails..." : thumbnailMessage;
                    return true;
                }

                if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                    worldService->world().clear();

                m_Loading.phase    = LoadingPhase::LoadScene;
                m_Loading.progress = 0.94f;
                m_Loading.message  = ctx.state.currentDefaultScene.empty() ?
                                         "Preparing editor windows..." :
                                         "Loading scene " + ctx.state.currentDefaultScene + "...";
                return true;
            }

            case LoadingPhase::LoadScene: {
                auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
                auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
                if (sceneService && worldService && !ctx.state.currentDefaultScene.empty())
                {
                    if (!m_Loading.sceneLoad)
                        m_Loading.sceneLoad = sceneService->loadSceneAsync(ctx.state.currentDefaultScene);

                    const auto status = sceneService->sceneLoadStatus(m_Loading.sceneLoad);
                    if (status.state == vultra::SceneLoadState::eLoading)
                    {
                        m_Loading.progress = 0.94f + std::clamp(status.progress, 0.0f, 1.0f) * 0.04f;
                        m_Loading.message  = status.message.empty() ? "Loading scene assets..." : status.message;
                        ctx.state.statusMessage = m_Loading.message;
                        return true;
                    }
                    if (status.state == vultra::SceneLoadState::eFailed)
                    {
                        ctx.state.statusMessage = "Failed to load default scene: " + ctx.state.currentDefaultScene;
                        sceneService->releaseSceneLoad(m_Loading.sceneLoad);
                        m_Loading.sceneLoad = {};
                        m_Loading.phase    = LoadingPhase::Finalize;
                        m_Loading.progress = 0.98f;
                        m_Loading.message  = ctx.state.statusMessage;
                        return true;
                    }

                    const auto root = sceneService->instantiateLoadedScene(
                        m_Loading.sceneLoad, worldService->world(), entt::null, true);
                    if (root != entt::null)
                    {
                        ctx.state.sceneDirty    = false;
                        ctx.state.statusMessage = "Loaded default scene: " + ctx.state.currentDefaultScene;
                    }
                    sceneService->releaseSceneLoad(m_Loading.sceneLoad);
                    m_Loading.sceneLoad = {};
                }
                m_History.reset(ctx, "Scene Loaded");

                m_Loading.phase    = LoadingPhase::Finalize;
                m_Loading.progress = 0.98f;
                m_Loading.message  = "Opening editor...";
                return true;
            }

            case LoadingPhase::Finalize:
                m_Loading.progress = 1.0f;
                m_Loading.message  = "Ready.";
                m_Loading.phase    = LoadingPhase::Complete;
                return true;

            case LoadingPhase::Complete:
                return false;

            case LoadingPhase::Idle:
                return false;
        }

        return false;
    }

    void EditorApp::drawLoadingOverlay(EditorContext& ctx) const
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
            return;

        ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
        ImVec2      size {viewport->WorkSize.x, viewport->WorkSize.y};
        if (ctx.services)
        {
            if (auto* windowService = ctx.services->tryGet<IWindowService>())
            {
                const auto extent = windowService->window().getExtent();
                size.x            = std::max(size.x, static_cast<float>(std::max(extent.x, 1)));
                size.y            = std::max(size.y, static_cast<float>(std::max(extent.y, 1)));
            }
        }

        const ImVec2 min = viewport->WorkPos;
        const ImVec2 max {min.x + size.x, min.y + size.y};
        const ImVec2 center {min.x + size.x * 0.5f, min.y + size.y * 0.5f};
        const float  progress = std::clamp(m_Loading.progress, 0.0f, 1.0f);

        namespace theme = vultra::imgui_theme;
        drawList->PushClipRectFullScreen();
        drawList->AddRectFilled(min, max, theme::u32(theme::background()));

        // A few translucent bands give the borderless splash depth without relying on any external texture.
        for (int i = 0; i < 10; ++i)
        {
            const float t = static_cast<float>(i) / 9.0f;
            const float y = min.y + size.y * t;
            drawList->AddRectFilled(
                ImVec2 {min.x, y},
                ImVec2 {max.x, y + size.y * 0.12f},
                theme::u32(theme::backgroundTransparent((18.0f / 255.0f) * (1.0f - std::abs(t - 0.5f)))));
        }

        const ImVec2 logoCenter {center.x, min.y + size.y * 0.36f};
        const float  logoRadius = 42.0f;
        for (int i = 5; i >= 1; --i)
        {
            drawList->AddCircle(logoCenter,
                                logoRadius + static_cast<float>(i * 4),
                                theme::u32(theme::accentTransparent(10.0f / 255.0f)),
                                96,
                                static_cast<float>(i));
        }
        drawList->AddCircle(logoCenter, logoRadius, theme::u32(theme::accentTransparent(210.0f / 255.0f)), 96, 2.0f);
        drawList->AddCircleFilled(
            logoCenter, logoRadius - 2.0f, theme::u32(theme::backgroundTransparent(210.0f / 255.0f)), 96);

        ImFont*      font         = ImGui::GetFont();
        const float  logoFontSize = 56.0f;
        const char*  logoText     = "V";
        const ImVec2 logoTextSize = font->CalcTextSizeA(logoFontSize, FLT_MAX, 0.0f, logoText);
        drawList->AddText(font,
                          logoFontSize,
                          ImVec2 {logoCenter.x - logoTextSize.x * 0.5f, logoCenter.y - logoTextSize.y * 0.52f},
                          theme::u32(theme::withAlpha(theme::text(), 245.0f / 255.0f)),
                          logoText);

        const char*  title         = progress >= 1.0f ? "OPENING EDITOR..." : "LOADING ASSETS...";
        const float  titleFontSize = 16.0f;
        const ImVec2 titleSize     = font->CalcTextSizeA(titleFontSize, FLT_MAX, 0.0f, title);
        const ImVec2 titlePos {center.x - titleSize.x * 0.5f, logoCenter.y + logoRadius + 30.0f};
        drawList->AddText(font, titleFontSize, titlePos, theme::u32(theme::textSoft()), title);

        const float  barWidth  = std::min(size.x * 0.54f, 340.0f);
        const float  barHeight = 8.0f;
        const ImVec2 barMin {center.x - barWidth * 0.5f, titlePos.y + 42.0f};
        const ImVec2 barMax {barMin.x + barWidth, barMin.y + barHeight};
        const float  rounding = barHeight * 0.5f;

        drawList->AddRectFilled(ImVec2 {barMin.x - 1.0f, barMin.y - 1.0f},
                                ImVec2 {barMax.x + 1.0f, barMax.y + 1.0f},
                                theme::u32(theme::withAlpha(theme::border(), 170.0f / 255.0f)),
                                rounding + 1.0f);
        drawList->AddRectFilled(barMin, barMax, theme::u32(theme::backgroundDeep()), rounding);

        const float  fillWidth = std::max(barHeight, barWidth * progress);
        const ImVec2 fillMax {barMin.x + fillWidth, barMax.y};
        drawList->AddRectFilled(ImVec2 {barMin.x - 8.0f, barMin.y - 6.0f},
                                ImVec2 {fillMax.x + 12.0f, barMax.y + 6.0f},
                                theme::u32(theme::accentTransparent(22.0f / 255.0f)),
                                10.0f);
        drawList->AddRectFilled(barMin, fillMax, theme::u32(theme::accentTransparent(230.0f / 255.0f)), rounding);

        char percentText[16] {};
        std::snprintf(percentText, sizeof(percentText), "%d%%", static_cast<int>(std::round(progress * 100.0f)));
        const float  percentFontSize = 18.0f;
        const ImVec2 percentSize     = font->CalcTextSizeA(percentFontSize, FLT_MAX, 0.0f, percentText);
        drawList->AddText(font,
                          percentFontSize,
                          ImVec2 {center.x - percentSize.x * 0.5f, barMax.y + 18.0f},
                          theme::u32(theme::withAlpha(theme::textMuted(), 245.0f / 255.0f)),
                          percentText);

        const std::string detail         = m_Loading.message.empty() ? std::string {"Preparing..."} : m_Loading.message;
        const float       detailFontSize = 13.0f;
        const ImVec2      detailSize     = font->CalcTextSizeA(detailFontSize, FLT_MAX, 0.0f, detail.c_str());
        drawList->AddText(font,
                          detailFontSize,
                          ImVec2 {center.x - detailSize.x * 0.5f, max.y - 32.0f},
                          theme::u32(theme::withAlpha(theme::textMuted(), 185.0f / 255.0f)),
                          detail.c_str());
        drawList->PopClipRect();
    }

    void EditorApp::shutdown(EditorContext& ctx)
    {
        m_RuntimeMcpServer.stop();
        waitForAssetImportTask();
        saveCurrentSceneThumbnail(ctx);

        if (ctx.services)
        {
            if (auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>())
                backendService->renderDevice().waitIdle();
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                renderService->resetSceneState();
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                worldService->world().clear();
        }

        m_WindowManager.destroy(ctx);
        m_ThumbnailService.clear(&ctx);
        m_FileWatcher.stop();
        m_Initialized        = false;
        m_DefaultLayoutBuilt = false;
        m_SyncedProject.clear();
        m_SyncedProjectGeneration = std::numeric_limits<uint64_t>::max();
        m_Loading                 = {};
        m_PlayModeSnapshot.reset();
        m_PlayModeSceneDirtySnapshot = false;
        m_PlaybackWasPlaying         = false;
        m_SplashWindowApplied        = false;
        m_EditorWindowApplied        = false;
        Selection::clear();
        ctx.state.sceneCamera.valid         = false;
        ctx.state.sceneViewVisible          = false;
        ctx.state.sceneViewVisibleLastFrame = false;
        ctx.state.gameViewVisible           = false;
        ctx.state.gameViewVisibleLastFrame  = false;
        ctx.state.gameViewRenderTargetAvailable = false;
        ctx.state.gameViewRenderTargetAvailableLastFrame = false;
        ctx.state.gameViewLastRenderTargetWidth  = 1280;
        ctx.state.gameViewLastRenderTargetHeight = 720;
        ctx.state.editorPlaying             = false;
        ctx.state.editorPaused              = false;
        ctx.state.editorShutdownRequested   = false;
        ctx.state.editorStepRequested       = false;
        ctx.state.editorGameTimeSeconds     = 0.0f;
        ctx.state.editorGameDeltaSeconds    = 0.0f;
    }

    void EditorApp::drawEditorTaskBar(EditorContext& ctx)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
            return;

        constexpr float barHeight = 26.0f;
        ImGuiWindowFlags flags    = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoDocking;

        if (!ImGui::BeginViewportSideBar("##VultraEditorTaskBar", viewport, ImGuiDir_Down, barHeight, flags))
            return;

        auto* renderService        = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
        auto* renderBackendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
        auto* assetService         = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
        auto* jobService           = ctx.services ? ctx.services->tryGet<vultra::IJobService>() : nullptr;
        auto* profiler             = renderService ? renderService->runtimeProfiler() : nullptr;

        const auto* frame = profiler ? profiler->selectedFrame() : nullptr;
        const float fps   = ImGui::GetIO().Framerate;
        const auto  renderMemoryStats =
            renderBackendService ? renderBackendService->renderDevice().getMemoryStats() :
                                   vultra::rhi::RenderDeviceMemoryStats {};
        const auto renderMemoryBudget =
            renderBackendService ? renderBackendService->renderDevice().getMemoryBudget() :
                                   vultra::rhi::RenderDeviceMemoryBudget {};
        const auto assetMemoryStats = assetService ? assetService->memoryStats() : vultra::AssetMemoryStats {};
        const auto jobSnapshots     = jobService ? jobService->snapshots() : std::vector<vultra::JobSnapshot> {};
        const auto systemMemory     = querySystemMemory();

        std::vector<std::string> labels;
        char                     fpsText[32] {};
        std::snprintf(fpsText, sizeof(fpsText), "FPS %.1f", fps);
        labels.emplace_back(fpsText);

        const uint64_t cpuCacheBytes =
            frame ? frame->assetCpuCacheBytes + frame->renderCpuCacheBytes :
                    assetMemoryStats.cpuCacheBytes + renderMemoryStats.cpuCacheBytes;
        const uint64_t gpuBytes = frame ? frame->gpuDeviceLocalBytes : renderMemoryStats.gpuDeviceLocalBytes;
        labels.emplace_back("CPU cache " + formatBytes(cpuCacheBytes));
        if (systemMemory.processResidentAvailable)
        {
            if (systemMemory.systemMemoryAvailable)
            {
                labels.emplace_back("RAM " + formatBytes(systemMemory.processResidentBytes) + " / avail " +
                                    formatBytes(systemMemory.systemAvailableBytes));
            }
            else
            {
                labels.emplace_back("RAM " + formatBytes(systemMemory.processResidentBytes));
            }
        }
        if (renderMemoryBudget.available && renderMemoryBudget.deviceLocalBudgetBytes > 0u)
        {
            labels.emplace_back("VRAM " + formatBytes(renderMemoryBudget.deviceLocalUsageBytes) + " / " +
                                formatBytes(renderMemoryBudget.deviceLocalBudgetBytes));
        }
        else
        {
            labels.emplace_back("VRAM " + formatBytes(gpuBytes));
        }

        const float separatorWidth = ImGui::CalcTextSize("|").x + 16.0f;
        float       totalWidth     = 0.0f;
        for (size_t i = 0; i < labels.size(); ++i)
        {
            totalWidth += ImGui::CalcTextSize(labels[i].c_str()).x;
            if (i + 1 < labels.size())
                totalWidth += separatorWidth;
        }

        ImGui::SetCursorPosY((barHeight - ImGui::GetTextLineHeight()) * 0.5f);
        if (!jobSnapshots.empty())
        {
            const auto& job = jobSnapshots.front();
            const float pulse =
                job.progress > 0.0f && job.progress < 1.0f ? job.progress : std::fmod(ImGui::GetTime() * 0.35f, 1.0f);
            ImGui::SetCursorPosX(8.0f);
            ImGui::TextDisabled("%s", job.label.empty() ? "Task" : job.label.c_str());
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::SetNextItemWidth(180.0f);
            ImGui::ProgressBar(pulse, ImVec2(180.0f, 8.0f), "");
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextDisabled("%s", job.message.empty() ? "Working..." : job.message.c_str());
        }
        else if (m_Loading.phase == LoadingPhase::LoadScene)
        {
            const float progress = std::clamp((m_Loading.progress - 0.94f) / 0.04f, 0.0f, 1.0f);
            ImGui::SetCursorPosX(8.0f);
            ImGui::TextDisabled("Scene Load");
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::SetNextItemWidth(180.0f);
            ImGui::ProgressBar(progress, ImVec2(180.0f, 8.0f), "");
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextDisabled("%s", m_Loading.message.empty() ? "Loading scene assets..." : m_Loading.message.c_str());
        }
        else if (!ctx.state.statusMessage.empty())
        {
            ImGui::SetCursorPosX(8.0f);
            ImGui::TextDisabled("%s", ctx.state.statusMessage.c_str());
        }

        ImGui::SetCursorPosY((barHeight - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::SetCursorPosX(std::max(8.0f, ImGui::GetWindowWidth() - totalWidth - 12.0f));
        for (size_t i = 0; i < labels.size(); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::TextDisabled("|");
                ImGui::SameLine(0.0f, 8.0f);
            }
            ImGui::TextDisabled("%s", labels[i].c_str());
        }

        ImGui::End();
    }

    void EditorApp::beginDockSpace()
    {
#ifdef IMGUI_HAS_DOCK
        static bool      dockSpaceOpen = true;
        ImGuiWindowFlags windowFlags   = ImGuiWindowFlags_NoDocking;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 {0.0f, 0.0f});

        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

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

        const ImGuiID  id          = dockSpaceId();
        ImGuiDockNode* dockNode    = ImGui::DockBuilderGetNode(id);
        const bool     missingNode = dockNode == nullptr;
        if (!m_DefaultLayoutBuilt || missingNode)
        {
            m_DefaultLayoutBuilt = true;

            ImGui::DockBuilderRemoveNode(id);
            ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);

            ImGuiID mainId   = id;
            ImGuiID leftId   = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 0.24f, nullptr, &mainId);
            ImGuiID rightId  = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 0.28f, nullptr, &mainId);
            ImGuiID historyId = ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Down, 0.34f, nullptr, &rightId);
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
            dockWindow("Material Graph", mainId);
            dockWindow("Animator Graph", mainId);
            dockWindow("Inspector", rightId);
            dockWindow("History", historyId);
            dockWindow("Content Browser", bottomId);
            dockWindow("Console", bottomId);
            dockWindow("Frame Debugger", bottomId);
            dockWindow("Profiler", bottomId);
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
