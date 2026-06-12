#include "editor_app/editor_app.hpp"

#include "editor_app/plugin_repository.hpp"
#include "editor_app/project_asset_utils.hpp"
#include "editor_app/ui/settings_widgets.hpp"
#include "launch_options.hpp"
#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/i18n/i18n.hpp>
#include <vultra/function/material_graph/material_graph_compiler.hpp>
#include <vultra/function/plugin/plugin_manifest.hpp>
#include <vultra/function/services/scene_service.hpp>

#ifdef VULTRA_HAS_VASSET_IMPORT
#include <vultra/core/builtin/builtin_resources.hpp>

#include <vasset/tool_cli.hpp>
#include <vasset/vasset_importers.hpp>
#endif

#include <vultra/function/imgui/imgui_dpi.hpp>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
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
        for (auto& [virtualPath, sourceText] : vultra::builtin::shaderIncludeSources())
        {
            options.shaderVirtualIncludes.push_back({
                .virtualPath = std::move(virtualPath),
                .sourceText  = std::move(sourceText),
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

        void appendPackRoot(std::vector<std::string>& packArgs, std::vector<std::string>& seenRoots, std::string root)
        {
            if (root.empty() || std::find(seenRoots.begin(), seenRoots.end(), root) != seenRoots.end())
                return;
            seenRoots.push_back(root);
            packArgs.push_back("--root");
            packArgs.push_back(std::move(root));
        }

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
                errorMessage = vultra::trf("editorBuild.error.createOutputFolderFailed", ec.message());
                return false;
            }

            if (!samePath(runtimeExecutable, packageExecutable))
            {
                fs::copy_file(runtimeExecutable, packageExecutable, fs::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    errorMessage = vultra::trf("editorBuild.error.copyRuntimeFailed", ec.message());
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


        // Reimport the project's assets and pack them into vpkPath. Shared by every platform's
        // export path (desktop copies a runtime exe next to it; web fetches it at page load).
        BuildRunResult packProjectVpk(const std::filesystem::path&                 projectRoot,
                                      const std::string&                           assetRoot,
                                      const std::string&                           projectName,
                                      const std::string&                           sceneUri,
                                      const std::filesystem::path&                 vpkPath,
                                      const std::shared_ptr<BuildRunTaskProgress>& progress)
        {
            namespace fs = std::filesystem;

            const fs::path assetRootPath = (projectRoot / assetRoot).lexically_normal();
            std::error_code ec;
            if (!fs::exists(assetRootPath, ec))
                return {.ok      = false,
                        .message = vultra::trf("editorBuild.error.missingAssetRoot", assetRootPath.generic_string())};

            setBuildRunProgress(progress, 0.15f, vultra::tr("editorBuild.progress.writingManifest"));
            auto                     buildScenes = normalizedBuildScenes(sceneUri, {});
            std::vector<std::string> enabledPluginIds;
            if (auto project = loadVProject(projectRoot); project.has_value())
            {
                buildScenes = normalizedBuildScenes(project->defaultScene.empty() ? sceneUri : project->defaultScene,
                                                    project->buildScenes);
                enabledPluginIds = project->enabledPlugins;
            }

            // Bundle the project's enabled plugins (pure-Lua and native + Lua helper) into the VPK so
            // the exported runtime can load them, scanning the same dirs the editor does (local
            // installs under <asset-root>/plugins plus lock-recorded managed dirs). Local installs
            // pack in place; managed plugins live outside the asset root (plugins://<id>/<version>,
            // i.e. .vultra/plugins) and are mirrored into the package as res://plugins/<id>/<version>
            // via --extra-dir. Native libs are extracted to a writable dir and loaded at runtime (a
            // .dll cannot be loaded from inside the VPK in place).
            std::vector<std::string> pluginDirUris;
            std::vector<std::string> pluginFileUris;
            std::vector<std::string> pluginExtraDirArgs; // "<physical-dir>=<logical-prefix>"
            if (!enabledPluginIds.empty())
            {
                for (const auto& manifest :
                     plugins::discoverProjectPlugins(plugins::discoveryDirs(projectRoot, assetRoot)))
                {
                    if (std::find(enabledPluginIds.begin(), enabledPluginIds.end(), manifest.id) ==
                        enabledPluginIds.end())
                        continue;

                    std::error_code dirEc;
                    const auto      relDir = fs::relative(manifest.directory, assetRootPath, dirEc).generic_string();
                    if (!dirEc && !relDir.empty() && relDir != "." && !relDir.starts_with(".."))
                    {
                        pluginDirUris.push_back("res://" + relDir);
                        std::error_code fec;
                        for (const auto& file : fs::recursive_directory_iterator(manifest.directory, fec))
                        {
                            if (fec)
                                break;
                            if (!file.is_regular_file())
                                continue;
                            const auto rel = fs::relative(file.path(), assetRootPath, fec).generic_string();
                            if (fec || rel.empty() || rel.starts_with(".."))
                                continue;
                            pluginFileUris.push_back("res://" + rel);
                        }
                    }
                    else
                    {
                        std::string logicalDir = "plugins/" + manifest.id;
                        if (!manifest.version.empty())
                            logicalDir += "/" + manifest.version;
                        pluginDirUris.push_back("res://" + logicalDir);
                        pluginExtraDirArgs.push_back(manifest.directory.generic_string() + "=" + logicalDir);
                    }
                }
            }

            std::string manifestError;
            if (!saveVPackageManifest(assetRootPath,
                                      VPackageManifest {
                                          .name        = projectName,
                                          .entryScene  = sceneUri,
                                          .buildScenes = buildScenes,
                                          .pluginDirs  = pluginDirUris,
                                      },
                                      &manifestError))
            {
                return {.ok = false, .message = vultra::trf("editorBuild.error.exportFailedDetail", manifestError)};
            }

            setBuildRunProgress(progress, 0.25f, vultra::tr("editorBuild.progress.reimportingPacking"));
#ifdef VULTRA_HAS_VASSET_IMPORT
            // Regenerate material-graph shaders before the cook so graph edits ship in the
            // package. runAssetTool() calls run_vasset_cli() in-process, bypassing main.cpp's
            // dispatch (which does this for the standalone CLI), so do it explicitly here.
            vultra::material_graph::compileProjectMaterialGraphs(projectRoot, assetRootPath);
            const int importResult = runAssetTool({"vultra asset", "import", assetRootPath.generic_string()});
            if (importResult != 0)
                return {.ok      = false,
                        .message = vultra::trf("editorBuild.error.assetImportReturned", importResult)};

            std::vector<std::string> packArgs {
                "vultra asset", "pack", assetRootPath.generic_string(), vpkPath.generic_string(), "--zstd", "6",
            };
            std::vector<std::string> packRoots;
            appendPackRoot(packArgs, packRoots, kVPackageManifestPath);
            for (const auto& scene : buildScenes)
            {
                if (scene.enabled)
                    appendPackRoot(packArgs, packRoots, scene.uri);
            }
            for (const auto& renderGraph : collectProjectAssetUrisWithSuffix(projectRoot, assetRoot, ".vrg.json"))
                appendPackRoot(packArgs, packRoots, renderGraph);
            // Graph assets are tiny and may be referenced indirectly (e.g. an animator graph
            // assigned to a component, a material graph used as an override) - pack every one
            // unconditionally so a stale/incomplete dependency edge can never drop them.
            for (const auto& animatorGraph :
                 collectProjectAssetUrisWithSuffix(projectRoot, assetRoot, ".vanimgraph.json"))
                appendPackRoot(packArgs, packRoots, animatorGraph);
            for (const auto& materialGraph :
                 collectProjectAssetUrisWithSuffix(projectRoot, assetRoot, ".vmatgraph.json"))
                appendPackRoot(packArgs, packRoots, materialGraph);
            for (const auto& shaderLibrary :
                 collectProjectAssetUrisWithSuffix(projectRoot, assetRoot, ".vshaderlib.lua"))
                appendPackRoot(packArgs, packRoots, shaderLibrary);
            for (const auto& feature : collectProjectAssetUrisWithSuffix(projectRoot, assetRoot, ".vfeature.lua"))
                appendPackRoot(packArgs, packRoots, feature);
            for (const auto& srp : collectProjectAssetUrisWithSuffix(projectRoot, assetRoot, ".vsrp.lua"))
                appendPackRoot(packArgs, packRoots, srp);
            for (const auto& renderLua : collectProjectAssetUrisWithExtension(projectRoot, assetRoot, ".lua"))
            {
                if (!renderLua.starts_with("res://render/"))
                    continue;
                appendPackRoot(packArgs, packRoots, renderLua);
            }
            // Bundled plugin files (manifest + Lua + native lib) collected above.
            for (const auto& pluginFile : pluginFileUris)
                appendPackRoot(packArgs, packRoots, pluginFile);
            // Managed plugin dirs live outside the asset root; pack them verbatim.
            for (const auto& extraDir : pluginExtraDirArgs)
            {
                packArgs.push_back("--extra-dir");
                packArgs.push_back(extraDir);
            }

            const int packResult = runAssetTool(packArgs);
            if (packResult != 0)
                return {.ok = false, .message = vultra::trf("editorBuild.error.assetPackReturned", packResult)};
            return {.ok = true, .message = vultra::trf("editorBuild.status.packedVpk", vpkPath.generic_string())};
#else
            static_cast<void>(vpkPath);
            return {.ok = false, .message = vultra::tr("editorBuild.error.vassetUnavailable")};
#endif
        }

        BuildRunResult exportDesktop(const std::filesystem::path&          projectRoot,
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

            const auto     packageName = sanitizedPackageName(projectName, projectRoot);
            const fs::path outputDir   = outputFolder.lexically_normal();
            const fs::path packageExecutable =
                outputDir / (targetNeedsExecutableExtension(targetPlatform) ? packageName + ".exe" : packageName);
            const fs::path vpkPath = outputDir / (packageName + ".vpk");

            std::error_code ec;
            fs::create_directories(outputDir, ec);
            if (ec)
                return {.ok      = false,
                        .message = vultra::trf("editorBuild.error.cannotCreateOutputFolder", outputDir.generic_string())};

            if (auto publishedRuntime = findPublishedRuntimeNextToEditor(); publishedRuntime.has_value())
            {
                if (auto pack = packProjectVpk(projectRoot, assetRoot, projectName, sceneUri, vpkPath, progress);
                    !pack.ok)
                    return pack;

                setBuildRunProgress(progress, 0.55f, vultra::tr("editorBuild.progress.copyingRuntime"));

                std::string copyError;
                if (!copyRuntimeToPackage(*publishedRuntime, packageExecutable, copyError))
                    return {.ok = false, .message = vultra::trf("editorBuild.error.exportFailedDetail", copyError)};

                if (launchRuntime)
                {
                    setBuildRunProgress(progress, 0.94f, vultra::tr("editorBuild.progress.launchingPublishedRuntime"));
                    const int launchResult = launchPackagedRuntime(packageExecutable, vpkPath, sceneUri);
                    if (launchResult != 0)
                        return {.ok      = false,
                                .message = vultra::trf("editorBuild.error.publishedRuntimeLaunchReturned", launchResult)};
                }

                setBuildRunProgress(progress,
                                    1.0f,
                                    launchRuntime ? vultra::tr("editorBuild.progress.runtimeLaunched") :
                                                    vultra::tr("editorBuild.progress.exportComplete"));
                return {.ok      = true,
                        .message = launchRuntime ?
                                       vultra::trf("editorBuild.status.runningPackage", packageExecutable.generic_string()) :
                                       vultra::trf("editorBuild.status.exportComplete", packageExecutable.generic_string())};
            }

            if (auto pack = packProjectVpk(projectRoot, assetRoot, projectName, sceneUri, vpkPath, progress); !pack.ok)
                return pack;

            if (launchRuntime && targetPlatform != currentHostPlatform())
            {
                return {.ok = false, .message = vultra::tr("editorBuild.error.targetMustMatchHost")};
            }

            setBuildRunProgress(progress, 0.90f, vultra::tr("editorBuild.progress.copyingTemplate"));
            fs::path runtimeExecutable;
            if (!exportTemplatePath.empty())
                runtimeExecutable = fs::path {exportTemplatePath}.lexically_normal();
            else if (targetPlatform == currentHostPlatform())
                runtimeExecutable = currentExecutablePath();
            else
            {
                return {.ok = false, .message = vultra::tr("editorBuild.error.templateRequired")};
            }

            if (runtimeExecutable.empty() || !fs::exists(runtimeExecutable, ec) ||
                !fs::is_regular_file(runtimeExecutable, ec))
            {
                return {.ok = false, .message = vultra::tr("editorBuild.error.templateNotFound")};
            }

            std::string copyError;
            if (!copyRuntimeToPackage(runtimeExecutable, packageExecutable, copyError))
                return {.ok = false, .message = vultra::trf("editorBuild.error.exportFailedDetail", copyError)};

            if (launchRuntime)
            {
                setBuildRunProgress(progress, 0.94f, vultra::tr("editorBuild.progress.launchingRuntime"));
                const int launchResult = launchPackagedRuntime(packageExecutable, vpkPath, sceneUri);
                if (launchResult != 0)
                    return {.ok      = false,
                            .message = vultra::trf("editorBuild.error.runtimeLaunchReturned", launchResult)};
            }

            setBuildRunProgress(progress,
                                1.0f,
                                launchRuntime ? vultra::tr("editorBuild.progress.exportCompleteRuntimeLaunched") :
                                                vultra::tr("editorBuild.progress.exportComplete"));
            return {.ok      = true,
                    .message = launchRuntime ?
                                   vultra::trf("editorBuild.status.exportCompleteRunningPackage",
                                               packageExecutable.generic_string()) :
                                   vultra::trf("editorBuild.status.exportComplete", packageExecutable.generic_string())};
        }

        // --- Web (WASM / WebGPU) export -------------------------------------------------------

        std::filesystem::path defaultWebTemplate()
        {
            const auto repoRoot = findRepoRoot();
            if (repoRoot.empty())
                return {};
            // The engine-only web template is a build artifact produced by building the wasm
            // vultra-runtime target (its after_build copies the bundle here). See source/xmake.lua.
            return (repoRoot / "build" / "web-template").lexically_normal();
        }

        bool extractOrCopyWebTemplate(const std::filesystem::path& templateSource,
                                      const std::filesystem::path& outputDir,
                                      std::string&                 errorMessage)
        {
            namespace fs = std::filesystem;
            std::error_code ec;

            if (templateSource.empty() || !fs::exists(templateSource, ec))
            {
                errorMessage = vultra::trf("editorBuild.error.webTemplateNotFound", templateSource.generic_string());
                return false;
            }

            // A .zip template is extracted in place; a directory template is copied file-by-file.
            if (fs::is_regular_file(templateSource, ec) && templateSource.extension().generic_string() == ".zip")
            {
                std::ostringstream cmd;
#if defined(_WIN32)
                // bsdtar (tar.exe) ships on Windows 10+ and extracts .zip transparently.
                cmd << "tar -xf " << quoteCommandArg(templateSource) << " -C " << quoteCommandArg(outputDir);
#else
                cmd << "unzip -o " << quoteCommandArg(templateSource) << " -d " << quoteCommandArg(outputDir);
#endif
                if (runCommand(cmd.str()) != 0)
                {
                    errorMessage =
                        vultra::trf("editorBuild.error.webTemplateExtractFailed", templateSource.generic_string());
                    return false;
                }
                return true;
            }

            if (!fs::is_directory(templateSource, ec))
            {
                errorMessage =
                    vultra::trf("editorBuild.error.webTemplateNotDirOrZip", templateSource.generic_string());
                return false;
            }

            for (const auto& entry : fs::recursive_directory_iterator(templateSource, ec))
            {
                if (ec)
                    break;
                const auto relative = fs::relative(entry.path(), templateSource, ec);
                if (ec)
                    continue;
                const auto destination = outputDir / relative;
                if (entry.is_directory())
                {
                    fs::create_directories(destination, ec);
                }
                else if (entry.is_regular_file())
                {
                    fs::create_directories(destination.parent_path(), ec);
                    fs::copy_file(entry.path(), destination, fs::copy_options::overwrite_existing, ec);
                    if (ec)
                    {
                        errorMessage = vultra::trf(
                            "editorBuild.error.copyTemplateFileFailed", entry.path().generic_string(), ec.message());
                        return false;
                    }
                }
            }
            return true;
        }

        std::string urlEncodeQueryValue(const std::string& value)
        {
            static const char* hex = "0123456789ABCDEF";
            std::string        out;
            out.reserve(value.size());
            for (const unsigned char ch : value)
            {
                const bool unreserved = std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
                                        ch == ':' || ch == '/';
                if (unreserved)
                    out.push_back(static_cast<char>(ch));
                else
                {
                    out.push_back('%');
                    out.push_back(hex[(ch >> 4) & 0xF]);
                    out.push_back(hex[ch & 0xF]);
                }
            }
            return out;
        }

        std::optional<std::string> findStaticServerCommand()
        {
            // Prefer a Python http.server; it needs no extra dependencies on most dev machines.
            for (const char* candidate : {"py", "python", "python3"})
            {
#if defined(_WIN32)
                const std::string probe = std::string {"where "} + candidate + " >nul 2>nul";
#else
                const std::string probe = std::string {"command -v "} + candidate + " >/dev/null 2>&1";
#endif
                if (runCommand(probe) == 0)
                    return std::string {candidate};
            }
            return std::nullopt;
        }

        bool launchWebRuntime(const std::filesystem::path& outputDir,
                              const std::string&           sceneUri,
                              const int                    port,
                              std::string&                 message)
        {
            const auto python = findStaticServerCommand();
            if (!python.has_value())
            {
                message = vultra::tr("editorBuild.web.noPythonServer");
                return false;
            }

            std::string url = "http://127.0.0.1:" + std::to_string(port) + "/index.html";
            if (!sceneUri.empty())
                url += "?scene=" + urlEncodeQueryValue(sceneUri);

#if defined(_WIN32)
            std::ostringstream serve;
            serve << "start \"vultra-web\" /D " << quoteCommandArg(outputDir) << " " << *python
                  << " -m http.server " << port << " --bind 127.0.0.1";
            runCommand(serve.str());

            std::ostringstream open;
            open << "start \"\" " << quoteCommandArg(url);
            runCommand(open.str());
#else
            std::ostringstream serve;
            serve << "cd " << quoteCommandArg(outputDir) << " && " << *python << " -m http.server " << port
                  << " --bind 127.0.0.1 >/dev/null 2>&1 &";
            runCommand(serve.str());

            std::ostringstream open;
#if defined(__APPLE__)
            open << "open " << quoteCommandArg(url);
#else
            open << "xdg-open " << quoteCommandArg(url) << " >/dev/null 2>&1 &";
#endif
            runCommand(open.str());
#endif
            message = vultra::trf("editorBuild.web.servingAt", url);
            return true;
        }

        BuildRunResult exportWeb(const std::filesystem::path&          projectRoot,
                                 const std::string&                    assetRoot,
                                 const std::string&                    projectName,
                                 const std::string&                    sceneUri,
                                 const std::filesystem::path&          outputFolder,
                                 const std::string&                    exportTemplatePath,
                                 const bool                            launchRuntime,
                                 std::shared_ptr<BuildRunTaskProgress> progress)
        {
            namespace fs = std::filesystem;

            const fs::path outputDir = outputFolder.lexically_normal();
            std::error_code ec;
            fs::create_directories(outputDir, ec);
            if (ec)
                return {.ok      = false,
                        .message = vultra::trf("editorBuild.error.cannotCreateOutputFolder", outputDir.generic_string())};

            // The web shell defaults to fetching "game.vpk"; keep the name fixed for the template.
            const fs::path vpkPath = outputDir / "game.vpk";
            if (auto pack = packProjectVpk(projectRoot, assetRoot, projectName, sceneUri, vpkPath, progress); !pack.ok)
                return pack;

            setBuildRunProgress(progress, 0.80f, vultra::tr("editorBuild.progress.copyingWebTemplate"));
            const fs::path templateSource =
                exportTemplatePath.empty() ? defaultWebTemplate() : fs::path {exportTemplatePath}.lexically_normal();

            std::string templateError;
            if (!extractOrCopyWebTemplate(templateSource, outputDir, templateError))
                return {.ok = false, .message = vultra::trf("editorBuild.error.exportFailedDetail", templateError)};

            const fs::path indexHtml = outputDir / "index.html";
            if (!fs::exists(indexHtml, ec))
                return {.ok      = false,
                        .message = vultra::trf("editorBuild.error.webTemplateNoIndexHtml",
                                               templateSource.generic_string())};

            if (!launchRuntime)
            {
                setBuildRunProgress(progress, 1.0f, vultra::tr("editorBuild.progress.exportComplete"));
                return {.ok      = true,
                        .message = vultra::trf("editorBuild.status.exportCompleteServeOverHttp",
                                               outputDir.generic_string())};
            }

            setBuildRunProgress(progress, 0.94f, vultra::tr("editorBuild.progress.startingWebServer"));
            std::string serverMessage;
            const int   port = 8753;
            const bool  served = launchWebRuntime(outputDir, sceneUri, port, serverMessage);

            setBuildRunProgress(progress,
                                1.0f,
                                served ? vultra::tr("editorBuild.progress.runtimeLaunchedInBrowser") :
                                         vultra::tr("editorBuild.progress.exportComplete"));
            return {.ok      = true,
                    .message = served ?
                                   vultra::trf("editorBuild.status.exportCompleteServerMessage", serverMessage) :
                                   vultra::trf("editorBuild.status.exportCompleteWithServerMessage",
                                               outputDir.generic_string(),
                                               serverMessage)};
        }

        BuildRunResult runBuildAndLaunch(const std::filesystem::path&          projectRoot,
                                         const std::string&                    assetRoot,
                                         const std::string&                    projectName,
                                         const std::string&                    sceneUri,
                                         const std::filesystem::path&          outputFolder,
                                         const std::string&                    targetPlatform,
                                         const std::string&                    exportTemplatePath,
                                         const bool                            launchRuntime,
                                         std::shared_ptr<BuildRunTaskProgress> progress)
        {
            if (targetPlatform == "WebGPU" || targetPlatform == "Web" || targetPlatform == "wasm")
                return exportWeb(
                    projectRoot, assetRoot, projectName, sceneUri, outputFolder, exportTemplatePath, launchRuntime, progress);

            if (targetPlatform == "Android")
                return {.ok = false, .message = vultra::tr("editorBuild.error.androidNotImplemented")};

            return exportDesktop(projectRoot,
                                 assetRoot,
                                 projectName,
                                 sceneUri,
                                 outputFolder,
                                 targetPlatform,
                                 exportTemplatePath,
                                 launchRuntime,
                                 progress);
        }
    } // namespace

    int runHeadlessExport(const LaunchOptions& options)
    {
        namespace fs = std::filesystem;

        if (options.projectPath.empty())
        {
            std::cerr << "[export] --export requires --project <project-dir>.\n";
            return 2;
        }
        const fs::path projectRoot = fs::path {options.projectPath}.lexically_normal();

        auto project = loadVProject(projectRoot);
        if (!project.has_value())
        {
            std::cerr << "[export] Failed to load a project at " << projectRoot.generic_string() << "\n";
            return 1;
        }

        const std::string assetRoot   = project->assetRoot.empty() ? std::string {"resources"} : project->assetRoot;
        const std::string projectName = project->name.empty() ? projectRoot.filename().generic_string() : project->name;
        std::string       sceneUri    = options.sceneUri.empty() ? project->defaultScene : options.sceneUri;
        if (sceneUri.empty() && !project->buildScenes.empty())
            sceneUri = project->buildScenes.front().uri;
        if (sceneUri.empty())
        {
            std::cerr << "[export] Project has no default/build scene; pass --scene <res://...>.\n";
            return 1;
        }

        const fs::path outputFolder = options.exportOutput.empty() ?
                                          (projectRoot / "export").lexically_normal() :
                                          fs::path {options.exportOutput}.lexically_normal();
        const std::string targetPlatform =
            options.exportPlatform.empty() ? currentHostPlatform() : options.exportPlatform;

        std::cout << "[export] project=" << projectName << " scene=" << sceneUri << " platform=" << targetPlatform
                  << " output=" << outputFolder.generic_string() << (options.exportRun ? " (run)" : "") << "\n";

        auto       progress = std::make_shared<BuildRunTaskProgress>();
        const auto result   = runBuildAndLaunch(projectRoot,
                                              assetRoot,
                                              projectName,
                                              sceneUri,
                                              outputFolder,
                                              targetPlatform,
                                              std::string {},
                                              options.exportRun,
                                              progress);

        std::cout << "[export] " << (result.ok ? "OK: " : "FAILED: ") << result.message << "\n";
        return result.ok ? 0 : 1;
    }

    void EditorApp::drawBuildRunPopup()
    {
        if (!m_BuildRunActive && !m_BuildRunCompleted.has_value())
            return;

        if (m_BuildRunPopupPendingOpen)
        {
            if (!ImGui::IsPopupOpen(vultra::trId("editorBuild.popup.exportAndRun", "ExportAndRunPopup")))
                ImGui::OpenPopup(vultra::trId("editorBuild.popup.exportAndRun", "ExportAndRunPopup"));
            m_BuildRunPopupPendingOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        bool popupOpen = true;
        if (ImGui::BeginPopupModal(vultra::trId("editorBuild.popup.exportAndRun", "ExportAndRunPopup"),
                                   &popupOpen,
                                   ImGuiWindowFlags_AlwaysAutoResize))
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
            std::string message  = vultra::tr("editorBuild.progress.preparing");
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

            ImGui::TextUnformatted(vultra::tr("editorBuild.label.exportAndRun"));
            ImGui::Spacing();
            ImGui::ProgressBar(progress, ImVec2 {vultra::ui::dp(360.0f), 0.0f});
            ImGui::Spacing();
            if (m_BuildRunCompleted.has_value() && !m_BuildRunCompleted->ok)
                ImGui::TextColored(ImVec4 {1.0f, 0.32f, 0.28f, 1.0f}, "%s", message.c_str());
            else
                ImGui::TextWrapped("%s", message.c_str());
            if (m_BuildRunActive)
                ImGui::TextDisabled("%s", vultra::tr("editorBuild.hint.reimportCanTakeAWhile"));
            else if (ImGui::Button(vultra::tr("common.close"), ImVec2 {vultra::ui::dp(96.0f), 0.0f}))
                closeCompletedPopup();
            ImGui::EndPopup();
        }
    }

    void EditorApp::drawBuildRunConfigurePopup(EditorContext& ctx)
    {
        if (m_BuildRunConfigureOpen)
        {
            ImGui::OpenPopup(vultra::trId("editorBuild.popup.exportAndRunOutput", "ExportAndRunOutputPopup"));
            m_BuildRunConfigureOpen = false;
        }

        ui::centerNextModalInCurrentWindow();
        bool popupOpen = true;
        if (!ImGui::BeginPopupModal(vultra::trId("editorBuild.popup.exportAndRunOutput", "ExportAndRunOutputPopup"),
                                    &popupOpen,
                                    ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if (!popupOpen)
        {
            ImGui::EndPopup();
            return;
        }

        ImGui::TextUnformatted(vultra::tr("editorBuild.label.exportAndRun"));
        ImGui::Spacing();
        m_BuildRunOutputDialog.setDefaultPath(ctx.state.currentProject);
        m_BuildRunOutputDialog.draw(
            vultra::tr("editorBuild.label.outputFolder"), m_BuildRunOutputFolder.data(), m_BuildRunOutputFolder.size());
        ImGui::Spacing();

        const bool hasOutput = m_BuildRunOutputFolder[0] != '\0';
        if (!hasOutput)
            ImGui::BeginDisabled();
        if (ImGui::Button(vultra::tr("editorBuild.label.exportAndRun"), ImVec2 {vultra::ui::dp(118.0f), 0.0f}))
        {
            beginBuildAndRun(ctx, std::filesystem::path {m_BuildRunOutputFolder.data()});
            ImGui::CloseCurrentPopup();
        }
        if (!hasOutput)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button(vultra::tr("common.cancel"), ImVec2 {vultra::ui::dp(96.0f), 0.0f}))
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
        ctx.state.buildSettings.lastBuildStatus =
            result.ok ? vultra::tr("editorBuild.status.succeeded") : vultra::tr("editorBuild.status.failed");
        ctx.state.buildSettings.lastBuildTime = vultra::tr("editorBuild.status.thisSession");
        ctx.state.buildSettings.buildLog        = result.message;
    }

    void EditorApp::startBuildAndRun(EditorContext& ctx)
    {
        if (m_BuildRunActive)
        {
            ctx.state.statusMessage = vultra::tr("editorBuild.status.alreadyRunning");
            return;
        }
        m_BuildRunCompleted.reset();
        if (ctx.state.currentProject.empty())
        {
            ctx.state.statusMessage = vultra::tr("editorBuild.status.noProjectLoaded");
            return;
        }
        if (ctx.state.currentDefaultScene.empty())
        {
            ctx.state.statusMessage = vultra::tr("editorBuild.status.noDefaultScene");
            return;
        }
        if (ctx.state.editorPlaying)
        {
            ctx.state.statusMessage = vultra::tr("editorBuild.status.stopPlayModeFirst");
            return;
        }

        const bool sceneWasDirty = ctx.state.sceneDirty;
        saveCurrentScene(ctx);
        if (sceneWasDirty && ctx.state.sceneDirty)
        {
            ctx.state.statusMessage = vultra::tr("editorBuild.status.saveSceneFirst");
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

        ctx.state.statusMessage = vultra::tr("editorBuild.status.chooseOutputFolder");
        m_BuildRunConfigureOpen = true;
    }

    void
    EditorApp::beginBuildAndRun(EditorContext& ctx, const std::filesystem::path& outputFolder, const bool launchRuntime)
    {
        if (m_BuildRunActive)
        {
            ctx.state.statusMessage = vultra::tr("editorBuild.status.alreadyRunning");
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

        ctx.state.statusMessage = launchRuntime ? vultra::tr("editorBuild.status.exportRunStartedPackaging") :
                                                  vultra::tr("editorBuild.status.exportStartedPackaging");
        m_BuildRunProgress         = std::make_shared<BuildRunTaskProgress>();
        m_BuildRunPopupPendingOpen = true;
        {
            std::scoped_lock lock(m_BuildRunProgress->mutex);
            m_BuildRunProgress->progress = 0.02f;
            m_BuildRunProgress->message  = vultra::tr("editorBuild.progress.savingScenePreparing");
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
                                          return runBuildAndLaunch(projectRoot,
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
