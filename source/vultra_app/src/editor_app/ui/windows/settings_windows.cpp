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
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <future>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#    include <windows.h>
#    include <winhttp.h>
#endif

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

        const char* pluginConfigTypeLabel(vultra::PluginConfigParamType type)
        {
            switch (type)
            {
                case vultra::PluginConfigParamType::ePath:
                    return "path";
                case vultra::PluginConfigParamType::eBool:
                    return "bool";
                case vultra::PluginConfigParamType::eInt:
                    return "int";
                case vultra::PluginConfigParamType::eFloat:
                    return "float";
                case vultra::PluginConfigParamType::eString:
                default:
                    return "string";
            }
        }

        std::string getProcessEnvString(const std::string& name)
        {
            if (name.empty())
                return {};
            const char* value = std::getenv(name.c_str());
            return value == nullptr ? std::string {} : std::string {value};
        }

        std::string trText(const char* key, const char* fallback)
        {
            std::string text = vultra::trId(key, fallback);
            if (const auto pos = text.find("###"); pos != std::string::npos)
                text.resize(pos);
            return text;
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

        int runPluginManagerCommand(const std::string& command) { return std::system(command.c_str()); }

        bool isHttpUrl(std::string_view value)
        {
            return value.starts_with("https://") || value.starts_with("http://");
        }

        std::vector<std::filesystem::path> splitPathList(const std::string& value)
        {
            std::vector<std::filesystem::path> result;
#if defined(_WIN32)
            constexpr char separator = ';';
#else
            constexpr char separator = ':';
#endif
            std::size_t start = 0;
            while (start <= value.size())
            {
                const auto end = value.find(separator, start);
                const auto item = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!item.empty())
                    result.emplace_back(item);
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            return result;
        }

        std::optional<std::filesystem::path> resolveExecutablePath(const std::string& executableName)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path direct {executableName};
            if (direct.is_absolute() && fs::exists(direct, ec))
                return direct;

            if (const char* pathEnv = std::getenv("PATH"); pathEnv != nullptr)
            {
                for (const auto& dir : splitPathList(pathEnv))
                {
                    const auto candidate = dir / executableName;
                    if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                        return candidate;
                }
            }

#if defined(_WIN32)
            if (executableName == "git.exe" || executableName == "git")
            {
                const std::array<fs::path, 4> candidates {
                    fs::path {"C:/Program Files/Git/cmd/git.exe"},
                    fs::path {"C:/Program Files/Git/bin/git.exe"},
                    fs::path {"C:/Program Files (x86)/Git/cmd/git.exe"},
                    fs::path {"C:/Program Files (x86)/Git/bin/git.exe"},
                };
                for (const auto& candidate : candidates)
                {
                    if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                        return candidate;
                }
            }
#endif

            return std::nullopt;
        }

        std::optional<std::filesystem::path> gitCommandPath()
        {
#if defined(_WIN32)
            return resolveExecutablePath("git.exe");
#else
            return resolveExecutablePath("git");
#endif
        }

        std::string defaultPluginCatalogUrl()
        {
            return "https://raw.githubusercontent.com/zzxzzk115/vultra-plugins/main/plugins.json";
        }

        std::filesystem::path localPluginInstallDir(const std::filesystem::path& projectRoot,
                                                    const std::string&           assetRoot)
        {
            return (projectRoot / assetRoot / "plugins").lexically_normal();
        }

        std::filesystem::path managedGitPluginDir(const std::filesystem::path& projectRoot)
        {
            return (projectRoot / ".vultra" / "plugins" / "git").lexically_normal();
        }

        std::vector<std::filesystem::path> pluginDiscoveryDirs(const std::filesystem::path& projectRoot,
                                                               const std::string&           assetRoot)
        {
            return {
                localPluginInstallDir(projectRoot, assetRoot),
                managedGitPluginDir(projectRoot),
            };
        }

        std::vector<vultra::PluginManifest> discoverProjectPlugins(const std::vector<std::filesystem::path>& dirs,
                                                                   const std::vector<std::string>& lockedManagedIds = {},
                                                                   const bool includeAllManaged = false)
        {
            std::vector<vultra::PluginManifest> result;
            std::unordered_map<std::string, std::size_t> seen;
            for (const auto& dir : dirs)
            {
                const bool isManagedDir = dir.filename() == "git" && dir.parent_path().filename() == "plugins";
                for (auto manifest : vultra::discoverPlugins(dir))
                {
                    if (manifest.id.empty())
                        continue;
                    if (isManagedDir && !includeAllManaged &&
                        std::find(lockedManagedIds.begin(), lockedManagedIds.end(), manifest.id) == lockedManagedIds.end())
                        continue;
                    if (seen.contains(manifest.id))
                    {
                        result[seen[manifest.id]] = std::move(manifest);
                        continue;
                    }
                    seen[manifest.id] = result.size();
                    result.push_back(std::move(manifest));
                }
            }
            std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
                return a.name < b.name;
            });
            return result;
        }

