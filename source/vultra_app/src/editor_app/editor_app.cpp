#include "editor_app/editor_app.hpp"

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
#include "editor_app/ui/windows/material_graph_window.hpp"
#include "editor_app/ui/windows/profiler_window.hpp"
#include "editor_app/ui/windows/render_graph_window.hpp"
#include "editor_app/ui/windows/scene_hierarchy_window.hpp"
#include "editor_app/ui/windows/scene_view_window.hpp"
#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/asset/asset_system.hpp>
#include <vultra/function/imgui/imgui_theme.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/job_service.hpp>
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

        bool targetNeedsExecutableExtension(const std::string& targetPlatform) { return targetPlatform == "Windows"; }

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
            DWORD        size = 0;
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
            const ssize_t     size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
            if (size <= 0)
                return {};
            buffer[static_cast<size_t>(size)] = '\0';
            return std::filesystem::path(buffer.data());
#endif
        }

        std::filesystem::path currentExecutableDir()
        {
            const auto exe = currentExecutablePath();
            return exe.empty() ? std::filesystem::path {} : exe.parent_path();
        }

        bool hasAssimpRuntimeDll(const std::filesystem::path& dir)
        {
            if (dir.empty())
                return false;

            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec || !entry.is_regular_file(ec))
                    continue;

                auto filename = entry.path().filename().generic_string();
                std::transform(filename.begin(), filename.end(), filename.begin(), [](const unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (filename.find("assimp") != std::string::npos && filename.ends_with(".dll"))
                    return true;
            }
            return false;
        }

        std::optional<std::filesystem::path> findPublishedRuntimeNextToEditor()
        {
            namespace fs = std::filesystem;

            const auto exeDir = currentExecutableDir();
            if (!hasAssimpRuntimeDll(exeDir))
                return std::nullopt;

#if defined(_WIN32)
            const auto runtime = exeDir / "vultra.exe";
#else
            const auto runtime = exeDir / "vultra";
#endif

            std::error_code ec;
            if (fs::exists(runtime, ec) && fs::is_regular_file(runtime, ec))
                return runtime.lexically_normal();
            return std::nullopt;
        }

        std::filesystem::path findRepoRoot()
        {
            namespace fs = std::filesystem;

            std::vector<fs::path> starts;
            std::error_code       ec;
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
                    if (fs::exists(path / "source" / "xmake.lua", ec) && fs::exists(path / "external" / "vasset", ec))
                        return path;
                    if (path == path.root_path())
                        break;
                }
            }
            return {};
        }

        int runCommand(const std::string& command) { return std::system(command.c_str()); }

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

#ifdef VULTRA_HAS_VASSET_IMPORT
        int runAssetTool(std::vector<std::string> args)
        {
            std::vector<char*> argv;
            argv.reserve(args.size());
            for (auto& arg : args)
                argv.push_back(arg.data());
            return vasset::tool::run_vasset_cli(
                static_cast<int>(argv.size()), argv.data(), makeEditorAssetImportOptions());
        }
