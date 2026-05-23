#include "editor_app/editor_app.hpp"

#include "editor_app/ui/editor_top_bar.hpp"
#include "editor_app/ui/windows/code_editor_window.hpp"
#include "editor_app/ui/windows/content_browser_window.hpp"
#include "editor_app/ui/windows/console_window.hpp"
#include "editor_app/ui/windows/frame_debugger_window.hpp"
#include "editor_app/ui/windows/game_view_window.hpp"
#include "editor_app/ui/windows/inspector_window.hpp"
#include "editor_app/ui/windows/profiler_window.hpp"
#include "editor_app/ui/windows/render_graph_window.hpp"
#include "editor_app/ui/windows/scene_hierarchy_window.hpp"
#include "editor_app/ui/windows/scene_view_window.hpp"
#include "editor_app/selection.hpp"
#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/imgui/imgui_theme.hpp>
#include <vultra/function/asset/asset_system.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#include <unistd.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#ifdef VULTRA_HAS_VASSET_IMPORT
#include <vasset/vasset_importers.hpp>
#include <vasset/vasset_registry.hpp>
#endif

namespace vultra_app
{
    namespace
    {
        ImGuiID dockSpaceId()
        {
            return ImHashStr("VultraDockSpace");
        }

        std::string quoteCommandArg(const std::string& text)
        {
            std::string out = "\"";
            for (const char ch : text)
            {
                if (ch == '"')
                    out += "\\\"";
                else
                    out += ch;
            }
            out += "\"";
            return out;
        }

        std::string quoteCommandArg(const std::filesystem::path& path)
        {
            return quoteCommandArg(path.generic_string());
        }

        std::filesystem::path currentExecutablePath()
        {
#if defined(_WIN32)
            std::wstring buffer(MAX_PATH, L'\0');
            DWORD size = 0;
            for (;;)
            {
                size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (size == 0)
                    return {};
                if (size < buffer.size() - 1)
                    break;
                buffer.resize(buffer.size() * 2);
            }
            return std::filesystem::path(std::wstring(buffer.data(), size));
#elif defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::vector<char> buffer(size + 1, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) != 0)
                return {};
            std::error_code ec;
            return std::filesystem::weakly_canonical(buffer.data(), ec);
#else
            std::vector<char> buffer(PATH_MAX, '\0');
            const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
            if (size <= 0)
                return {};
            buffer[static_cast<size_t>(size)] = '\0';
            return std::filesystem::path(buffer.data());
#endif
        }

        std::filesystem::path findRepoRoot()
        {
            namespace fs = std::filesystem;

            std::vector<fs::path> starts;
            std::error_code ec;
            starts.push_back(fs::current_path(ec));
            if (const auto exe = currentExecutablePath(); !exe.empty())
                starts.push_back(exe.parent_path());

            for (auto start : starts)
            {
                if (start.empty())
                    continue;

                start = start.lexically_normal();
                for (fs::path path = start; !path.empty(); path = path.parent_path())
                {
                    if (fs::exists(path / "scripts" / "pack.ps1", ec) && fs::exists(path / "source" / "xmake.lua", ec))
                        return path;
                    if (path == path.root_path())
                        break;
                }
            }
            return {};
        }

        int runCommand(const std::string& command)
        {
            return std::system(command.c_str());
        }

        void setBuildRunProgress(const std::shared_ptr<BuildRunTaskProgress>& progress,
                                 float                                         value,
                                 std::string                                   message)
        {
            if (!progress)
                return;
            std::scoped_lock lock(progress->mutex);
            progress->progress = std::clamp(value, 0.0f, 1.0f);
            progress->message  = std::move(message);
        }

        std::string makeRuntimeArguments(const std::filesystem::path& vpkPath)
        {
            std::ostringstream args;
            args << " --backend vulkan --vpk " << quoteCommandArg(vpkPath);
            return args.str();
        }