#if defined(_WIN32)
        std::wstring widenUtf8(std::string_view text);

        std::wstring windowsCommandLineArg(const std::wstring& arg)
        {
            if (arg.empty())
                return L"\"\"";

            bool needsQuotes = false;
            for (const wchar_t ch : arg)
            {
                if (ch == L' ' || ch == L'\t' || ch == L'"')
                {
                    needsQuotes = true;
                    break;
                }
            }
            if (!needsQuotes)
                return arg;

            std::wstring out = L"\"";
            std::size_t  backslashes = 0;
            for (const wchar_t ch : arg)
            {
                if (ch == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (ch == L'"')
                {
                    out.append(backslashes * 2 + 1, L'\\');
                    out.push_back(ch);
                    backslashes = 0;
                    continue;
                }
                out.append(backslashes, L'\\');
                backslashes = 0;
                out.push_back(ch);
            }
            out.append(backslashes * 2, L'\\');
            out.push_back(L'"');
            return out;
        }

        bool runProcess(const std::filesystem::path& executable,
                        const std::vector<std::string>& args,
                        std::string& status)
        {
            std::wstring commandLine = windowsCommandLineArg(executable.wstring());
            for (const auto& arg : args)
            {
                commandLine.push_back(L' ');
                commandLine += windowsCommandLineArg(widenUtf8(arg));
            }

            STARTUPINFOW        startup {};
            PROCESS_INFORMATION process {};
            startup.cb = sizeof(startup);
            std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
            mutableCommand.push_back(L'\0');

            if (!CreateProcessW(executable.wstring().c_str(),
                                mutableCommand.data(),
                                nullptr,
                                nullptr,
                                FALSE,
                                CREATE_NO_WINDOW,
                                nullptr,
                                nullptr,
                                &startup,
                                &process))
            {
                status = "Failed to start process.";
                return false;
            }

            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(process.hProcess, &exitCode);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            if (exitCode != 0)
            {
                status = "Process failed with exit code " + std::to_string(exitCode) + ".";
                return false;
            }
            return true;
        }
#else
        bool runProcess(const std::filesystem::path& executable,
                        const std::vector<std::string>& args,
                        std::string& status)
        {
            std::ostringstream cmd;
            cmd << quoteCommandArg(executable);
            for (const auto& arg : args)
                cmd << " " << quoteCommandArg(arg);
            if (runPluginManagerCommand(cmd.str()) != 0)
            {
                status = "Process failed.";
                return false;
            }
            return true;
        }
#endif

#if defined(_WIN32)
        std::wstring widenUtf8(std::string_view text)
        {
            if (text.empty())
                return {};
            const int size = MultiByteToWideChar(
                CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (size <= 0)
                return {};
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
            return result;
        }

        struct WinHttpHandle
        {
            HINTERNET handle {nullptr};
            ~WinHttpHandle()
            {
                if (handle != nullptr)
                    WinHttpCloseHandle(handle);
            }
            WinHttpHandle() = default;
            explicit WinHttpHandle(HINTERNET value) : handle(value) {}
            WinHttpHandle(const WinHttpHandle&)            = delete;
            WinHttpHandle& operator=(const WinHttpHandle&) = delete;
            WinHttpHandle(WinHttpHandle&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
            WinHttpHandle& operator=(WinHttpHandle&& other) noexcept
            {
                if (this != &other)
                {
                    if (handle != nullptr)
                        WinHttpCloseHandle(handle);
                    handle       = other.handle;
                    other.handle = nullptr;
                }
                return *this;
            }
            explicit operator bool() const { return handle != nullptr; }
        };

        bool downloadTextToFile(const std::string& url, const std::filesystem::path& destination, std::string& status)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(destination.parent_path(), ec);
            if (ec)
            {
                status = "Failed to create download cache: " + ec.message();
                return false;
            }

            std::wstring wideUrl = widenUtf8(url);
            URL_COMPONENTS parts {};
            parts.dwStructSize      = sizeof(parts);
            parts.dwSchemeLength    = static_cast<DWORD>(-1);
            parts.dwHostNameLength  = static_cast<DWORD>(-1);
            parts.dwUrlPathLength   = static_cast<DWORD>(-1);
            parts.dwExtraInfoLength = static_cast<DWORD>(-1);
            if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts))
            {
                status = "Invalid catalog URL.";
                return false;
            }

            const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
            std::wstring       path(parts.lpszUrlPath, parts.dwUrlPathLength);
            if (parts.dwExtraInfoLength > 0)
                path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
            if (path.empty())
                path = L"/";

            WinHttpHandle session {WinHttpOpen(L"VultraEditor/0.1",
                                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                               WINHTTP_NO_PROXY_NAME,
                                               WINHTTP_NO_PROXY_BYPASS,
                                               0)};
            if (!session)
            {
                status = "WinHTTP session creation failed.";
                return false;
            }

            WinHttpHandle connect {WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0)};
            if (!connect)
            {
                status = "WinHTTP connection failed.";
                return false;
            }

            const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
            WinHttpHandle request {WinHttpOpenRequest(connect.handle,
                                                      L"GET",
                                                      path.c_str(),
                                                      nullptr,
                                                      WINHTTP_NO_REFERER,
                                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                      flags)};
            if (!request)
            {
                status = "WinHTTP request creation failed.";
                return false;
            }

            if (!WinHttpSendRequest(request.handle,
                                    WINHTTP_NO_ADDITIONAL_HEADERS,
                                    0,
                                    WINHTTP_NO_REQUEST_DATA,
                                    0,
                                    0,
                                    0) ||
                !WinHttpReceiveResponse(request.handle, nullptr))
            {
                status = "Catalog download failed.";
                return false;
            }

            DWORD statusCode     = 0;
            DWORD statusCodeSize = sizeof(statusCode);
            if (WinHttpQueryHeaders(request.handle,
                                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX,
                                    &statusCode,
                                    &statusCodeSize,
                                    WINHTTP_NO_HEADER_INDEX) &&
                (statusCode < 200 || statusCode >= 300))
            {
                status = "Catalog download returned HTTP " + std::to_string(statusCode) + ".";
                return false;
            }

            std::ofstream out(destination, std::ios::binary);
            if (!out)
            {
                status = "Failed to open catalog cache for writing.";
                return false;
            }

            for (;;)
            {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request.handle, &available))
                {
                    status = "Catalog download failed while reading.";
                    return false;
                }
                if (available == 0)
                    break;

                std::vector<char> buffer(available);
                DWORD             read = 0;
                if (!WinHttpReadData(request.handle, buffer.data(), available, &read))
                {
                    status = "Catalog download failed while receiving data.";
                    return false;
                }
                out.write(buffer.data(), static_cast<std::streamsize>(read));
            }

            return true;
        }
#else
        bool downloadTextToFile(const std::string&, const std::filesystem::path&, std::string& status)
        {
            status = "Remote plugin catalogs are not implemented on this platform yet.";
            return false;
        }
