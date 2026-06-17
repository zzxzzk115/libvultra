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
#include <vasset/vasset_pack.hpp>
#endif

#include <vultra/function/imgui/imgui_dpi.hpp>

#include <imgui.h>
#include <miniz.h>

// cpp-httplib pulls in <windows.h>/winsock; keep it after our own includes and below NOMINMAX.
#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
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

        bool targetNeedsExecutableExtension(const std::string& targetPlatform)
        {
            // Case-insensitive so the CLI (`--export-platform windows`) and GUI ("Windows") agree.
            return targetPlatform == "Windows" || targetPlatform == "windows";
        }

        std::string currentHostArch()
        {
#if defined(__aarch64__) || defined(_M_ARM64)
            return "arm64";
#else
            return "x64";
#endif
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
            std::vector<std::string> pluginExtraDirArgs;     // "<physical-dir>=<logical-prefix>"
            std::vector<std::string> pluginExtraExcludeArgs; // "<logical-prefix>=<glob>"
            if (!enabledPluginIds.empty())
            {
                for (const auto& manifest :
                     plugins::discoverProjectPlugins(plugins::discoveryDirs(projectRoot, assetRoot)))
                {
                    if (std::find(enabledPluginIds.begin(), enabledPluginIds.end(), manifest.id) ==
                        enabledPluginIds.end())
                        continue;

                    // A whole editor-only plugin (e.g. an ImGui panel extension) can never run in the
                    // exported runtime, so drop it entirely - no files, no plugin_dirs entry.
                    if (manifest.editorOnly)
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
#ifdef VULTRA_HAS_VASSET_IMPORT
                            // Drop editor-only files (matched against the plugin-relative path).
                            if (!manifest.editorOnlyFiles.empty())
                            {
                                std::error_code rec;
                                const auto pluginRel = fs::relative(file.path(), manifest.directory, rec).generic_string();
                                if (!rec && std::any_of(manifest.editorOnlyFiles.begin(),
                                                        manifest.editorOnlyFiles.end(),
                                                        [&](const std::string& g) {
                                                            return vasset::matchPathGlob(pluginRel, g);
                                                        }))
                                    continue;
                            }
#endif
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
                        // Managed plugins are packed verbatim by vasset's --extra-dir; pass each
                        // editor-only glob through as an --extra-exclude keyed by the same logical prefix.
                        for (const auto& glob : manifest.editorOnlyFiles)
                            pluginExtraExcludeArgs.push_back(logicalDir + "=" + glob);
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
            // Editor-only files inside managed plugins are dropped via --extra-exclude.
            for (const auto& exclude : pluginExtraExcludeArgs)
            {
                packArgs.push_back("--extra-exclude");
                packArgs.push_back(exclude);
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
                                     const std::string&                    architecture,
                                     const std::string&                    exportTemplatePath,
                                     const bool                            launchRuntime,
                                     std::shared_ptr<BuildRunTaskProgress> progress)
        {
            namespace fs = std::filesystem;
            static_cast<void>(architecture); // template selection is explicit; arch is informational only

            const auto     packageName = sanitizedPackageName(projectName, projectRoot);
            const fs::path outputDir   = outputFolder.lexically_normal();
            const fs::path packageExecutable =
                outputDir / (targetNeedsExecutableExtension(targetPlatform) ? packageName + ".exe" : packageName);
            // Engine-neutral, fixed package name (findDefaultVpk() prefers "resources.vpk"); the app
            // executable keeps the project name, only the asset package is standardized.
            const fs::path vpkPath = outputDir / "resources.vpk";

            std::error_code ec;
            fs::create_directories(outputDir, ec);
            if (ec)
                return {.ok      = false,
                        .message = vultra::trf("editorBuild.error.cannotCreateOutputFolder", outputDir.generic_string())};

            if (auto pack = packProjectVpk(projectRoot, assetRoot, projectName, sceneUri, vpkPath, progress); !pack.ok)
                return pack;

            if (launchRuntime && targetPlatform != currentHostPlatform())
            {
                return {.ok = false, .message = vultra::tr("editorBuild.error.targetMustMatchHost")};
            }

            setBuildRunProgress(progress, 0.90f, vultra::tr("editorBuild.progress.copyingTemplate"));
            // A runtime template is mandatory for every platform, including the host: the editor binary
            // itself is never shipped as the game runtime (it carries editor/MCP launch options and is
            // not a clean runtime). The template is an editor-free vultra-runtime obtained by the user
            // (selected locally or downloaded official); no path is ever assumed.
            if (exportTemplatePath.empty())
                return {.ok = false, .message = vultra::tr("editorBuild.error.templateRequired")};
            const fs::path runtimeExecutable = fs::path {exportTemplatePath}.lexically_normal();
            if (!fs::exists(runtimeExecutable, ec) || !fs::is_regular_file(runtimeExecutable, ec))
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

        // Copies every file under srcDir into outputDir, preserving relative layout.
        bool copyWebTemplateTree(const std::filesystem::path& srcDir,
                                 const std::filesystem::path& outputDir,
                                 std::string&                 errorMessage)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(srcDir, ec))
            {
                if (ec)
                    break;
                const auto relative = fs::relative(entry.path(), srcDir, ec);
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

            // A .zip template is extracted to a staging dir, then the directory that actually contains
            // index.html is copied out. This tolerates archives that nest the bundle under a top-level
            // folder (e.g. zipping the `web-template` folder itself, not its contents).
            if (fs::is_regular_file(templateSource, ec) && templateSource.extension().generic_string() == ".zip")
            {
                const fs::path staging = outputDir / ".vultra_webtemplate_extract";
                fs::remove_all(staging, ec);
                fs::create_directories(staging, ec);

                // Extract natively with miniz: no external archiver, so no PATH lookup, no shell quoting,
                // and no locale-encoded (GBK) error text. A system tar/unzip is unreliable across machines.
                mz_zip_archive zip;
                mz_zip_zero_struct(&zip);
                if (!mz_zip_reader_init_file(&zip, templateSource.string().c_str(), 0))
                {
                    fs::remove_all(staging, ec);
                    errorMessage = vultra::trf("editorBuild.error.webTemplateExtractFailed",
                                               templateSource.generic_string() + " (cannot open zip)");
                    return false;
                }
                const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
                for (mz_uint i = 0; i < fileCount; ++i)
                {
                    mz_zip_archive_file_stat st;
                    if (!mz_zip_reader_file_stat(&zip, i, &st))
                        continue;
                    std::string name = st.m_filename;
                    std::replace(name.begin(), name.end(), '\\', '/');
                    // Zip-slip guard: skip absolute or parent-escaping entries.
                    if (name.empty() || name.front() == '/' || name.find("..") != std::string::npos)
                        continue;
                    const fs::path dest = staging / fs::path(name);
                    if (mz_zip_reader_is_file_a_directory(&zip, i))
                    {
                        fs::create_directories(dest, ec);
                        continue;
                    }
                    fs::create_directories(dest.parent_path(), ec);
                    if (!mz_zip_reader_extract_to_file(&zip, i, dest.string().c_str(), 0))
                    {
                        mz_zip_reader_end(&zip);
                        fs::remove_all(staging, ec);
                        errorMessage = vultra::trf("editorBuild.error.webTemplateExtractFailed",
                                                   templateSource.generic_string() + " (" + name + ")");
                        return false;
                    }
                }
                mz_zip_reader_end(&zip);

                // Find index.html anywhere in the extracted tree; its directory is the template root.
                fs::path templateRoot;
                for (const auto& entry : fs::recursive_directory_iterator(staging, ec))
                {
                    if (ec)
                        break;
                    if (entry.is_regular_file() && entry.path().filename() == "index.html")
                    {
                        templateRoot = entry.path().parent_path();
                        break;
                    }
                }
                if (templateRoot.empty())
                {
                    fs::remove_all(staging, ec);
                    errorMessage =
                        vultra::trf("editorBuild.error.webTemplateNoIndexHtml", templateSource.generic_string());
                    return false;
                }

                const bool ok = copyWebTemplateTree(templateRoot, outputDir, errorMessage);
                fs::remove_all(staging, ec);
                return ok;
            }

            if (!fs::is_directory(templateSource, ec))
            {
                errorMessage =
                    vultra::trf("editorBuild.error.webTemplateNotDirOrZip", templateSource.generic_string());
                return false;
            }

            return copyWebTemplateTree(templateSource, outputDir, errorMessage);
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

        // The in-process static server must outlive launchWebRuntime() so the editor keeps serving the
        // exported build for the rest of the session; a single instance is reused across re-exports.
        std::unique_ptr<httplib::Server> g_webServer;
        std::thread                      g_webServerThread;

        void openInBrowser(const std::string& url)
        {
#if defined(_WIN32)
            std::ostringstream open;
            open << "start \"\" " << quoteCommandArg(url);
            runCommand(open.str());
#elif defined(__APPLE__)
            std::ostringstream open;
            open << "open " << quoteCommandArg(url);
            runCommand(open.str());
#else
            std::ostringstream open;
            open << "xdg-open " << quoteCommandArg(url) << " >/dev/null 2>&1 &";
            runCommand(open.str());
#endif
        }

        // Serve `outputDir` over an in-process HTTP server (cpp-httplib) and open the browser. fetch()
        // is blocked on file://, so a real http origin is required; bundling the server avoids a fragile
        // external Python dependency. The server runs detached for the lifetime of the process.
        bool launchWebRuntime(const std::filesystem::path& outputDir,
                              const std::string&           sceneUri,
                              const int                    port,
                              std::string&                 message)
        {
            if (g_webServer)
            {
                g_webServer->stop();
                if (g_webServerThread.joinable())
                    g_webServerThread.join();
                g_webServer.reset();
            }

            auto server = std::make_unique<httplib::Server>();
            // Serve .wasm with the correct MIME so the browser can stream-compile it.
            server->set_file_extension_and_mimetype_mapping("wasm", "application/wasm");
            if (!server->set_mount_point("/", outputDir.string()))
            {
                message = vultra::trf("editorBuild.web.cannotMountDir", outputDir.generic_string());
                return false;
            }
            if (!server->bind_to_port("127.0.0.1", port))
            {
                message = vultra::trf("editorBuild.web.cannotBindPort", std::to_string(port));
                return false;
            }

            g_webServer       = std::move(server);
            g_webServerThread = std::thread([] { g_webServer->listen_after_bind(); });
            g_webServerThread.detach();

            std::string url = "http://127.0.0.1:" + std::to_string(port) + "/index.html?vpk=resources.vpk";
            if (!sceneUri.empty())
                url += "&scene=" + urlEncodeQueryValue(sceneUri);

            openInBrowser(url);
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

            // Fixed, engine-neutral package name (a project is not necessarily a "game"); the web
            // shell fetches it and the runtime's findDefaultVpk() already prefers "resources.vpk".
            const fs::path vpkPath = outputDir / "resources.vpk";
            if (auto pack = packProjectVpk(projectRoot, assetRoot, projectName, sceneUri, vpkPath, progress); !pack.ok)
                return pack;

            setBuildRunProgress(progress, 0.80f, vultra::tr("editorBuild.progress.copyingWebTemplate"));
            // A web export template is mandatory and never assumed: the user must set a directory/.zip
            // (or download the official one). No silent fallback to build/web-template.
            if (exportTemplatePath.empty())
                return {.ok = false, .message = vultra::tr("editorBuild.error.webTemplateRequired")};
            const fs::path templateSource = fs::path {exportTemplatePath}.lexically_normal();

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
                                         const std::string&                    architecture,
                                         const std::string&                    exportTemplatePath,
                                         const bool                            launchRuntime,
                                         std::shared_ptr<BuildRunTaskProgress> progress)
        {
            std::string platformKey = targetPlatform;
            std::transform(platformKey.begin(), platformKey.end(), platformKey.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (platformKey == "webgpu" || platformKey == "web" || platformKey == "wasm")
                return exportWeb(
                    projectRoot, assetRoot, projectName, sceneUri, outputFolder, exportTemplatePath, launchRuntime, progress);

            if (platformKey == "android")
                return {.ok = false, .message = vultra::tr("editorBuild.error.androidNotImplemented")};

            return exportDesktop(projectRoot,
                                 assetRoot,
                                 projectName,
                                 sceneUri,
                                 outputFolder,
                                 targetPlatform,
                                 architecture,
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
        const std::string architecture = currentHostArch();

        std::cout << "[export] project=" << projectName << " scene=" << sceneUri << " platform=" << targetPlatform
                  << " output=" << outputFolder.generic_string() << (options.exportRun ? " (run)" : "") << "\n";

        auto       progress = std::make_shared<BuildRunTaskProgress>();
        const auto result   = runBuildAndLaunch(projectRoot,
                                              assetRoot,
                                              projectName,
                                              sceneUri,
                                              outputFolder,
                                              targetPlatform,
                                              architecture,
                                              options.exportTemplatePath,
                                              options.exportRun,
                                              progress);

        std::cout << "[export] " << (result.ok ? "OK: " : "FAILED: ") << result.message << "\n";

        // For web Export & Run the HTTP server lives on a detached thread in-process; block here so it
        // keeps serving (otherwise headless exit would tear it down immediately). Ctrl+C stops it.
        std::string platformKeyLower = targetPlatform;
        std::transform(platformKeyLower.begin(), platformKeyLower.end(), platformKeyLower.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (result.ok && options.exportRun &&
            (platformKeyLower == "web" || platformKeyLower == "wasm" || platformKeyLower == "webgpu"))
        {
            std::cout << "[export] Serving web build at http://127.0.0.1:8753/ -- press Ctrl+C to stop.\n";
            std::cout.flush();
            for (;;)
                std::this_thread::sleep_for(std::chrono::hours(1));
        }

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
            // Seed the template field from this platform's saved preset so the user sees what's set.
            std::snprintf(m_ExportTemplateBuffer.data(),
                          m_ExportTemplateBuffer.size(),
                          "%s",
                          ctx.state.buildSettings.exportTemplatePath.c_str());
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

        auto& settings = ctx.state.buildSettings;

        ImGui::TextUnformatted(vultra::tr("editorBuild.label.exportAndRun"));
        ImGui::Spacing();
        ImGui::Text("%s: %s", vultra::tr("exportSettings.platform"), settings.targetPlatform.c_str());
        ImGui::Spacing();

        m_BuildRunOutputDialog.setDefaultPath(ctx.state.currentProject);
        m_BuildRunOutputDialog.draw(
            vultra::tr("editorBuild.label.outputFolder"), m_BuildRunOutputFolder.data(), m_BuildRunOutputFolder.size());
        ImGui::Spacing();

        // A runtime/web template is mandatory for every platform, including the host (the editor binary
        // is never shipped as the game runtime). Offer the same options everywhere: pick a local
        // template (a runtime binary, or a directory/.zip for Web) or download the official one.
        m_ExportTemplateDialog.drawBrowseOnly(
            vultra::tr("exportSettings.exportTemplate"), m_ExportTemplateBuffer.data(), m_ExportTemplateBuffer.size());
        settings.exportTemplatePath = m_ExportTemplateBuffer.data();
        drawExportTemplateDownloadRow(settings);
        // The download row may have refreshed the buffer/path on completion.
        settings.exportTemplatePath = m_ExportTemplateBuffer.data();
        ImGui::Spacing();

        const bool hasOutput   = m_BuildRunOutputFolder[0] != '\0';
        const bool hasTemplate = m_ExportTemplateBuffer[0] != '\0';
        if (!hasOutput)
            ImGui::TextColored(ImVec4 {1.0f, 0.32f, 0.28f, 1.0f}, "%s", vultra::tr("exportSettings.block.outputRequired"));
        else if (!hasTemplate)
            ImGui::TextColored(ImVec4 {1.0f, 0.32f, 0.28f, 1.0f},
                               "%s",
                               vultra::trf("exportSettings.block.missingTemplate", settings.targetPlatform).c_str());

        const bool canRun = hasOutput && hasTemplate;
        if (!canRun)
            ImGui::BeginDisabled();
        if (ImGui::Button(vultra::tr("editorBuild.label.exportAndRun"), ImVec2 {vultra::ui::dp(118.0f), 0.0f}))
        {
            settings.exportTemplatePath = m_ExportTemplateBuffer.data();
            beginBuildAndRun(ctx, std::filesystem::path {m_BuildRunOutputFolder.data()});
            ImGui::CloseCurrentPopup();
        }
        if (!canRun)
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

    void EditorApp::persistExportSettings(EditorContext& ctx)
    {
        // Best-effort: round-trip the on-disk .vproject so we only add/refresh the export_* fields and
        // leave scenes/plugins untouched. Silently no-op without a loadable project.
        if (ctx.state.currentProject.empty())
            return;
        // Capture the active platform's current settings, then write the whole per-platform map. The
        // active target platform itself is intentionally not persisted.
        storeExportPreset(ctx.state);
        auto project = loadVProject(ctx.state.currentProject);
        if (!project.has_value())
            return;
        project->exportSettings = ctx.state.exportSettings;
        std::string err;
        static_cast<void>(saveVProject(*project, &err));
    }

    void
    EditorApp::beginBuildAndRun(EditorContext& ctx, const std::filesystem::path& outputFolder, const bool launchRuntime)
    {
        if (m_BuildRunActive)
        {
            ctx.state.statusMessage = vultra::tr("editorBuild.status.alreadyRunning");
            return;
        }

        // A runtime/web template is mandatory for EVERY platform, including the host: the editor binary
        // is never shipped as the game runtime. Never assume a path -- fail fast if none is set.
        if (ctx.state.buildSettings.exportTemplatePath.empty())
        {
            ctx.state.statusMessage =
                vultra::trf("exportSettings.block.missingTemplate", ctx.state.buildSettings.targetPlatform);
            return;
        }

        // Remember what we're exporting with so the choice sticks across sessions (persist on export).
        persistExportSettings(ctx);

        const auto projectRoot        = ctx.state.currentProject.lexically_normal();
        const auto assetRoot          = ctx.state.currentAssetRoot;
        const auto projectName        = ctx.state.buildSettings.projectName.empty() ? ctx.state.currentProjectName :
                                                                                      ctx.state.buildSettings.projectName;
        const auto sceneUri           = ctx.state.currentDefaultScene;
        const auto outputDir          = outputFolder.lexically_normal();
        const auto targetPlatform     = ctx.state.buildSettings.targetPlatform;
        const auto architecture       = ctx.state.buildSettings.architecture;
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
                                       architecture,
                                       exportTemplatePath,
                                       launchRuntime,
                                       progress]() {
                                          return runBuildAndLaunch(projectRoot,
                                                                   assetRoot,
                                                                   projectName,
                                                                   sceneUri,
                                                                   outputDir,
                                                                   targetPlatform,
                                                                   architecture,
                                                                   exportTemplatePath,
                                                                   launchRuntime,
                                                                   progress);
                                      });
    }

} // namespace vultra_app