        std::optional<std::filesystem::path> findInstalledRuntime(const std::filesystem::path& repoRoot)
        {
            namespace fs = std::filesystem;

#if defined(_WIN32)
            constexpr const char* kExecutableName = "vultra.exe";
#else
            constexpr const char* kExecutableName = "vultra";
#endif

            const std::vector<fs::path> candidates {
                repoRoot / "build" / "install" / "vultra-app" / "bin" / kExecutableName,
                repoRoot / "build" / "install" / "bin" / kExecutableName,
            };

            std::error_code ec;
            for (const auto& candidate : candidates)
            {
                if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                    return candidate.lexically_normal();
            }
            return std::nullopt;
        }

        std::string installedRuntimeSearchHint(const std::filesystem::path& repoRoot)
        {
#if defined(_WIN32)
            constexpr const char* kExecutableName = "vultra.exe";
#else
            constexpr const char* kExecutableName = "vultra";
#endif
            std::ostringstream out;
            out << (repoRoot / "build" / "install" / "vultra-app" / "bin" / kExecutableName).generic_string()
                << "; "
                << (repoRoot / "build" / "install" / "bin" / kExecutableName).generic_string();
            return out.str();
        }

        uint32_t selectedEntityPickingId(EditorContext& ctx)
        {
            if (Selection::lastCategory() != SelectionCategory::Entity)
                return 0u;

            const auto selectedId = Selection::lastId();
            if (!selectedId.valid() || !ctx.services)
                return 0u;

            return vultra::makeEntityPickingId(selectedId);
        }