#endif

        std::string safePluginCacheName(std::string_view text)
        {
            std::string name;
            for (const char ch : text)
            {
                if (std::isalnum(static_cast<unsigned char>(ch)))
                    name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                else if (ch == '-' || ch == '_' || ch == '.')
                    name.push_back(ch);
                else if (!name.empty() && name.back() != '-')
                    name.push_back('-');
            }
            if (name.size() > 48)
                name.resize(48);
            while (!name.empty() && name.back() == '-')
                name.pop_back();
            if (name.empty())
                name = "plugin";
            std::ostringstream suffix;
            suffix << "-" << std::hex << std::hash<std::string_view> {}(text);
            return name + suffix.str();
        }

        std::optional<std::filesystem::path> findPluginRoot(const std::filesystem::path& root)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (root.empty() || !fs::exists(root, ec))
                return std::nullopt;

            const auto direct = root / vultra::kPluginManifestFile;
            if (fs::exists(direct, ec))
                return root;

            for (const auto& entry : fs::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec))
                    continue;
                if (entry.path().filename() == vultra::kPluginManifestFile)
                    return entry.path().parent_path();
            }
            return std::nullopt;
        }

        std::uintmax_t directoryFileCount(const std::filesystem::path& dir)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            std::uintmax_t  count = 0;
            for (const auto& entry : fs::recursive_directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (entry.is_regular_file(ec))
                    ++count;
            }
            return count;
        }

        std::uintmax_t directoryByteSize(const std::filesystem::path& dir)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            std::uintmax_t  bytes = 0;
            for (const auto& entry : fs::recursive_directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (entry.is_regular_file(ec))
                    bytes += entry.file_size(ec);
            }
            return bytes;
        }

        std::string textFileHash(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return {};
            std::ostringstream buffer;
            buffer << file.rdbuf();
            std::ostringstream out;
            out << std::hex << std::hash<std::string> {}(buffer.str());
            return out.str();
        }

        nlohmann::json loadPluginLock(const std::filesystem::path& lockPath)
        {
            std::ifstream file(lockPath);
            if (!file)
                return nlohmann::json {{"schemaVersion", 1}, {"plugins", nlohmann::json::array()}};
            auto json = nlohmann::json::parse(file, nullptr, false);
            if (json.is_discarded() || !json.is_object())
                return nlohmann::json {{"schemaVersion", 1}, {"plugins", nlohmann::json::array()}};
            if (!json.contains("plugins") || !json["plugins"].is_array())
                json["plugins"] = nlohmann::json::array();
            json["schemaVersion"] = json.value("schemaVersion", 1);
            return json;
        }

        std::vector<std::string> lockedPluginIds(const std::filesystem::path& projectRoot)
        {
            std::vector<std::string> ids;
            const auto lock = loadPluginLock(projectRoot / "vultra.plugins.lock");
            for (const auto& item : lock.value("plugins", nlohmann::json::array()))
            {
                const auto id = item.value("id", std::string {});
                if (!id.empty())
                    ids.push_back(id);
            }
            return ids;
        }

        void savePluginLock(const std::filesystem::path& projectRoot,
                            const std::vector<std::filesystem::path>& pluginDirs,
                            const std::optional<vultra::PluginManifest>& importedManifest,
                            const nlohmann::json&                       importedSource,
                            const std::vector<std::string>&             excludedIds = {})
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(projectRoot, ec);
            const auto lockPath = projectRoot / "vultra.plugins.lock";

            std::unordered_map<std::string, nlohmann::json> previous;
            const auto oldLock = loadPluginLock(lockPath);
            for (const auto& item : oldLock.value("plugins", nlohmann::json::array()))
            {
                const auto id = item.value("id", std::string {});
                if (!id.empty())
                    previous[id] = item;
            }

            std::vector<std::string> managedIds;
            managedIds.reserve(previous.size() + (importedManifest.has_value() ? 1 : 0));
            for (const auto& [id, entry] : previous)
            {
                if (std::find(excludedIds.begin(), excludedIds.end(), id) == excludedIds.end())
                    managedIds.push_back(id);
            }
            if (importedManifest.has_value() &&
                std::find(excludedIds.begin(), excludedIds.end(), importedManifest->id) == excludedIds.end() &&
                std::find(managedIds.begin(), managedIds.end(), importedManifest->id) == managedIds.end())
            {
                managedIds.push_back(importedManifest->id);
            }

            nlohmann::json plugins = nlohmann::json::array();
            for (const auto& manifest : discoverProjectPlugins(pluginDirs, managedIds))
            {
                if (std::find(excludedIds.begin(), excludedIds.end(), manifest.id) != excludedIds.end())
                    continue;
                nlohmann::json entry = previous.contains(manifest.id) ? previous[manifest.id] : nlohmann::json::object();
                entry["id"]          = manifest.id;
                entry["name"]        = manifest.name;
                entry["version"]     = manifest.version;
                entry["directory"]   = fs::relative(manifest.directory, projectRoot, ec).generic_string();
                entry["manifestHash"] = textFileHash(manifest.manifestPath);
                entry["fileCount"]    = directoryFileCount(manifest.directory);
                entry["byteSize"]     = directoryByteSize(manifest.directory);

                if (importedManifest.has_value() && importedManifest->id == manifest.id)
                    entry["source"] = importedSource;
                else if (!entry.contains("source"))
                    entry["source"] = nlohmann::json {{"type", "unknown"}};

                plugins.push_back(std::move(entry));
            }

            const nlohmann::json lock = {{"schemaVersion", 1}, {"plugins", std::move(plugins)}};
            std::ofstream       out(lockPath);
            if (out)
                out << lock.dump(2) << "\n";
        }

        bool copyPluginDirectory(const std::filesystem::path& source,
                                 const std::filesystem::path& destination,
                                 std::string&                 error)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (fs::equivalent(source, destination, ec))
                return true;

            fs::remove_all(destination, ec);
            if (ec)
            {
                error = "failed to replace existing plugin folder: " + ec.message();
                return false;
            }

            fs::create_directories(destination.parent_path(), ec);
            if (ec)
            {
                error = "failed to create plugin folder: " + ec.message();
                return false;
            }

            fs::copy(source,
                     destination,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                     ec);
            if (ec)
            {
                error = "failed to copy plugin files: " + ec.message();
                return false;
            }
            return true;
        }

        void refreshPluginLock(const std::filesystem::path& projectRoot,
                               const std::string&           assetRoot,
                               const std::vector<std::string>& excludedIds = {})
        {
            savePluginLock(
                projectRoot, pluginDiscoveryDirs(projectRoot, assetRoot), std::nullopt, nlohmann::json {}, excludedIds);
        }

        bool unloadPluginIfLoaded(vultra::IPluginService* plugins, const vultra::PluginManifest& manifest, std::string& status)
        {
            if (plugins == nullptr || !plugins->isLoaded(manifest.id))
                return true;

            const auto displayName = manifest.name.empty() ? manifest.id : manifest.name;
            if (!plugins->unloadPlugin(manifest.id))
            {
                status = vultra::trf("projectSettings.plugins.unloadFailed", displayName, manifest.id);
                return false;
            }

            status = vultra::trf("projectSettings.plugins.unloaded", displayName, manifest.id);
            return true;
        }

        bool removePluginInstall(const std::filesystem::path& projectRoot,
                                 const std::string&           assetRoot,
                                 const vultra::PluginManifest& manifest,
                                 std::string&                 status)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (manifest.directory.empty() || !fs::exists(manifest.directory, ec))
            {
                status = "Plugin directory does not exist.";
                return false;
            }

            const auto localDir   = localPluginInstallDir(projectRoot, assetRoot).lexically_normal();
            const auto managedDir = managedGitPluginDir(projectRoot).lexically_normal();
            const auto pluginDir  = manifest.directory.lexically_normal();
            const auto localRel   = fs::relative(pluginDir, localDir, ec);
            const auto localRelText = localRel.generic_string();
            const bool inLocal =
                !ec && !localRelText.empty() && localRelText != ".." && !localRelText.starts_with("../");
            ec.clear();
            const auto managedRel = fs::relative(pluginDir, managedDir, ec);
            const auto managedRelText = managedRel.generic_string();
            const bool inManaged =
                !ec && !managedRelText.empty() && managedRelText != ".." && !managedRelText.starts_with("../");
            if (!inLocal && !inManaged)
            {
                status = "Refusing to remove a plugin outside the project plugin roots.";
                return false;
            }

            if (inLocal)
            {
                fs::remove_all(pluginDir, ec);
                if (ec)
                {
                    status = "Failed to remove plugin: " + ec.message();
                    return false;
                }
                status = "Removed local plugin '" + manifest.name + "' (" + manifest.id + ").";
            }
            else
            {
                status = "Removed managed plugin '" + manifest.name + "' (" + manifest.id + ") from project lock.";
            }

            refreshPluginLock(projectRoot, assetRoot, inManaged ? std::vector<std::string> {manifest.id} :
                                                                  std::vector<std::string> {});
            return true;
        }

        bool installLocalPluginFromRoot(const std::filesystem::path& projectRoot,
                                        const std::filesystem::path& pluginsDir,
                                        const std::filesystem::path& sourceRoot,
                                        const nlohmann::json&        sourceInfo,
                                        std::string&                 status,
                                        std::string*                 installedId = nullptr)
        {
            std::string manifestError;
            auto        manifest = vultra::loadPluginManifest(sourceRoot / vultra::kPluginManifestFile, &manifestError);
            if (!manifest.has_value())
            {
                status = manifestError.empty() ? "not a Vultra plugin folder" : manifestError;
                return false;
            }

            const auto destination = (pluginsDir / sourceRoot.filename()).lexically_normal();
            std::string copyError;
            if (!copyPluginDirectory(sourceRoot, destination, copyError))
            {
                status = copyError;
                return false;
            }

            auto installedManifest =
                vultra::loadPluginManifest(destination / vultra::kPluginManifestFile, &manifestError);
            if (!installedManifest.has_value())
            {
                status = manifestError.empty() ? "plugin was copied but its manifest could not be read" : manifestError;
                return false;
            }

            savePluginLock(projectRoot, {pluginsDir, managedGitPluginDir(projectRoot)}, installedManifest, sourceInfo);
            if (installedId != nullptr)
                *installedId = installedManifest->id;
            status = "Installed plugin '" + installedManifest->name + "' (" + installedManifest->id + ").";
            return true;
        }

        bool importPluginFromFolder(const std::filesystem::path& projectRoot,
                                    const std::filesystem::path& pluginsDir,
                                    const std::filesystem::path& sourceFolder,
                                    std::string&                 status,
                                    std::string*                 installedId = nullptr)
        {
            const auto root = findPluginRoot(sourceFolder);
            if (!root.has_value())
            {
                status = "No " + std::string(vultra::kPluginManifestFile) + " found in folder.";
                return false;
            }

            return installLocalPluginFromRoot(
                projectRoot,
                pluginsDir,
                *root,
                nlohmann::json {{"type", "folder"}, {"path", sourceFolder.generic_string()}},
                status,
                installedId);
        }

        bool importPluginFromZip(const std::filesystem::path& projectRoot,
                                 const std::filesystem::path& pluginsDir,
                                 const std::filesystem::path& zipPath,
                                 std::string&                 status,
                                 std::string*                 installedId = nullptr)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (!fs::exists(zipPath, ec) || !fs::is_regular_file(zipPath, ec))
            {
                status = "Zip file does not exist.";
                return false;
            }

            const auto extractDir =
                (projectRoot / ".vultra" / "plugins" / "zip-imports" / safePluginCacheName(zipPath.stem().generic_string()))
                    .lexically_normal();
            fs::remove_all(extractDir, ec);
            fs::create_directories(extractDir, ec);
            if (ec)
            {
                status = "Failed to create zip import cache: " + ec.message();
                return false;
            }

            std::ostringstream cmd;