#endif

        void
        setBuildRunProgress(const std::shared_ptr<BuildRunTaskProgress>& progress, float value, std::string message)
        {
            if (!progress)
                return;
            std::scoped_lock lock(progress->mutex);
            progress->progress = std::clamp(value, 0.0f, 1.0f);
            progress->message  = std::move(message);
        }

        std::string sanitizedPackageName(std::string name, const std::filesystem::path& projectRoot)
        {
            if (name.empty())
                name = projectRoot.filename().generic_string();
            if (name.empty())
                name = "VultraApp";

            for (auto& ch : name)
            {
                const auto uch = static_cast<unsigned char>(ch);
                if (uch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' ||
                    ch == '|' || ch == '?' || ch == '*')
                    ch = '_';
            }

            while (!name.empty() && (name.back() == '.' || name.back() == ' '))
                name.pop_back();
            while (!name.empty() && name.front() == ' ')
                name.erase(name.begin());
            return name.empty() ? std::string {"VultraApp"} : name;
        }

        bool samePath(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
        {
            std::error_code ec;
            const auto      lhsCanonical = std::filesystem::weakly_canonical(lhs, ec);
            if (ec)
                return lhs.lexically_normal() == rhs.lexically_normal();
            const auto rhsCanonical = std::filesystem::weakly_canonical(rhs, ec);
            if (ec)
                return lhs.lexically_normal() == rhs.lexically_normal();
            return lhsCanonical == rhsCanonical;
        }

        bool copyRuntimeToPackage(const std::filesystem::path& runtimeExecutable,
                                  const std::filesystem::path& packageExecutable,
                                  std::string&                 errorMessage)
        {
            namespace fs = std::filesystem;

            std::error_code ec;
            fs::create_directories(packageExecutable.parent_path(), ec);
            if (ec)
            {
                errorMessage = "failed to create output folder: " + ec.message();
                return false;
            }

            if (!samePath(runtimeExecutable, packageExecutable))
            {
                fs::copy_file(runtimeExecutable, packageExecutable, fs::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    errorMessage = "failed to copy runtime executable: " + ec.message();
                    return false;
                }
            }

            return true;
        }

        int launchPackagedRuntime(const std::filesystem::path& packageExecutable,
                                  const std::filesystem::path& packageVpk,
                                  const std::string&           sceneUri)
        {
            const auto workingDir = packageExecutable.parent_path();
#if defined(_WIN32)
            std::ostringstream launch;
            launch << "start \"\" /D " << quoteCommandArg(workingDir) << " "
                   << quoteCommandArg(packageExecutable.filename()) << " --vpk "
                   << quoteCommandArg(packageVpk.filename()) << " --scene " << quoteCommandArg(sceneUri);
#else
            std::ostringstream launch;
            launch << "cd " << quoteCommandArg(workingDir) << " && "
                   << quoteCommandArg(std::string {"./"} + packageExecutable.filename().generic_string()) << " --vpk "
                   << quoteCommandArg(std::string {"./"} + packageVpk.filename().generic_string()) << " --scene "
                   << quoteCommandArg(sceneUri) << " &";
#endif
            return runCommand(launch.str());
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

        BuildRunResult runPcVulkanBuildAndLaunch(const std::filesystem::path&          projectRoot,
                                                 const std::string&                    assetRoot,
                                                 const std::string&                    projectName,
                                                 const std::string&                    sceneUri,
                                                 const std::filesystem::path&          outputFolder,
                                                 const std::string&                    targetPlatform,
                                                 const std::string&                    exportTemplatePath,
                                                 const bool                            launchRuntime,
                                                 std::shared_ptr<BuildRunTaskProgress> progress)
        {
            namespace fs = std::filesystem;

            const fs::path assetRootPath = (projectRoot / assetRoot).lexically_normal();
            const auto     packageName   = sanitizedPackageName(projectName, projectRoot);
            const fs::path outputDir     = outputFolder.lexically_normal();
            const fs::path packageExecutable =
                outputDir / (targetNeedsExecutableExtension(targetPlatform) ? packageName + ".exe" : packageName);
            const fs::path vpkPath = outputDir / (packageName + ".vpk");

            std::error_code ec;
            if (!fs::exists(assetRootPath, ec))
                return {.ok = false, .message = "Export failed: missing asset root " + assetRootPath.generic_string()};
            fs::create_directories(outputDir, ec);
            if (ec)
                return {.ok      = false,
                        .message = "Export failed: cannot create output folder " + outputDir.generic_string()};

            setBuildRunProgress(progress, 0.15f, "Writing package manifest...");
            std::string manifestError;
            if (!saveVPackageManifest(assetRootPath,
                                      VPackageManifest {
                                          .name       = projectName,
                                          .entryScene = sceneUri,
                                      },
                                      &manifestError))
            {
                return {.ok = false, .message = "Export failed: " + manifestError};
            }

            auto packCurrentAssets = [&]() -> std::optional<BuildRunResult> {
                setBuildRunProgress(progress, 0.25f, "Reimporting assets and packing VPK...");
#ifdef VULTRA_HAS_VASSET_IMPORT
                const int importResult = runAssetTool({"vultra asset", "import", assetRootPath.generic_string()});
                if (importResult != 0)
                    return BuildRunResult {
                        .ok      = false,
                        .message = "Export failed: asset import step returned " + std::to_string(importResult) + ".",
                    };

                const int packResult = runAssetTool(
                    {"vultra asset", "pack", assetRootPath.generic_string(), vpkPath.generic_string(), "--zstd", "6"});
                if (packResult != 0)
                    return BuildRunResult {
                        .ok      = false,
                        .message = "Export failed: asset package step returned " + std::to_string(packResult) + ".",
                    };
                return std::nullopt;
#else
                return BuildRunResult {
                    .ok      = false,
                    .message = "Export failed: vasset import support is not available in this build.",
                };
#endif
            };

            if (auto publishedRuntime = findPublishedRuntimeNextToEditor(); publishedRuntime.has_value())
            {
                if (auto packError = packCurrentAssets(); packError.has_value())
                    return *packError;

                setBuildRunProgress(progress, 0.55f, "Copying runtime executable...");

                std::string copyError;
                if (!copyRuntimeToPackage(*publishedRuntime, packageExecutable, copyError))
                    return {.ok = false, .message = "Export failed: " + copyError};

                if (launchRuntime)
                {
                    setBuildRunProgress(progress, 0.94f, "Launching published runtime...");
                    const int launchResult = launchPackagedRuntime(packageExecutable, vpkPath, sceneUri);
                    if (launchResult != 0)
                        return {.ok      = false,
                                .message = "Export failed: published runtime launch returned " +
                                           std::to_string(launchResult) + "."};
                }

                setBuildRunProgress(progress, 1.0f, launchRuntime ? "Runtime launched." : "Export complete.");
                return {.ok      = true,
                        .message = launchRuntime ? "Running package: " + packageExecutable.generic_string() :
                                                   "Export complete: " + packageExecutable.generic_string()};
            }

            if (auto packError = packCurrentAssets(); packError.has_value())
                return *packError;

            if (launchRuntime && targetPlatform != currentHostPlatform())
            {
                return {.ok      = false,
                        .message = "Export & Run requires the target platform to match the host platform."};
            }

            setBuildRunProgress(progress, 0.90f, "Copying export template...");
            fs::path runtimeExecutable;
            if (!exportTemplatePath.empty())
                runtimeExecutable = fs::path {exportTemplatePath}.lexically_normal();
            else if (targetPlatform == currentHostPlatform())
                runtimeExecutable = currentExecutablePath();
            else
            {
                return {.ok      = false,
                        .message = "Export failed: target platform requires an export template executable."};
            }

            if (runtimeExecutable.empty() || !fs::exists(runtimeExecutable, ec) ||
                !fs::is_regular_file(runtimeExecutable, ec))
            {
                return {.ok = false, .message = "Export failed: export template executable was not found."};
            }

            std::string copyError;
            if (!copyRuntimeToPackage(runtimeExecutable, packageExecutable, copyError))
                return {.ok = false, .message = "Export failed: " + copyError};

            if (launchRuntime)
            {
                setBuildRunProgress(progress, 0.94f, "Launching runtime...");
                const int launchResult = launchPackagedRuntime(packageExecutable, vpkPath, sceneUri);
                if (launchResult != 0)
                    return {.ok = false,
                            .message =
                                "Export succeeded, but runtime launch returned " + std::to_string(launchResult) + "."};
            }

            setBuildRunProgress(
                progress, 1.0f, launchRuntime ? "Export complete. Runtime launched." : "Export complete.");
            return {.ok      = true,
                    .message = launchRuntime ?
                                   "Export complete. Running package: " + packageExecutable.generic_string() :
                                   "Export complete: " + packageExecutable.generic_string()};
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
            engine.ctx().config.render.renderPipelineAsset = project->editingRenderGraph;
            engine.ctx().config.render.renderPipelineRendererKey.clear();
            return;
        }

        engine.ctx().config.asset.loadFromVPK = false;
        engine.ctx().config.asset.assetRoot   = (projectPath / "resources").lexically_normal().generic_string();
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
                                 [](EditorContext& topBarCtx) {
                                     auto* worldService = topBarCtx.services ?
                                                              topBarCtx.services->tryGet<vultra::IWorldService>() :
                                                              nullptr;
                                     if (!worldService)
                                     {
                                         topBarCtx.state.statusMessage = "New scene failed: world service unavailable.";
                                         return;
                                     }

                                     auto& world = worldService->world();
                                     world.clear();
                                     Selection::clear(SelectionCategory::Entity);
                                     topBarCtx.state.sceneDirty    = true;
                                     topBarCtx.state.statusMessage = "Created an empty scene workspace.";
                                     if (topBarCtx.history)
                                         topBarCtx.history->reset(topBarCtx, "New Empty Scene");
                                 },
                             .saveScene   = [this](EditorContext& topBarCtx) { saveCurrentScene(topBarCtx); },
                             .buildAndRun = [this](EditorContext& topBarCtx) { startBuildAndRun(topBarCtx); },
                             .backToLauncher =
                                 [this](EditorContext& topBarCtx) {
                                     saveCurrentSceneThumbnail(topBarCtx);
                                     topBarCtx.state.currentProject.clear();
                                     topBarCtx.state.currentProjectName.clear();
                                     topBarCtx.state.selectedSourceAsset.clear();
                                     topBarCtx.state.codeEditorPath.clear();
                                     topBarCtx.state.pendingEditorCommands.clear();
                                     topBarCtx.state.currentAssetRoot          = "resources";
                                     topBarCtx.state.currentDefaultScene.clear();
                                     topBarCtx.state.currentEditingRenderGraph = "res://render/default.vrg.json";
                                     topBarCtx.state.currentEditingMaterialGraph = "res://materials/default.vmatgraph.json";
                                     ++topBarCtx.state.projectGeneration;
                                     topBarCtx.state.editorPlaying           = false;
                                     topBarCtx.state.editorPaused            = false;
                                     topBarCtx.state.editorStepRequested     = false;
                                     topBarCtx.state.codeEditorOpenRequested = false;
                                     topBarCtx.state.runtimeFrameGraphViewerOpenRequested = false;
                                     topBarCtx.state.materialGraphOpenRequested = false;
                                     topBarCtx.state.editorShutdownRequested = true;
                                     topBarCtx.state.sceneDirty              = false;
                                     topBarCtx.state.mode                    = AppMode::Launcher;
                                     topBarCtx.state.statusMessage           = "Returned to Project Launcher.";
                                     m_SyncedProject.clear();
                                     m_SyncedProjectGeneration = std::numeric_limits<uint64_t>::max();
                                     m_Loading                 = {};
                                     m_PlayModeSnapshot.reset();
                                     m_PlayModeSceneDirtySnapshot = false;
                                     m_PlaybackWasPlaying         = false;
                                     m_History.clear();
                                 },
                             .resetLayout = [this](EditorContext&) { resetDefaultDockLayout(); },
                             .showAbout   = [this](EditorContext&) { m_ShowAboutPopup = true; },
                             });
        }
        if (ctx.state.mode != AppMode::Editor)
            return;

        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            saveCurrentScene(ctx);
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
            m_History.redo(ctx);
        else if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
            m_History.undo(ctx);
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5))
            startBuildAndRun(ctx);

        if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
            renderService->setFrameGraphTextureCaptureEnabled(false);

        {
            vultra::RuntimeProfiler::ExternalScope scope {"EditorApp::taskBar"};
            drawEditorTaskBar(ctx);
        }
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
                    if (command.payload.empty())
                        break;
                    if (ctx.state.sceneDirty)
                    {
                        m_PendingOpenSceneUri   = command.payload;
                        m_OpenSceneConfirmPopup = true;
                        ctx.state.statusMessage = "Open scene pending confirmation: " + command.payload;
                    }
                    else
                    {
                        (void)openSceneFromCommand(ctx, command.payload);
                    }
                    break;
                case AppState::EditorCommandType::OpenMaterialGraph:
                    if (command.payload.empty())
                        break;
                    ctx.state.currentEditingMaterialGraph = command.payload;
                    ctx.state.materialGraphOpenRequested  = true;
                    ctx.state.statusMessage               = "Opening material graph: " + command.payload;
                    break;
                case AppState::EditorCommandType::OpenRenderGraph:
                    if (command.payload.empty())
                        break;
                    ctx.state.currentEditingRenderGraph = command.payload;
                    ctx.state.renderGraphOpenRequested  = true;
                    ctx.state.statusMessage             = "Opening render graph: " + command.payload;
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

    void EditorApp::drawBuildRunPopup()
    {
        if (!m_BuildRunActive && !m_BuildRunCompleted.has_value())
            return;

        if (m_BuildRunPopupPendingOpen)
        {
            if (!ImGui::IsPopupOpen("Export & Run"))
                ImGui::OpenPopup("Export & Run");
            m_BuildRunPopupPendingOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        bool popupOpen = true;
        if (ImGui::BeginPopupModal("Export & Run", &popupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto closeCompletedPopup = [&]() {
                m_BuildRunPopupPendingOpen = false;
                if (!m_BuildRunActive)
                    m_BuildRunCompleted.reset();
                ImGui::CloseCurrentPopup();
            };

            if (!popupOpen)
            {
                closeCompletedPopup();
                ImGui::EndPopup();
                return;
            }

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

            ImGui::TextUnformatted("Export & Run");
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
                closeCompletedPopup();
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

    void EditorApp::drawBuildRunConfigurePopup(EditorContext& ctx)
    {
        if (m_BuildRunConfigureOpen)
        {
            ImGui::OpenPopup("Export & Run Output");
            m_BuildRunConfigureOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        bool popupOpen = true;
        if (!ImGui::BeginPopupModal("Export & Run Output", &popupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if (!popupOpen)
        {
            ImGui::EndPopup();
            return;
        }

        ImGui::TextUnformatted("Export & Run");
        ImGui::Spacing();
        m_BuildRunOutputDialog.draw("Output Folder", m_BuildRunOutputFolder.data(), m_BuildRunOutputFolder.size());
        ImGui::Spacing();

        const bool hasOutput = m_BuildRunOutputFolder[0] != '\0';
        if (!hasOutput)
            ImGui::BeginDisabled();
        if (ImGui::Button("Export & Run", ImVec2 {118.0f, 0.0f}))
        {
            beginBuildAndRun(ctx, std::filesystem::path {m_BuildRunOutputFolder.data()});
            ImGui::CloseCurrentPopup();
        }
        if (!hasOutput)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2 {96.0f, 0.0f}))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void EditorApp::updateBuildAndRun(EditorContext& ctx)
    {
        if (!m_BuildRunActive || !m_BuildRunFuture.valid())
            return;

        if (m_BuildRunFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        const auto result = m_BuildRunFuture.get();
        m_BuildRunActive  = false;
        m_BuildRunProgress.reset();
        m_BuildRunCompleted                     = result;
        m_BuildRunPopupPendingOpen              = true;
        ctx.state.statusMessage                 = result.message;
        ctx.state.buildSettings.lastBuildStatus = result.ok ? "Succeeded" : "Failed";
        ctx.state.buildSettings.lastBuildTime   = "This session";
        ctx.state.buildSettings.buildLog        = result.message;
    }

    void EditorApp::startBuildAndRun(EditorContext& ctx)
    {
        if (m_BuildRunActive)
        {
            ctx.state.statusMessage = "Export & Run is already running.";
            return;
        }
        m_BuildRunCompleted.reset();
        if (ctx.state.currentProject.empty())
        {
            ctx.state.statusMessage = "Export & Run failed: no project is loaded.";
            return;
        }
        if (ctx.state.currentDefaultScene.empty())
        {
            ctx.state.statusMessage = "Export & Run failed: no default scene is selected.";
            return;
        }
        if (ctx.state.editorPlaying)
        {
            ctx.state.statusMessage = "Stop Play Mode before Export & Run.";
            return;
        }

        const bool sceneWasDirty = ctx.state.sceneDirty;
        saveCurrentScene(ctx);
        if (sceneWasDirty && ctx.state.sceneDirty)
        {
            ctx.state.statusMessage = "Export & Run stopped: save the current scene first.";
            return;
        }

        const auto defaultOutput = (ctx.state.currentProject / "build").lexically_normal().generic_string();
        if (ctx.state.buildSettings.outputDirectory.empty())
            ctx.state.buildSettings.outputDirectory = defaultOutput;
        if (ctx.state.buildSettings.projectName.empty())
            ctx.state.buildSettings.projectName = ctx.state.currentProjectName;
        if (m_BuildRunOutputFolder[0] == '\0')
        {
            std::snprintf(m_BuildRunOutputFolder.data(),
                          m_BuildRunOutputFolder.size(),
                          "%s",
                          ctx.state.buildSettings.outputDirectory.c_str());
        }

        ctx.state.statusMessage = "Export & Run: choose output folder.";
        m_BuildRunConfigureOpen = true;
    }

    void
    EditorApp::beginBuildAndRun(EditorContext& ctx, const std::filesystem::path& outputFolder, const bool launchRuntime)
    {
        if (m_BuildRunActive)
        {
            ctx.state.statusMessage = "Export & Run is already running.";
            return;
        }

        const auto projectRoot        = ctx.state.currentProject.lexically_normal();
        const auto assetRoot          = ctx.state.currentAssetRoot;
        const auto projectName        = ctx.state.buildSettings.projectName.empty() ? ctx.state.currentProjectName :
                                                                                      ctx.state.buildSettings.projectName;
        const auto sceneUri           = ctx.state.currentDefaultScene;
        const auto outputDir          = outputFolder.lexically_normal();
        const auto targetPlatform     = ctx.state.buildSettings.targetPlatform;
        const auto exportTemplatePath = ctx.state.buildSettings.exportTemplatePath;

        ctx.state.statusMessage =
            launchRuntime ? "Export & Run started: packaging runtime..." : "Export started: packaging runtime...";
        m_BuildRunProgress         = std::make_shared<BuildRunTaskProgress>();
        m_BuildRunPopupPendingOpen = true;
        {
            std::scoped_lock lock(m_BuildRunProgress->mutex);
            m_BuildRunProgress->progress = 0.02f;
            m_BuildRunProgress->message  = "Saving scene and preparing package...";
        }
        m_BuildRunActive = true;
        auto progress    = m_BuildRunProgress;
        m_BuildRunFuture = std::async(std::launch::async,
                                      [projectRoot,
                                       assetRoot,
                                       projectName,
                                       sceneUri,
                                       outputDir,
                                       targetPlatform,
                                       exportTemplatePath,
                                       launchRuntime,
                                       progress]() {
                                          return runPcVulkanBuildAndLaunch(projectRoot,
                                                                           assetRoot,
                                                                           projectName,
                                                                           sceneUri,
                                                                           outputDir,
                                                                           targetPlatform,
                                                                           exportTemplatePath,
                                                                           launchRuntime,
                                                                           progress);
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
        }

        m_WindowManager.destroy(ctx);
        m_ThumbnailService.clear();
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
        m_WindowManager.addWindow<FrameDebuggerWindow>();
        m_WindowManager.addWindow<ProfilerWindow>();
        m_WindowManager.addWindow<InspectorWindow>();
        m_WindowManager.addWindow<HistoryWindow>();
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
                                         std::vector<std::filesystem::path> importPaths)
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
            1, 1, [this, progress, assetRootPath, importedFolder, registryFile, importPaths = std::move(importPaths)](vtask::Range) {
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
                    importResult = importer.importOrReimportAssetFolder(result.assetRoot, false);
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
                            importResult = importer.importOrReimportAssetFolder(importPath.generic_string(), false);
                        else
                            importResult = importer.importOrReimportAsset(importPath.generic_string(), false);

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
            ctx.state.pendingAssetImportPaths.clear();
            ctx.state.pendingAssetImportRefresh = false;
            m_BackgroundAssetImportPaths = importPaths;
            startAssetImportTask(ctx.state.currentProject, ctx.state.currentAssetRoot, std::move(importPaths));
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
        if (!importedThumbnailPaths.empty())
            m_ThumbnailService.prewarmSourceThumbnails(ctx, importedThumbnailPaths);
        m_ThumbnailService.prewarmSourceThumbnails(ctx, m_BackgroundAssetImportPaths);
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
            releaseEditorStateForProjectLoad(ctx);
            applySplashWindow(ctx);
            return true;
        }

        if (m_Loading.phase == LoadingPhase::Idle)
            return false;

        if (projectRoot != m_Loading.projectRoot)
        {
            startProjectLoading(projectRoot);
            releaseEditorStateForProjectLoad(ctx);
            applySplashWindow(ctx);
            return true;
        }

        switch (m_Loading.phase)
        {
            case LoadingPhase::Pending:
                applySplashWindow(ctx);
                m_Loading.phase    = LoadingPhase::ShowSplash;
                m_Loading.progress = 0.06f;
                m_Loading.message  = "Preparing project assets...";
                return true;

            case LoadingPhase::ShowSplash:
                applySplashWindow(ctx);
                releaseEditorStateForProjectLoad(ctx);
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
        waitForAssetImportTask();
        saveCurrentSceneThumbnail(ctx);

        if (ctx.services)
        {
            if (auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>())
                backendService->renderDevice().waitIdle();
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                worldService->world().clear();
        }

        m_WindowManager.destroy(ctx);
        m_ThumbnailService.clear();
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

        std::vector<std::string> labels;
        char                     fpsText[32] {};
        std::snprintf(fpsText, sizeof(fpsText), "FPS %.1f", fps);
        labels.emplace_back(fpsText);

        const uint64_t cpuCacheBytes =
            frame ? frame->assetCpuCacheBytes + frame->renderCpuCacheBytes :
                    assetMemoryStats.cpuCacheBytes + renderMemoryStats.cpuCacheBytes;
        const uint64_t gpuBytes = frame ? frame->gpuDeviceLocalBytes : renderMemoryStats.gpuDeviceLocalBytes;
        labels.emplace_back("CPU cache " + formatBytes(cpuCacheBytes));
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
