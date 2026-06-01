#include "editor_app/editor_app.hpp"

#include "editor_app/ui/settings_widgets.hpp"
#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/function/services/scene_service.hpp>

#ifdef VULTRA_HAS_VASSET_IMPORT
#include <builtin_shaders.hpp>
#include <vasset/tool_cli.hpp>
#include <vasset/vasset_importers.hpp>
#endif

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

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

namespace vultra_app
{
    namespace
    {
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
        m_BuildRunOutputDialog.setDefaultPath(ctx.state.currentProject);
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

        if (ctx.state.buildSettings.projectName.empty())
            ctx.state.buildSettings.projectName = ctx.state.currentProjectName;
        if (m_BuildRunOutputFolder[0] == '\0' && !ctx.state.buildSettings.outputDirectory.empty())
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

} // namespace vultra_app