#if defined(_WIN32)
            cmd << "tar -xf " << quoteCommandArg(zipPath) << " -C " << quoteCommandArg(extractDir);
#else
            cmd << "unzip -o " << quoteCommandArg(zipPath) << " -d " << quoteCommandArg(extractDir);
#endif
            if (runPluginManagerCommand(cmd.str()) != 0)
            {
                status = "Failed to extract plugin zip.";
                return false;
            }

            const auto root = findPluginRoot(extractDir);
            if (!root.has_value())
            {
                status = "Zip did not contain " + std::string(vultra::kPluginManifestFile) + ".";
                return false;
            }

            return installLocalPluginFromRoot(
                projectRoot,
                pluginsDir,
                *root,
                nlohmann::json {{"type", "zip"}, {"path", zipPath.generic_string()}},
                status,
                installedId);
        }

        bool importPluginFromGit(const std::filesystem::path& projectRoot,
                                 const std::filesystem::path& pluginsDir,
                                 const std::string&           url,
                                 std::string&                 status,
                                 std::string*                 installedId = nullptr)
        {
            namespace fs = std::filesystem;
            if (url.empty())
            {
                status = "Git URL is empty.";
                return false;
            }
            const auto git = gitCommandPath();
            if (!git.has_value())
            {
                status =
                    "Git executable was not found by the editor process. Add Git for Windows to PATH or install it in "
                    "C:/Program Files/Git.";
                return false;
            }

            const auto cacheDir = (managedGitPluginDir(projectRoot) / safePluginCacheName(url)).lexically_normal();
            std::error_code ec;
            fs::create_directories(cacheDir.parent_path(), ec);
            if (ec)
            {
                status = "Failed to create git plugin cache: " + ec.message();
                return false;
            }

            bool        ok = false;
            std::string cacheWarning;
            if (fs::exists(cacheDir / ".git", ec))
            {
                std::string pullStatus;
                ok = runProcess(*git, {"-C", cacheDir.generic_string(), "pull", "--ff-only"}, pullStatus);
                if (!ok && fs::exists(cacheDir, ec))
                {
                    ok = true;
                    cacheWarning = " Using cached checkout because update failed: " + pullStatus;
                }
            }
            else if (fs::exists(cacheDir, ec) && findPluginRoot(cacheDir).has_value())
            {
                ok = true;
                cacheWarning = " Using cached plugin files.";
            }
            else
            {
                ok = runProcess(*git,
                                {"clone", "--depth", "1", url, cacheDir.generic_string()},
                                status);
            }

            if (!ok)
            {
                status = "Git import failed: " + status;
                return false;
            }

            const auto root = findPluginRoot(cacheDir);
            if (!root.has_value())
            {
                status = "Git repository did not contain " + std::string(vultra::kPluginManifestFile) + ".";
                return false;
            }

            std::string manifestError;
            auto        manifest = vultra::loadPluginManifest(*root / vultra::kPluginManifestFile, &manifestError);
            if (!manifest.has_value())
            {
                status = manifestError.empty() ? "git plugin manifest could not be read" : manifestError;
                return false;
            }

            savePluginLock(projectRoot,
                           {pluginsDir, managedGitPluginDir(projectRoot)},
                           manifest,
                           nlohmann::json {
                               {"type", "git"},
                               {"url", url},
                               {"cache", fs::relative(cacheDir, projectRoot, ec).generic_string()},
                           });
            if (installedId != nullptr)
                *installedId = manifest->id;
            status = "Installed managed plugin '" + manifest->name + "' (" + manifest->id + ")." + cacheWarning;
            return true;
        }

        struct CatalogPluginEntry
        {
            std::string id;
            std::string name;
            std::string version;
            std::string author;
            std::string description;
            std::string repository;
            std::string gitUrl;
        };

        bool fetchPluginCatalog(const std::filesystem::path& projectRoot,
                                const std::string&           catalogLocation,
                                std::vector<CatalogPluginEntry>& entries,
                                std::string&                 status)
        {
            namespace fs = std::filesystem;
            entries.clear();
            if (catalogLocation.empty())
            {
                status = "Catalog location is empty.";
                return false;
            }

            std::error_code ec;
            fs::path catalogPath {catalogLocation};
            if (isHttpUrl(catalogLocation))
            {
                catalogPath =
                    (projectRoot / ".vultra" / "plugins" / "catalogs" / (safePluginCacheName(catalogLocation) + ".json"))
                        .lexically_normal();
                if (!downloadTextToFile(catalogLocation, catalogPath, status))
                    return false;
            }

            std::ifstream file(catalogPath);
            if (!file)
            {
                status = "Plugin catalog could not be opened.";
                return false;
            }

            auto json = nlohmann::json::parse(file, nullptr, false);
            if (json.is_discarded() || !json.is_object() || !json.value("plugins", nlohmann::json::array()).is_array())
            {
                status = "Plugin catalog is not valid JSON.";
                return false;
            }

            for (const auto& plugin : json.value("plugins", nlohmann::json::array()))
            {
                const auto source = plugin.value("source", nlohmann::json::object());
                if (source.value("type", std::string {}) != "git")
                    continue;

                const auto url = source.value("url", std::string {});
                if (url.empty())
                    continue;

                entries.push_back(CatalogPluginEntry {
                    .id          = plugin.value("id", std::string {}),
                    .name        = plugin.value("name", std::string {}),
                    .version     = plugin.value("version", std::string {}),
                    .author      = plugin.value("author", std::string {}),
                    .description = plugin.value("description", std::string {}),
                    .repository  = plugin.value("repository", std::string {}),
                    .gitUrl      = url,
                });
            }

            if (entries.empty())
            {
                status = "Catalog did not contain any git-backed plugins.";
                return false;
            }

            status = "Loaded " + std::to_string(entries.size()) + " catalog plugin(s).";
            return true;
        }

        bool drawPluginConfigParam(
            std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& values,
            const vultra::PluginManifest& manifest,
            const vultra::PluginConfigParam& param)
        {
            bool changed = false;
            auto& value = values[manifest.id][param.key];
            if (value.empty())
            {
                if (!param.envVar.empty())
                    value = getProcessEnvString(param.envVar);
                if (value.empty() && !param.defaultValue.empty())
                    value = param.defaultValue;
            }

            const std::string label = param.label.empty() ? param.key : param.label;
            ImGui::PushID(param.key.c_str());
            ImGui::TextUnformatted(label.c_str());
            if (param.required)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4 {1.0f, 0.55f, 0.3f, 1.0f}, "*");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%s%s%s)",
                                pluginConfigTypeLabel(param.type),
                                param.envVar.empty() ? "" : " env ",
                                param.envVar.empty() ? "" : param.envVar.c_str());

            if (param.type == vultra::PluginConfigParamType::eBool)
            {
                bool boolValue = value == "1" || value == "true" || value == "yes" || value == "on";
                if (ImGui::Checkbox("##value", &boolValue))
                {
                    value   = boolValue ? "true" : "false";
                    changed = true;
                }
            }
            else
            {
                std::array<char, 512> buffer {};
                setBuffer(buffer, value);
                const auto flags = param.secret ? ImGuiInputTextFlags_Password : ImGuiInputTextFlags_None;
                if (ImGui::InputTextWithHint("##value",
                                             param.defaultValue.empty() ? "" : param.defaultValue.c_str(),
                                             buffer.data(),
                                             buffer.size(),
                                             flags))
                {
                    value   = bufferString(buffer);
                    changed = true;
                }
            }

            if (!param.description.empty())
                ImGui::TextWrapped("%s", param.description.c_str());
            ImGui::PopID();
            return changed;
        }

        void splitPluginConfigValuesForSave(
            const std::vector<std::filesystem::path>& pluginDirs,
            const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& uiValues,
            std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& projectValues,
            std::unordered_map<std::string, std::string>& envValues)
        {
            projectValues = uiValues;
            envValues.clear();

            for (const auto& manifest : discoverProjectPlugins(pluginDirs))
            {
                auto pluginIt = projectValues.find(manifest.id);
                if (pluginIt == projectValues.end())
                    continue;

                for (const auto& param : manifest.configParams)
                {
                    if (param.envVar.empty())
                        continue;

                    const auto valueIt = pluginIt->second.find(param.key);
                    if (valueIt == pluginIt->second.end())
                        continue;

                    envValues[param.envVar] = valueIt->second;
                    pluginIt->second.erase(valueIt);
                }

                if (pluginIt->second.empty())
                    projectValues.erase(pluginIt);
            }
        }

        enum class PluginImportMode
        {
            eNone,
            eGit,
            eCatalog,
            eZip,
            eFolder,
        };

        std::string pluginImportTitle(PluginImportMode mode)
        {
            switch (mode)
            {
                case PluginImportMode::eGit:
                    return trText("projectSettings.plugins.importGit", "Git URL");
                case PluginImportMode::eCatalog:
                    return trText("projectSettings.plugins.importCatalog", "Catalog");
                case PluginImportMode::eZip:
                    return trText("projectSettings.plugins.importZip", "ZIP");
                case PluginImportMode::eFolder:
                    return trText("projectSettings.plugins.importFolder", "Folder");
                case PluginImportMode::eNone:
                default:
                    return trText("projectSettings.plugins.importHeader", "Import plugin");
            }
        }

        std::string pluginImportHint(PluginImportMode mode)
        {
            switch (mode)
            {
                case PluginImportMode::eGit:
                    return trText("projectSettings.plugins.gitUrlHint",
                                  "Git URL (https://github.com/owner/plugin.git)");
                case PluginImportMode::eCatalog:
                    return trText("projectSettings.plugins.catalogHint",
                                  "Catalog JSON URL/path (vultra-plugins/plugins.json)");
                case PluginImportMode::eZip:
                    return trText("projectSettings.plugins.zipPathHint", "Local ZIP path");
                case PluginImportMode::eFolder:
                    return trText("projectSettings.plugins.folderPathHint", "Local plugin folder path");
                case PluginImportMode::eNone:
                default:
                    return "";
            }
        }

        std::array<char, 512>& pluginImportBuffer(PluginImportMode mode,
                                                  std::array<char, 512>& gitUrl,
                                                  std::array<char, 512>& catalog,
                                                  std::array<char, 512>& zipPath,
                                                  std::array<char, 512>& folderPath)
        {
            switch (mode)
            {
                case PluginImportMode::eGit:
                    return gitUrl;
                case PluginImportMode::eCatalog:
                    return catalog;
                case PluginImportMode::eZip:
                    return zipPath;
                case PluginImportMode::eFolder:
                case PluginImportMode::eNone:
                default:
                    return folderPath;
            }
        }

        struct PluginImportTaskResult
        {
            bool        imported {false};
            bool        catalogFetched {false};
            std::string importedId;
            std::string status;
            std::vector<CatalogPluginEntry> catalogEntries;
        };

        PluginImportTaskResult runPluginImportTask(PluginImportMode mode,
                                                   std::filesystem::path projectRoot,
                                                   std::filesystem::path pluginsDir,
                                                   std::string           value)
        {
            PluginImportTaskResult result {};
            const auto beforeIds = lockedPluginIds(projectRoot);
            switch (mode)
            {
                case PluginImportMode::eGit:
                    result.imported = importPluginFromGit(projectRoot, pluginsDir, value, result.status, &result.importedId);
                    break;
                case PluginImportMode::eCatalog:
                    result.catalogFetched =
                        fetchPluginCatalog(projectRoot, value, result.catalogEntries, result.status);
                    break;
                case PluginImportMode::eZip:
                    result.imported =
                        importPluginFromZip(projectRoot, pluginsDir, std::filesystem::path {value}, result.status, &result.importedId);
                    break;
                case PluginImportMode::eFolder:
                    result.imported =
                        importPluginFromFolder(projectRoot, pluginsDir, std::filesystem::path {value}, result.status, &result.importedId);
                    break;
                case PluginImportMode::eNone:
                default:
                    result.status = "No plugin import mode selected.";
                    break;
            }
            if (result.imported && result.importedId.empty())
            {
                const auto afterIds = lockedPluginIds(projectRoot);
                for (const auto& id : afterIds)
                {
                    if (std::find(beforeIds.begin(), beforeIds.end(), id) == beforeIds.end())
                    {
                        result.importedId = id;
                        break;
                    }
                }
            }
            return result;
        }

    } // namespace

    void EditorApp::drawProjectSettingsPopup(EditorContext& ctx)
    {
        static int                      selectedPage = 0;
        static std::vector<std::string> s_EnabledPlugins;
        static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> s_PluginConfigValues;
        static std::array<char, 512>    s_PluginGitUrl {};
        static std::array<char, 512>    s_PluginCatalog {};
        static std::array<char, 512>    s_PluginZipPath {};
        static std::array<char, 512>    s_PluginFolderPath {};
        static std::string              s_PluginImportStatus;
        static PluginImportMode         s_PluginImportMode {PluginImportMode::eNone};
        static bool                     s_OpenPluginImportDialog {false};
        static std::future<PluginImportTaskResult> s_PluginImportFuture;
        static bool                              s_PluginImportInFlight {false};
        static bool                              s_OpenPluginImportLoading {false};
        static bool                              s_ClosePluginImportLoading {false};
        static std::vector<CatalogPluginEntry>   s_PluginCatalogEntries;
        static std::string                       s_PluginCatalogStatus;
        static std::optional<vultra::PluginManifest> s_PendingRemovePlugin;
        static bool                              s_OpenRemovePluginDialog {false};
        if (ctx.state.projectSettingsOpen)
        {
            setBuffer(m_ProjectNameBuffer, ctx.state.currentProjectName);
            setBuffer(m_ProjectAssetRootBuffer, ctx.state.currentAssetRoot);
            setBuffer(m_ProjectDefaultSceneBuffer, ctx.state.currentDefaultScene);
            setBuffer(m_ProjectEditingRenderGraphBuffer, ctx.state.currentEditingRenderGraph);
            s_EnabledPlugins.clear();
            s_PluginConfigValues.clear();
            s_PluginImportStatus.clear();
            if (auto project = loadVProject(ctx.state.currentProject); project.has_value())
            {
                s_EnabledPlugins = project->enabledPlugins;
                s_PluginConfigValues = project->pluginConfigValues;
            }
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
            const auto pluginsDir = localPluginInstallDir(ctx.state.currentProject, ctx.state.currentAssetRoot);
            const auto pluginDirs = pluginDiscoveryDirs(ctx.state.currentProject, ctx.state.currentAssetRoot);
            ui::drawInfoRegion(vultra::tr("projectSettings.plugins.info"));

            ImGui::PushID("PluginImport");
            if (ImGui::Button(ICON_MDI_PLUS, ImVec2 {vultra::ui::dp(32.0f), 0.0f}))
                ImGui::OpenPopup("PluginImportMenu");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "%s", trText("projectSettings.plugins.importHeader", "Import plugin").c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s",
                                trText("projectSettings.plugins.importHint",
                                       "Import from Git, catalog, ZIP, or folder.").c_str());

            if (ImGui::BeginPopup("PluginImportMenu"))
            {
                ImGui::TextDisabled(
                    "%s", trText("projectSettings.plugins.chooseImportSource", "Choose import source").c_str());
                ImGui::Separator();
                if (ImGui::MenuItem((std::string {ICON_MDI_SOURCE_BRANCH "  "} +
                                     trText("projectSettings.plugins.importGit", "Git URL"))
                                        .c_str()))
                {
                    s_PluginImportMode       = PluginImportMode::eGit;
                    s_OpenPluginImportDialog = true;
                    s_PluginImportStatus.clear();
                }
                if (ImGui::MenuItem((std::string {ICON_MDI_FORMAT_LIST_BULLETED "  "} +
                                     trText("projectSettings.plugins.importCatalog", "Catalog"))
                                        .c_str()))
                {
                    if (bufferString(s_PluginCatalog).empty())
                        setBuffer(s_PluginCatalog, defaultPluginCatalogUrl());
                    s_PluginImportMode       = PluginImportMode::eCatalog;
                    s_OpenPluginImportDialog = true;
                    s_PluginImportStatus.clear();
                }
                if (ImGui::MenuItem((std::string {ICON_MDI_ARCHIVE_ARROW_DOWN_OUTLINE "  "} +
                                     trText("projectSettings.plugins.importZip", "ZIP"))
                                        .c_str()))
                {
                    s_PluginImportMode       = PluginImportMode::eZip;
                    s_OpenPluginImportDialog = true;
                    s_PluginImportStatus.clear();
                }
                if (ImGui::MenuItem((std::string {ICON_MDI_FOLDER_DOWNLOAD_OUTLINE "  "} +
                                     trText("projectSettings.plugins.importFolder", "Folder"))
                                        .c_str()))
                {
                    s_PluginImportMode       = PluginImportMode::eFolder;
                    s_OpenPluginImportDialog = true;
                    s_PluginImportStatus.clear();
                }
                ImGui::EndPopup();
            }

            const auto importDialogTitle = trText("projectSettings.plugins.importDialogTitle", "Import Plugin");
            if (s_OpenPluginImportDialog)
            {
                ImGui::OpenPopup(importDialogTitle.c_str());
                s_OpenPluginImportDialog = false;
            }

            ImGui::SetNextWindowSize(ImVec2 {vultra::ui::dp(520.0f), 0.0f}, ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal(importDialogTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextDisabled("%s", pluginImportTitle(s_PluginImportMode).c_str());
                ImGui::Spacing();

                auto& importBuffer = pluginImportBuffer(s_PluginImportMode,
                                                        s_PluginGitUrl,
                                                        s_PluginCatalog,
                                                        s_PluginZipPath,
                                                        s_PluginFolderPath);
                ImGui::SetNextItemWidth(vultra::ui::dp(480.0f));
                const auto hint = pluginImportHint(s_PluginImportMode);
                ImGui::InputTextWithHint("##PluginImportValue",
                                         hint.c_str(),
                                         importBuffer.data(),
                                         importBuffer.size());

                if (!s_PluginImportStatus.empty())
                    ImGui::TextWrapped("%s", s_PluginImportStatus.c_str());

                ImGui::Spacing();
                const bool canImport = s_PluginImportMode != PluginImportMode::eNone;
                if (!canImport)
                    ImGui::BeginDisabled();
                const std::string actionLabel = s_PluginImportMode == PluginImportMode::eCatalog ?
                                                    trText("projectSettings.plugins.fetchCatalog", "Fetch") :
                                                    std::string {vultra::tr("common.import")};
                if (ImGui::Button(actionLabel.c_str(), ImVec2 {vultra::ui::dp(96.0f), 0.0f}))
                {
                    if (!s_PluginImportInFlight)
                    {
                        const auto value = bufferString(importBuffer);
                        s_PluginImportStatus = s_PluginImportMode == PluginImportMode::eCatalog ?
                                                   trText("projectSettings.plugins.catalogLoading",
                                                          "Loading plugin catalog...") :
                                                   trText("projectSettings.plugins.importLoading",
                                                          "Importing plugin...");
                        s_PluginImportFuture = std::async(std::launch::async,
                                                          runPluginImportTask,
                                                          s_PluginImportMode,
                                                          ctx.state.currentProject,
                                                          pluginsDir,
                                                          value);
                        s_PluginImportInFlight     = true;
                        s_OpenPluginImportLoading  = true;
                        ctx.state.statusMessage    = s_PluginImportStatus;
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!canImport)
                    ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(vultra::tr("common.cancel"), ImVec2 {vultra::ui::dp(96.0f), 0.0f}))
                    ImGui::CloseCurrentPopup();

                ImGui::EndPopup();
            }

            const auto loadingTitle = trText("projectSettings.plugins.importLoadingTitle", "Importing Plugin");
            if (s_OpenPluginImportLoading)
            {
                ImGui::OpenPopup(loadingTitle.c_str());
                s_OpenPluginImportLoading = false;
            }

            if (s_PluginImportInFlight && s_PluginImportFuture.valid() &&
                s_PluginImportFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto result = s_PluginImportFuture.get();
                s_PluginImportInFlight = false;
                s_PluginImportStatus   = std::move(result.status);
                ctx.state.statusMessage = s_PluginImportStatus;
                if (result.catalogFetched)
                {
                    s_PluginCatalogEntries = std::move(result.catalogEntries);
                    s_PluginCatalogStatus  = s_PluginImportStatus;
                }
                if (result.imported)
                {
                    if (!result.importedId.empty())
                    {
                        s_EnabledPlugins.erase(
                            std::remove(s_EnabledPlugins.begin(), s_EnabledPlugins.end(), result.importedId),
                            s_EnabledPlugins.end());
                    }
                    projectSettingsChanged = true;
                }
                s_ClosePluginImportLoading = true;
            }

            ImGui::SetNextWindowSize(ImVec2 {vultra::ui::dp(360.0f), 0.0f}, ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal(loadingTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (s_ClosePluginImportLoading)
                {
                    s_ClosePluginImportLoading = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::TextUnformatted(s_PluginImportStatus.empty() ?
                                           trText("projectSettings.plugins.importLoading", "Importing plugin...").c_str() :
                                           s_PluginImportStatus.c_str());
                ImGui::Spacing();
                ImGui::ProgressBar(-1.0f, ImVec2 {vultra::ui::dp(320.0f), 0.0f});
                if (!s_PluginImportInFlight)
                {
                    ImGui::Spacing();
                    if (ImGui::Button(vultra::tr("common.close"), ImVec2 {vultra::ui::dp(96.0f), 0.0f}))
                        ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ImGui::Separator();

            const auto lockedIds = lockedPluginIds(ctx.state.currentProject);
            const auto manifests = discoverProjectPlugins(pluginDirs, lockedIds);
            if (!s_PluginCatalogEntries.empty())
            {
                ImGui::TextDisabled("%s", trText("projectSettings.plugins.catalogResults", "Catalog").c_str());
                if (!s_PluginCatalogStatus.empty())
                    ImGui::TextWrapped("%s", s_PluginCatalogStatus.c_str());
                ImGui::BeginChild("PluginCatalogResults",
                                  ImVec2 {0.0f, vultra::ui::dp(180.0f)},
                                  true,
                                  ImGuiWindowFlags_AlwaysVerticalScrollbar);
                for (const auto& entry : s_PluginCatalogEntries)
                {
                    ImGui::PushID(entry.gitUrl.c_str());
                    const bool installed = !entry.id.empty() &&
                        std::find(lockedIds.begin(), lockedIds.end(), entry.id) != lockedIds.end();
                    ImGui::TextUnformatted(entry.name.empty() ? entry.id.c_str() : entry.name.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("v%s%s%s",
                                        entry.version.empty() ? "?" : entry.version.c_str(),
                                        entry.author.empty() ? "" : "  \xc2\xb7  ",
                                        entry.author.c_str());
                    if (!entry.description.empty())
                        ImGui::TextWrapped("%s", entry.description.c_str());
                    if (!entry.repository.empty())
                        ImGui::TextDisabled("%s", entry.repository.c_str());

                    const bool disableImport = installed || s_PluginImportInFlight;
                    if (disableImport)
                        ImGui::BeginDisabled();
                    if (ImGui::Button(installed ?
                                          trText("projectSettings.plugins.installed", "Installed").c_str() :
                                          vultra::tr("common.import"),
                                      ImVec2 {vultra::ui::dp(96.0f), 0.0f}))
                    {
                        s_PluginImportStatus =
                            trText("projectSettings.plugins.importLoading", "Importing plugin...");
                        s_PluginImportFuture = std::async(std::launch::async,
                                                          runPluginImportTask,
                                                          PluginImportMode::eGit,
                                                          ctx.state.currentProject,
                                                          pluginsDir,
                                                          entry.gitUrl);
                        s_PluginImportInFlight    = true;
                        s_OpenPluginImportLoading = true;
                        ctx.state.statusMessage   = s_PluginImportStatus;
                    }
                    if (disableImport)
                        ImGui::EndDisabled();
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::Separator();
            }

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
                        const auto displayName = manifest.name.empty() ? manifest.id : manifest.name;
                        bool loadedNow = true;
                        if (auto* plugins = ctx.services ? ctx.services->tryGet<vultra::IPluginService>() : nullptr)
                            loadedNow = plugins->loadPlugin(manifest);
                        if (loadedNow)
                        {
                            s_EnabledPlugins.push_back(manifest.id);
                            ctx.state.statusMessage =
                                vultra::trf("projectSettings.plugins.enabledStatus", displayName, manifest.id);
                        }
                        else
                        {
                            ctx.state.statusMessage =
                                vultra::trf("projectSettings.plugins.enableFailed", displayName, manifest.id);
                        }
                    }
                    else if (!enabled && wasEnabled)
                    {
                        std::string status;
                        auto* plugins = ctx.services ? ctx.services->tryGet<vultra::IPluginService>() : nullptr;
                        const auto displayName = manifest.name.empty() ? manifest.id : manifest.name;
                        if (unloadPluginIfLoaded(plugins, manifest, status))
                        {
                            s_EnabledPlugins.erase(
                                std::remove(s_EnabledPlugins.begin(), s_EnabledPlugins.end(), manifest.id),
                                s_EnabledPlugins.end());
                            ctx.state.statusMessage =
                                status.empty() ?
                                    vultra::trf("projectSettings.plugins.disabledStatus", displayName, manifest.id) :
                                    status;
                        }
                        else
                        {
                            ctx.state.statusMessage = status;
                        }
                    }
                }
                if (!supported)
                    ImGui::EndDisabled();

                ImGui::SameLine();
                ImGui::TextDisabled("v%s%s%s",
                                    manifest.version.empty() ? "?" : manifest.version.c_str(),
                                    manifest.author.empty() ? "" : "  \xc2\xb7  ",
                                    manifest.author.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MDI_DELETE_OUTLINE))
                {
                    s_PendingRemovePlugin     = manifest;
                    s_OpenRemovePluginDialog = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", trText("projectSettings.plugins.remove", "Remove").c_str());

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
                if (!manifest.configParams.empty())
                {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Configuration");
                    ImGui::Indent();
                    if (!wasEnabled)
                        ImGui::BeginDisabled();
                    for (const auto& param : manifest.configParams)
                    {
                        if (drawPluginConfigParam(s_PluginConfigValues, manifest, param))
                            projectSettingsChanged = true;
                    }
                    if (!wasEnabled)
                        ImGui::EndDisabled();
                    ImGui::Unindent();
                }
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

            const auto removeDialogTitle = trText("projectSettings.plugins.removeDialogTitle", "Remove Plugin");
            if (s_OpenRemovePluginDialog)
            {
                ImGui::OpenPopup(removeDialogTitle.c_str());
                s_OpenRemovePluginDialog = false;
            }
            if (ImGui::BeginPopupModal(removeDialogTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (s_PendingRemovePlugin.has_value())
                {
                    const auto& pending = *s_PendingRemovePlugin;
                    ImGui::TextWrapped("%s",
                                       vultra::trf("projectSettings.plugins.removeConfirm",
                                                   pending.name.empty() ? pending.id : pending.name)
                                           .c_str());
                    ImGui::TextDisabled("%s", pending.directory.generic_string().c_str());
                    bool loaded = false;
                    if (auto* plugins = ctx.services ? ctx.services->tryGet<vultra::IPluginService>() : nullptr)
                        loaded = plugins->isLoaded(pending.id);
                    if (loaded)
                        ImGui::TextWrapped("%s",
                                           trText("projectSettings.plugins.removeLoadedNote",
                                                  "This plugin is loaded and will be unloaded before removal.")
                                               .c_str());

                    if (ImGui::Button(trText("projectSettings.plugins.remove", "Remove").c_str(),
                                      ImVec2 {vultra::ui::dp(96.0f), 0.0f}))
                    {
                        std::string status;
                        auto* plugins = ctx.services ? ctx.services->tryGet<vultra::IPluginService>() : nullptr;
                        if (!unloadPluginIfLoaded(plugins, pending, status))
                        {
                            s_PluginImportStatus = status;
                            ctx.state.statusMessage = status;
                        }
                        else if (removePluginInstall(ctx.state.currentProject,
                                                     ctx.state.currentAssetRoot,
                                                     pending,
                                                     status))
                        {
                            s_EnabledPlugins.erase(
                                std::remove(s_EnabledPlugins.begin(), s_EnabledPlugins.end(), pending.id),
                                s_EnabledPlugins.end());
                            s_PluginConfigValues.erase(pending.id);
                            s_PluginImportStatus = status;
                            ctx.state.statusMessage = status;
                            projectSettingsChanged = true;
                            s_PendingRemovePlugin.reset();
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            s_PluginImportStatus = status;
                            ctx.state.statusMessage = status;
                        }
                    }
                    ImGui::SameLine();
                }
                if (ImGui::Button(vultra::tr("common.cancel"), ImVec2 {vultra::ui::dp(96.0f), 0.0f}))
                {
                    s_PendingRemovePlugin.reset();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
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
            s_PluginConfigValues.clear();
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
            std::unordered_map<std::string, std::unordered_map<std::string, std::string>> projectPluginConfigValues;
            std::unordered_map<std::string, std::string>                                 envPluginConfigValues;
            splitPluginConfigValuesForSave(pluginDiscoveryDirs(ctx.state.currentProject, ctx.state.currentAssetRoot),
                                           s_PluginConfigValues,
                                           projectPluginConfigValues,
                                           envPluginConfigValues);

            VProject project {
                .projectDir         = ctx.state.currentProject,
                .name               = ctx.state.currentProjectName,
                .assetRoot          = ctx.state.currentAssetRoot,
                .defaultScene       = ctx.state.currentDefaultScene,
                .buildScenes        = normalizedBuildScenes(ctx.state.currentDefaultScene, ctx.state.currentBuildScenes),
                .editingRenderGraph = ctx.state.currentEditingRenderGraph,
                .enabledPlugins     = s_EnabledPlugins,
                .pluginConfigValues = projectPluginConfigValues,
            };
            std::string error;
            if (!saveProjectEnvValues(project.projectDir, envPluginConfigValues, &error))
                ctx.state.statusMessage = vultra::trf("projectSettings.status.saveFailed", error);
            else if (saveVProject(project, &error))
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