        BuildRunResult runPcVulkanBuildAndLaunch(const std::filesystem::path& projectRoot,
                                                 const std::string&           assetRoot,
                                                 const std::string&           projectName,
                                                 const std::string&           sceneUri,
                                                 const std::string&           renderPipeline,
                                                 std::shared_ptr<BuildRunTaskProgress> progress)
        {
            namespace fs = std::filesystem;

            setBuildRunProgress(progress, 0.08f, "Locating repository and asset root...");
            const fs::path repoRoot = findRepoRoot();
            if (repoRoot.empty())
                return {.ok = false, .message = "Build & Run failed: repository root was not found."};

            const fs::path assetRootPath = (projectRoot / assetRoot).lexically_normal();
            const fs::path vpkPath       = (projectRoot / "resources.vpk").lexically_normal();

            std::error_code ec;
            if (!fs::exists(assetRootPath, ec))
                return {.ok = false, .message = "Build & Run failed: missing asset root " + assetRootPath.generic_string()};

            setBuildRunProgress(progress, 0.15f, "Writing package manifest...");
            std::string manifestError;
            if (!saveVPackageManifest(assetRootPath,
                                      VPackageManifest {
                                          .name           = projectName,
                                          .entryScene     = sceneUri,
                                          .renderPipeline = renderPipeline,
                                      },
                                      &manifestError))
            {
                return {.ok = false, .message = "Build & Run failed: " + manifestError};
            }

#if defined(_WIN32)
            const fs::path packScript = repoRoot / "scripts" / "pack.ps1";
            std::ostringstream pack;
            pack << "powershell -ExecutionPolicy Bypass -File " << quoteCommandArg(packScript) << " "
                 << quoteCommandArg(repoRoot) << " " << quoteCommandArg(assetRootPath) << " " << quoteCommandArg(vpkPath);
#else
            const fs::path packScript = repoRoot / "scripts" / "pack.sh";
            std::ostringstream pack;
            pack << "sh " << quoteCommandArg(packScript) << " " << quoteCommandArg(repoRoot) << " "
                 << quoteCommandArg(assetRootPath) << " " << quoteCommandArg(vpkPath);
#endif

            setBuildRunProgress(progress, 0.25f, "Reimporting assets and packing VPK...");
            const int packResult = runCommand(pack.str());
            if (packResult != 0)
                return {.ok = false, .message = "Build & Run failed: asset package step returned " + std::to_string(packResult) + "."};

            setBuildRunProgress(progress, 0.86f, "Finding installed runtime...");
            const std::string runtimeArgs = makeRuntimeArguments(vpkPath);

            const auto runtimeExecutable = findInstalledRuntime(repoRoot);
            if (!runtimeExecutable.has_value())
            {
                return {.ok = false,
                        .message =
                            "Build & Run failed: installed vultra-app runtime was not found. Run "
                            "`xmake install -o build/install vultra-app` first. Searched: " +
                            installedRuntimeSearchHint(repoRoot)};
            }

#if defined(_WIN32)
            std::ostringstream launch;
            launch << "start \"\" " << quoteCommandArg(*runtimeExecutable) << runtimeArgs;
#else
            std::ostringstream launch;
            launch << quoteCommandArg(*runtimeExecutable) << runtimeArgs << " &";
#endif

            setBuildRunProgress(progress, 0.94f, "Launching installed runtime...");
            const int launchResult = runCommand(launch.str());
            if (launchResult != 0)
                return {.ok = false,
                        .message = "Build succeeded, but installed runtime launch returned " +
                                   std::to_string(launchResult) + "."};

            setBuildRunProgress(progress, 1.0f, "Build complete. Runtime launched.");
            return {.ok = true,
                    .message = "Build complete. Running installed runtime: " +
                               runtimeExecutable->generic_string()};
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

    void EditorApp::tick(EditorContext& ctx)
    {
        updateBuildAndRun(ctx);
        (void)updateProjectLoading(ctx);
    }

    bool EditorApp::isProjectLoading() const
    {
        return m_Loading.phase != LoadingPhase::Idle;
    }

    void EditorApp::draw(EditorContext& ctx)
    {
        if (isProjectLoading())
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
                                 Selection::clear(SelectionCategory::Entity);
                                 topBarCtx.state.sceneDirty = true;
                                 topBarCtx.state.statusMessage = "Created an empty scene workspace.";
                             },
                             .saveScene = [this](EditorContext& topBarCtx) { saveCurrentScene(topBarCtx); },
                             .buildAndRun = [this](EditorContext& topBarCtx) { startBuildAndRun(topBarCtx); },
                             .backToLauncher = [this](EditorContext& topBarCtx)
                             {
                                 topBarCtx.state.currentProject.clear();
                                 topBarCtx.state.currentProjectName.clear();
                                 topBarCtx.state.selectedSourceAsset.clear();
                                 topBarCtx.state.codeEditorPath.clear();
                                 topBarCtx.state.currentAssetRoot    = "resources";
                                 topBarCtx.state.currentDefaultScene = "res://scenes/test.vscn";
                                 topBarCtx.state.currentRenderPipeline = "res://render/default.vrg.json";
                                 ++topBarCtx.state.projectGeneration;
                                 topBarCtx.state.editorPlaying       = false;
                                 topBarCtx.state.editorPaused        = false;
                                 topBarCtx.state.editorStepRequested = false;
                                 topBarCtx.state.codeEditorOpenRequested = false;
                                 topBarCtx.state.editorShutdownRequested = true;
                                 topBarCtx.state.sceneDirty          = false;
                                 topBarCtx.state.mode                = AppMode::Launcher;
                                 topBarCtx.state.statusMessage       = "Returned to Project Launcher.";
                                 m_SyncedProject.clear();
                                 m_SyncedProjectGeneration = std::numeric_limits<uint64_t>::max();
                                 m_Loading = {};
                                 m_PlayModeSnapshot.reset();
                                 m_PlaybackWasPlaying = false;
                             },
                             .resetLayout = [this](EditorContext&) { resetDefaultDockLayout(); },
                             .showAbout   = [this](EditorContext&) { m_ShowAboutPopup = true; },
                         });
        if (ctx.state.mode != AppMode::Editor)
            return;

        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            saveCurrentScene(ctx);
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5))
            startBuildAndRun(ctx);

        beginDockSpace();
        buildDefaultDockLayout();
        m_WindowManager.draw(ctx);
        endDockSpace();

        if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
            renderService->builtinRenderSettings().selectionOutline.selectedEntityId = selectedEntityPickingId(ctx);

        syncPlaybackState(ctx);
        drawBuildRunPopup();
        drawEditorSettingsPopup(ctx);

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

    void EditorApp::drawEditorSettingsPopup(EditorContext& ctx)
    {
        if (ctx.state.editorSettingsOpen)
        {
            ImGui::OpenPopup("Editor Settings");
            ctx.state.editorSettingsOpen = false;
        }

        if (!ImGui::BeginPopupModal("Editor Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
        {
            auto& outline = renderService->builtinRenderSettings().selectionOutline;
            if (ImGui::CollapsingHeader("Render Settings", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("Selection outline", &outline.enabled);
                ImGui::ColorEdit3("Outline color", &outline.color.x);
                ImGui::SliderFloat("Outline thickness", &outline.thickness, 1.0f, 8.0f, "%.0f px");
                ImGui::SliderFloat("Fill opacity", &outline.fillOpacity, 0.0f, 0.25f, "%.2f");
                ImGui::SliderFloat("Edge opacity", &outline.edgeOpacity, 0.0f, 1.0f, "%.2f");
            }
        }
        else
        {
            ImGui::TextUnformatted("Render service is not available.");
        }

        ImGui::Spacing();
        if (ImGui::Button("Close"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
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

    void EditorApp::drawBuildRunPopup()
    {
        if (!m_BuildRunActive && !m_BuildRunCompleted.has_value())
            return;

        ImGui::OpenPopup("Build & Run");
        if (ImGui::BeginPopupModal("Build & Run", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            float       progress = 0.0f;
            std::string message  = "Preparing...";
            if (m_BuildRunCompleted.has_value())
            {
                progress = m_BuildRunCompleted->ok ? 1.0f : 0.0f;
                message  = m_BuildRunCompleted->message;
            }
            else if (m_BuildRunProgress)
            {
                std::scoped_lock lock(m_BuildRunProgress->mutex);
                progress = m_BuildRunProgress->progress;
                message  = m_BuildRunProgress->message.empty() ? message : m_BuildRunProgress->message;
            }

            ImGui::TextUnformatted("Build & Run");
            ImGui::Spacing();
            ImGui::ProgressBar(progress, ImVec2 {360.0f, 0.0f});
            ImGui::Spacing();
            if (m_BuildRunCompleted.has_value() && !m_BuildRunCompleted->ok)
                ImGui::TextColored(ImVec4 {1.0f, 0.32f, 0.28f, 1.0f}, "%s", message.c_str());
            else
                ImGui::TextWrapped("%s", message.c_str());
            if (m_BuildRunActive)
                ImGui::TextDisabled("This can take a while when assets are reimported.");
            else if (ImGui::Button("Close", ImVec2 {96.0f, 0.0f}))
            {
                m_BuildRunCompleted.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void EditorApp::updateBuildAndRun(EditorContext& ctx)
    {
        if (!m_BuildRunActive || !m_BuildRunFuture.valid())
            return;

        if (m_BuildRunFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        const auto result = m_BuildRunFuture.get();
        m_BuildRunActive = false;
        m_BuildRunProgress.reset();
        m_BuildRunCompleted = result;
        ctx.state.statusMessage = result.message;
    }

    void EditorApp::startBuildAndRun(EditorContext& ctx)
    {
        if (m_BuildRunActive)
        {
            ctx.state.statusMessage = "Build & Run is already running.";
            return;
        }
        m_BuildRunCompleted.reset();
        if (ctx.state.currentProject.empty())
        {
            ctx.state.statusMessage = "Build & Run failed: no project is loaded.";
            return;
        }
        if (ctx.state.currentDefaultScene.empty())
        {
            ctx.state.statusMessage = "Build & Run failed: no default scene is selected.";
            return;
        }
        if (ctx.state.editorPlaying)
        {
            ctx.state.statusMessage = "Stop Play Mode before Build & Run.";
            return;
        }

        const bool sceneWasDirty = ctx.state.sceneDirty;
        saveCurrentScene(ctx);
        if (sceneWasDirty && ctx.state.sceneDirty)
        {
            ctx.state.statusMessage = "Build & Run stopped: save the current scene first.";
            return;
        }

        const auto projectRoot = ctx.state.currentProject.lexically_normal();
        const auto assetRoot   = ctx.state.currentAssetRoot;
        const auto projectName = ctx.state.currentProjectName;
        const auto sceneUri    = ctx.state.currentDefaultScene;
        const auto renderPipeline = ctx.state.currentRenderPipeline;

        ctx.state.statusMessage = "Build & Run started: packaging runtime...";
        m_BuildRunProgress = std::make_shared<BuildRunTaskProgress>();
        {
            std::scoped_lock lock(m_BuildRunProgress->mutex);
            m_BuildRunProgress->progress = 0.02f;
            m_BuildRunProgress->message  = "Saving scene and preparing package...";
        }
        m_BuildRunActive = true;
        auto progress = m_BuildRunProgress;
        m_BuildRunFuture = std::async(std::launch::async,
                                      [projectRoot, assetRoot, projectName, sceneUri, renderPipeline, progress]()
                                      {
                                          return runPcVulkanBuildAndLaunch(
                                              projectRoot, assetRoot, projectName, sceneUri, renderPipeline, progress);
                                      });
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
        m_WindowManager.addWindow<FrameDebuggerWindow>();
        m_WindowManager.addWindow<ProfilerWindow>();
        m_WindowManager.addWindow<InspectorWindow>();
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

    void EditorApp::startAssetImportTask(const std::filesystem::path& projectRoot, const std::string& assetRoot)
    {
        auto progress     = std::make_shared<ImportTaskProgress>();
        progress->message = "Scanning project assets...";
        progress->progress = 0.08f;
        m_ImportProgress  = progress;

        const auto rootPath       = projectRoot.lexically_normal();
        const auto assetRootPath  = (rootPath / assetRoot).lexically_normal();
        const auto importedFolder = std::string {"imported"};
        const auto registryFile   = std::string {"asset_registry.tsv"};

        if (!m_ImportScheduler)
            m_ImportScheduler = std::make_unique<vtask::Scheduler>();

        m_ImportResult   = {};
        m_ImportTaskDone = false;
        m_ImportTask     = std::make_unique<vtask::TaskSet>(
            1,
            1,
            [this, progress, assetRootPath, importedFolder, registryFile](vtask::Range) {
                                      ImportTaskResult result;
                                      result.assetRoot = assetRootPath.generic_string();
                                      result.registryPath =
                                          (assetRootPath / importedFolder / registryFile).generic_string();

#ifdef VULTRA_HAS_VASSET_IMPORT
                                      vasset::VAssetRegistry registry;
                                      registry.setAssetRootPath(result.assetRoot);
                                      registry.setImportedFolderName(importedFolder);

                                      if (std::filesystem::exists(result.registryPath))
                                          registry.load(result.registryPath);

                                      vasset::VAssetImporter importer {registry};
                                      vasset::VAssetImporter::ImportOptions options;
                                      options.progress = [progress](const vasset::VAssetImporter::ImportProgress& p) {
                                          std::scoped_lock lock(progress->mutex);
                                          switch (p.phase)
                                          {
                                              case vasset::VAssetImporter::ImportProgress::Phase::eScan:
                                                  progress->progress = 0.08f;
                                                  progress->message  = p.currentPath.empty()
                                                                           ? "Scanning project assets..."
                                                                           : "Scanning " + p.currentPath;
                                                  break;
                                              case vasset::VAssetImporter::ImportProgress::Phase::eImport: {
                                                  const float amount =
                                                      p.totalFiles > 0 ?
                                                          static_cast<float>(p.processedFiles) /
                                                              static_cast<float>(p.totalFiles) :
                                                          1.0f;
                                                  progress->progress = 0.12f + amount * 0.68f;
                                                  progress->message  = p.currentPath.empty()
                                                                           ? "Importing project assets..."
                                                                           : "Importing " + p.currentPath;
                                                  break;
                                              }
                                              case vasset::VAssetImporter::ImportProgress::Phase::eDone:
                                                  progress->progress = 0.82f;
                                                  progress->message  = "Finalizing asset database...";
                                                  break;
                                          }
                                      };
                                      importer.setOptions(options);

                                      auto importResult = importer.importOrReimportAssetFolder(result.assetRoot, false);
                                      if (!importResult)
                                      {
                                          result.ok    = false;
                                          result.error = "asset import failed";
                                      }
                                      else if (!registry.save(result.registryPath))
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

    void EditorApp::applySplashWindow(EditorContext& ctx)
    {
        if (m_SplashWindowApplied || !ctx.services)
            return;

        auto* windowService = ctx.services->tryGet<IWindowService>();
        if (!windowService)
            return;

        auto& window = windowService->window();
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
            m_Loading = {};
            m_PlayModeSnapshot.reset();
            m_PlaybackWasPlaying = false;
            return false;
        }

        const bool projectReloadRequested =
            projectRoot != m_SyncedProject || ctx.state.projectGeneration != m_SyncedProjectGeneration;
        if (projectReloadRequested && m_Loading.phase == LoadingPhase::Idle)
        {
            startProjectLoading(projectRoot);
            applySplashWindow(ctx);
            return true;
        }

        if (m_Loading.phase == LoadingPhase::Idle)
            return false;

        if (projectRoot != m_Loading.projectRoot)
        {
            startProjectLoading(projectRoot);
            applySplashWindow(ctx);
            return true;
        }

        switch (m_Loading.phase)
        {
            case LoadingPhase::Pending:
                applySplashWindow(ctx);
                if (!m_Loading.releasedEditorState)
                {
                    if (auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>())
                        backendService->renderDevice().waitIdle();
                    m_WindowManager.destroy(ctx);
                    m_Initialized        = false;
                    m_DefaultLayoutBuilt = false;
                    Selection::clear();
                    if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                        worldService->world().clear();
                    m_PlayModeSnapshot.reset();
                    m_PlaybackWasPlaying = false;
                    ctx.state.editorPlaying = false;
                    ctx.state.editorPaused = false;
                    ctx.state.editorStepRequested = false;
                    m_Loading.releasedEditorState = true;
                }
                startAssetImportTask(projectRoot, ctx.state.currentAssetRoot);
                m_Loading.phase    = LoadingPhase::ImportAssets;
                m_Loading.progress = 0.08f;
                m_Loading.message  = "Scanning project assets...";
                return true;

            case LoadingPhase::ImportAssets:
            {
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
                desc.enableImportScan = false;
                assetService->configure(desc);

                m_SyncedProject         = projectRoot;
                m_SyncedProjectGeneration = ctx.state.projectGeneration;
                m_PlayModeSnapshot.reset();
                m_PlaybackWasPlaying    = false;
                ctx.state.statusMessage = "Loaded project assets: " + desc.assetRoot;

                if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                    renderService->reloadRenderPipeline(ctx.state.currentRenderPipeline, "project");

                m_Loading.phase    = LoadingPhase::LoadScene;
                m_Loading.progress = 0.90f;
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
                m_Loading.phase    = LoadingPhase::Complete;
                return true;

            case LoadingPhase::Complete:
                applyEditorWindow(ctx);
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
        const ImVec2 size {viewport->WorkSize.x, viewport->WorkSize.y};
        const ImVec2 center {min.x + size.x * 0.5f, min.y + size.y * 0.5f};
        const float  progress = std::clamp(m_Loading.progress, 0.0f, 1.0f);

        namespace theme = vultra::imgui_theme;
        drawList->AddRectFilled(min, max, theme::u32(theme::background()));

        // A few translucent bands give the borderless splash depth without relying on any external texture.
        for (int i = 0; i < 10; ++i)
        {
            const float t = static_cast<float>(i) / 9.0f;
            const float y = min.y + size.y * t;
            drawList->AddRectFilled(ImVec2 {min.x, y},
                                    ImVec2 {max.x, y + size.y * 0.12f},
                                    theme::u32(theme::backgroundTransparent(
                                        (18.0f / 255.0f) * (1.0f - std::abs(t - 0.5f)))));
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
        drawList->AddCircleFilled(logoCenter,
                                  logoRadius - 2.0f,
                                  theme::u32(theme::backgroundTransparent(210.0f / 255.0f)),
                                  96);

        ImFont* font = ImGui::GetFont();
        const float logoFontSize = 56.0f;
        const char* logoText = "V";
        const ImVec2 logoTextSize = font->CalcTextSizeA(logoFontSize, FLT_MAX, 0.0f, logoText);
        drawList->AddText(font,
                          logoFontSize,
                          ImVec2 {logoCenter.x - logoTextSize.x * 0.5f, logoCenter.y - logoTextSize.y * 0.52f},
                          theme::u32(theme::withAlpha(theme::text(), 245.0f / 255.0f)),
                          logoText);

        const char* title = progress >= 1.0f ? "OPENING EDITOR..." : "LOADING ASSETS...";
        const float titleFontSize = 16.0f;
        const ImVec2 titleSize = font->CalcTextSizeA(titleFontSize, FLT_MAX, 0.0f, title);
        const ImVec2 titlePos {center.x - titleSize.x * 0.5f, logoCenter.y + logoRadius + 30.0f};
        drawList->AddText(font, titleFontSize, titlePos, theme::u32(theme::textSoft()), title);

        const float barWidth  = std::min(size.x * 0.54f, 340.0f);
        const float barHeight = 8.0f;
        const ImVec2 barMin {center.x - barWidth * 0.5f, titlePos.y + 42.0f};
        const ImVec2 barMax {barMin.x + barWidth, barMin.y + barHeight};
        const float  rounding = barHeight * 0.5f;

        drawList->AddRectFilled(ImVec2 {barMin.x - 1.0f, barMin.y - 1.0f},
                                ImVec2 {barMax.x + 1.0f, barMax.y + 1.0f},
                                theme::u32(theme::withAlpha(theme::border(), 170.0f / 255.0f)),
                                rounding + 1.0f);
        drawList->AddRectFilled(barMin, barMax, theme::u32(theme::backgroundDeep()), rounding);

        const float fillWidth = std::max(barHeight, barWidth * progress);
        const ImVec2 fillMax {barMin.x + fillWidth, barMax.y};
        drawList->AddRectFilled(ImVec2 {barMin.x - 8.0f, barMin.y - 6.0f},
                                ImVec2 {fillMax.x + 12.0f, barMax.y + 6.0f},
                                theme::u32(theme::accentTransparent(22.0f / 255.0f)),
                                10.0f);
        drawList->AddRectFilled(barMin, fillMax, theme::u32(theme::accentTransparent(230.0f / 255.0f)), rounding);

        char percentText[16] {};
        std::snprintf(percentText, sizeof(percentText), "%d%%", static_cast<int>(std::round(progress * 100.0f)));
        const float percentFontSize = 18.0f;
        const ImVec2 percentSize = font->CalcTextSizeA(percentFontSize, FLT_MAX, 0.0f, percentText);
        drawList->AddText(font,
                          percentFontSize,
                          ImVec2 {center.x - percentSize.x * 0.5f, barMax.y + 18.0f},
                          theme::u32(theme::withAlpha(theme::textMuted(), 245.0f / 255.0f)),
                          percentText);

        const std::string detail = m_Loading.message.empty() ? std::string {"Preparing..."} : m_Loading.message;
        const float detailFontSize = 13.0f;
        const ImVec2 detailSize = font->CalcTextSizeA(detailFontSize, FLT_MAX, 0.0f, detail.c_str());
        drawList->AddText(font,
                          detailFontSize,
                          ImVec2 {center.x - detailSize.x * 0.5f, max.y - 32.0f},
                          theme::u32(theme::withAlpha(theme::textMuted(), 185.0f / 255.0f)),
                          detail.c_str());
    }

    void EditorApp::shutdown(EditorContext& ctx)
    {
        waitForAssetImportTask();

        if (ctx.services)
        {
            if (auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>())
                backendService->renderDevice().waitIdle();
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                worldService->world().clear();
        }

        m_WindowManager.destroy(ctx);
        m_Initialized        = false;
        m_DefaultLayoutBuilt = false;
        m_SyncedProject.clear();
        m_SyncedProjectGeneration = std::numeric_limits<uint64_t>::max();
        m_Loading = {};
        m_PlayModeSnapshot.reset();
        m_PlaybackWasPlaying = false;
        m_SplashWindowApplied = false;
        m_EditorWindowApplied = false;
        Selection::clear();
        ctx.state.sceneCamera.valid = false;
        ctx.state.gameViewVisible = false;
        ctx.state.gameViewVisibleLastFrame = false;
        ctx.state.editorPlaying = false;
        ctx.state.editorPaused = false;
        ctx.state.editorShutdownRequested = false;
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
