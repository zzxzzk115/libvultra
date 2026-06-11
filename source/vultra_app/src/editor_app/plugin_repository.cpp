#include "editor_app/plugin_repository.hpp"

#include <vultra/core/base/common_context.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#    include <windows.h>
#    include <winhttp.h>
#endif

namespace vultra_app::plugins
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr const char* kLockFileName = "vultra.plugins.lock";

        bool isHttpUrl(std::string_view value)
        {
            return value.starts_with("https://") || value.starts_with("http://");
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

        std::string quoteCommandArg(const fs::path& path) { return quoteCommandArg(path.generic_string()); }

        std::vector<fs::path> splitPathList(const std::string& value)
        {
            std::vector<fs::path> result;
#if defined(_WIN32)
            constexpr char separator = ';';
#else
            constexpr char separator = ':';
#endif
            std::size_t start = 0;
            while (start <= value.size())
            {
                const auto end  = value.find(separator, start);
                const auto item = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!item.empty())
                    result.emplace_back(item);
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            return result;
        }

        std::optional<fs::path> resolveExecutablePath(const std::string& executableName)
        {
            std::error_code ec;
            fs::path        direct {executableName};
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
                std::vector<fs::path> candidates {
                    fs::path {"C:/Program Files/Git/cmd/git.exe"},
                    fs::path {"C:/Program Files/Git/bin/git.exe"},
                    fs::path {"C:/Program Files (x86)/Git/cmd/git.exe"},
                    fs::path {"C:/Program Files (x86)/Git/bin/git.exe"},
                };
                // Git for Windows also installs per-user without touching PATH.
                if (const char* localAppData = std::getenv("LOCALAPPDATA"); localAppData != nullptr)
                {
                    candidates.emplace_back(fs::path {localAppData} / "Programs/Git/cmd/git.exe");
                    candidates.emplace_back(fs::path {localAppData} / "Programs/Git/bin/git.exe");
                }
                for (const auto& candidate : candidates)
                {
                    if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                        return candidate;
                }
            }
#endif

            return std::nullopt;
        }

        std::optional<fs::path> gitCommandPath()
        {
#if defined(_WIN32)
            return resolveExecutablePath("git.exe");
#else
            return resolveExecutablePath("git");
#endif
        }

#if defined(_WIN32)
        std::wstring widenUtf8(std::string_view text)
        {
            if (text.empty())
                return {};
            const int size =
                MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (size <= 0)
                return {};
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
            return result;
        }

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

            std::wstring out         = L"\"";
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

        bool runProcess(const fs::path& executable, const std::vector<std::string>& args, std::string& status)
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
        bool runProcess(const fs::path& executable, const std::vector<std::string>& args, std::string& status)
        {
            std::ostringstream cmd;
            cmd << quoteCommandArg(executable);
            for (const auto& arg : args)
                cmd << " " << quoteCommandArg(arg);
            if (std::system(cmd.str().c_str()) != 0)
            {
                status = "Process failed.";
                return false;
            }
            return true;
        }
#endif

        bool runGit(const std::vector<std::string>& args, std::string& status)
        {
            const auto git = gitCommandPath();
            if (!git.has_value())
            {
                status = "Git executable was not found by the editor process. Add Git to PATH or install it in "
                         "C:/Program Files/Git.";
                return false;
            }
            return runProcess(*git, args, status);
        }

#if defined(_WIN32)
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
            explicit       operator bool() const { return handle != nullptr; }
        };

        bool downloadTextToFile(const std::string& url, const fs::path& destination, std::string& status)
        {
            std::error_code ec;
            fs::create_directories(destination.parent_path(), ec);
            if (ec)
            {
                status = "Failed to create download cache: " + ec.message();
                return false;
            }

            std::wstring   wideUrl = widenUtf8(url);
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

            const DWORD   flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
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

            if (!WinHttpSendRequest(
                    request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
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
        bool downloadTextToFile(const std::string&, const fs::path&, std::string& status)
        {
            status = "Remote plugin catalogs are not implemented on this platform yet.";
            return false;
        }
#endif

        std::string safeCacheName(std::string_view text)
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

        std::string sanitizeNameSegment(std::string_view text)
        {
            std::string out;
            for (const char ch : text)
            {
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.')
                    out.push_back(ch);
            }
            return out;
        }

        // Human-readable cache name for a repository-ish URL: "<owner>-<repo>".
        //   https://github.com/zzxzzk115/vultra-plugin-streamline.git -> zzxzzk115-vultra-plugin-streamline
        //   https://raw.githubusercontent.com/zzxzzk115/vultra-plugins/main/plugins.json -> zzxzzk115-vultra-plugins
        // Falls back to a hashed safe name when the URL has no owner/repo shape.
        std::string readableRepoName(std::string_view url)
        {
            std::string_view rest = url;
            if (const auto pos = rest.find("://"); pos != std::string_view::npos)
                rest = rest.substr(pos + 3);

            std::vector<std::string_view> segments;
            std::size_t                   start = 0;
            while (start <= rest.size())
            {
                const auto end     = rest.find_first_of("/?#", start);
                const auto segment = rest.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                                                      : end - start);
                if (!segment.empty())
                    segments.push_back(segment);
                if (end == std::string_view::npos || rest[end] != '/')
                    break;
                start = end + 1;
            }

            // segments[0] is the host; owner/repo follow.
            if (segments.size() >= 3)
            {
                std::string repo {segments[2]};
                if (repo.ends_with(".git"))
                    repo.resize(repo.size() - 4);
                const auto owner = sanitizeNameSegment(segments[1]);
                const auto name  = sanitizeNameSegment(repo);
                if (!owner.empty() && !name.empty())
                    return owner + "-" + name;
            }
            return safeCacheName(url);
        }

        // Copy a plugin payload, excluding version-control internals (.git).
        bool copyPluginPayload(const fs::path& source, const fs::path& destination, std::string& error)
        {
            std::error_code ec;
            fs::remove_all(destination, ec);
            if (ec)
            {
                error = "failed to replace existing plugin folder: " + ec.message();
                return false;
            }
            fs::create_directories(destination, ec);
            if (ec)
            {
                error = "failed to create plugin folder: " + ec.message();
                return false;
            }

            for (const auto& entry : fs::directory_iterator(source, ec))
            {
                if (ec)
                    break;
                if (entry.path().filename() == ".git")
                    continue;
                fs::copy(entry.path(),
                         destination / entry.path().filename(),
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                         ec);
                if (ec)
                {
                    error = "failed to copy plugin files: " + ec.message();
                    return false;
                }
            }
            if (ec)
            {
                error = "failed to enumerate plugin files: " + ec.message();
                return false;
            }
            return true;
        }

        fs::path gitCacheRoot(const fs::path& projectRoot) { return managedRoot(projectRoot) / ".cache"; }

        fs::path absoluteLockDir(const fs::path& projectRoot, const std::string& directory)
        {
            fs::path path {directory};
            if (!path.is_absolute())
                path = projectRoot / path;
            return path.lexically_normal();
        }

        std::optional<fs::path> findPluginRoot(const fs::path& root)
        {
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

        std::uintmax_t directoryFileCount(const fs::path& dir)
        {
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

        std::uintmax_t directoryByteSize(const fs::path& dir)
        {
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

        std::string textFileHash(const fs::path& path)
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

        nlohmann::json loadLock(const fs::path& lockPath)
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

        nlohmann::json lockEntryFor(const fs::path&               projectRoot,
                                    const vultra::PluginManifest& manifest,
                                    nlohmann::json                base)
        {
            std::error_code ec;
            base["id"]           = manifest.id;
            base["name"]         = manifest.name;
            base["version"]      = manifest.version;
            base["directory"]    = fs::relative(manifest.directory, projectRoot, ec).generic_string();
            base["manifestHash"] = textFileHash(manifest.manifestPath);
            base["fileCount"]    = directoryFileCount(manifest.directory);
            base["byteSize"]     = directoryByteSize(manifest.directory);
            if (!base.contains("source"))
                base["source"] = nlohmann::json {{"type", "unknown"}};
            return base;
        }

        // Rebuild vultra.plugins.lock: a fresh import is authoritative for its id; managed (git)
        // entries are kept (refreshed from disk when their version directory exists -- a missing
        // one stays restorable from its source); local entries are rescanned from the install dir.
        void saveLock(const fs::path&                              projectRoot,
                      const fs::path&                              localPluginsDir,
                      const std::optional<vultra::PluginManifest>& importedManifest,
                      const nlohmann::json&                        importedSource,
                      const std::vector<std::string>&              excludedIds = {})
        {
            std::error_code ec;
            fs::create_directories(projectRoot, ec);
            const auto lockPath = projectRoot / kLockFileName;

            const auto excluded = [&](const std::string& id) {
                return std::find(excludedIds.begin(), excludedIds.end(), id) != excludedIds.end();
            };

            nlohmann::json                  pluginsJson = nlohmann::json::array();
            std::unordered_set<std::string> written;

            if (importedManifest.has_value() && !excluded(importedManifest->id))
            {
                nlohmann::json entry = nlohmann::json::object();
                entry["source"]      = importedSource;
                pluginsJson.push_back(lockEntryFor(projectRoot, *importedManifest, std::move(entry)));
                written.insert(importedManifest->id);
            }

            const auto oldLock = loadLock(lockPath);
            for (const auto& item : oldLock.value("plugins", nlohmann::json::array()))
            {
                const auto id = item.value("id", std::string {});
                if (id.empty() || excluded(id) || written.contains(id))
                    continue;
                const auto source = item.value("source", nlohmann::json::object());
                if (source.value("type", std::string {}) != "git")
                    continue; // local entries are rescanned below

                const auto dir = absoluteLockDir(projectRoot, item.value("directory", std::string {}));
                if (auto manifest = vultra::loadPluginManifest(dir / vultra::kPluginManifestFile); manifest.has_value())
                    pluginsJson.push_back(lockEntryFor(projectRoot, *manifest, item));
                else
                    pluginsJson.push_back(item); // restorable from source; keep as recorded
                written.insert(id);
            }

            for (const auto& manifest : discoverProjectPlugins({localPluginsDir}))
            {
                if (excluded(manifest.id) || written.contains(manifest.id))
                    continue;
                nlohmann::json base = nlohmann::json::object();
                for (const auto& item : oldLock.value("plugins", nlohmann::json::array()))
                {
                    if (item.value("id", std::string {}) == manifest.id)
                    {
                        base = item;
                        break;
                    }
                }
                pluginsJson.push_back(lockEntryFor(projectRoot, manifest, std::move(base)));
                written.insert(manifest.id);
            }

            const nlohmann::json lock = {{"schemaVersion", 1}, {"plugins", std::move(pluginsJson)}};
            std::ofstream        out(lockPath);
            if (out)
                out << lock.dump(2) << "\n";
        }

        bool copyPluginDirectory(const fs::path& source, const fs::path& destination, std::string& error)
        {
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

            fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = "failed to copy plugin files: " + ec.message();
                return false;
            }
            return true;
        }

        ImportResult installLocalPluginFromRoot(const fs::path&       projectRoot,
                                                const fs::path&       pluginsDir,
                                                const fs::path&       sourceRoot,
                                                const nlohmann::json& sourceInfo)
        {
            ImportResult result;
            std::string  manifestError;
            auto manifest = vultra::loadPluginManifest(sourceRoot / vultra::kPluginManifestFile, &manifestError);
            if (!manifest.has_value())
            {
                result.status = manifestError.empty() ? "not a Vultra plugin folder" : manifestError;
                return result;
            }

            const auto  destination = (pluginsDir / sourceRoot.filename()).lexically_normal();
            std::string copyError;
            if (!copyPluginDirectory(sourceRoot, destination, copyError))
            {
                result.status = copyError;
                return result;
            }

            auto installedManifest =
                vultra::loadPluginManifest(destination / vultra::kPluginManifestFile, &manifestError);
            if (!installedManifest.has_value())
            {
                result.status =
                    manifestError.empty() ? "plugin was copied but its manifest could not be read" : manifestError;
                return result;
            }

            saveLock(projectRoot, pluginsDir, installedManifest, sourceInfo);
            result.ok          = true;
            result.installedId = installedManifest->id;
            result.status = "Installed plugin '" + installedManifest->name + "' (" + installedManifest->id + ").";
            return result;
        }

        // Bring a managed git cache to the requested state. Returns false with a status on hard
        // failure; cacheWarning collects soft fallbacks (e.g. offline but usable cache).
        bool syncGitCache(const fs::path&    cacheDir,
                          const std::string& url,
                          const std::string& ref,
                          std::string&       cacheWarning,
                          std::string&       status)
        {
            std::error_code ec;
            const bool      isGitCheckout = fs::exists(cacheDir / ".git", ec);

            if (!isGitCheckout)
            {
                // A non-git cache folder (e.g. a release payload) can be reused as-is, but cannot
                // be switched to a specific ref. Re-clone in that case.
                if (fs::exists(cacheDir, ec) && findPluginRoot(cacheDir).has_value() && ref.empty())
                {
                    cacheWarning = " Using cached plugin files.";
                    return true;
                }
                fs::remove_all(cacheDir, ec);

                std::vector<std::string> args {"clone", "--depth", "1"};
                if (!ref.empty())
                {
                    args.emplace_back("--branch");
                    args.push_back(ref);
                }
                args.push_back(url);
                args.push_back(cacheDir.generic_string());
                return runGit(args, status);
            }

            const auto cache = cacheDir.generic_string();
            if (ref.empty())
            {
                std::string pullStatus;
                if (!runGit({"-C", cache, "pull", "--ff-only"}, pullStatus))
                    cacheWarning = " Using cached checkout because update failed: " + pullStatus;
                return true;
            }

            // Pin to the requested release tag: fetch it (force handles shallow clones and moved
            // tags), then check it out detached. If fetching fails (offline), a local checkout of
            // the same ref still succeeds.
            std::string fetchStatus;
            const bool  fetched = runGit(
                {"-C", cache, "fetch", "--depth", "1", "--force", "origin", "tag", ref}, fetchStatus);
            if (!runGit({"-C", cache, "checkout", "--force", ref}, status))
            {
                if (!fetched && fetchStatus != status)
                    status += " (tag fetch also failed: " + fetchStatus + ")";
                return false;
            }
            if (!fetched)
                cacheWarning = " Using locally cached ref because fetch failed.";
            return true;
        }

        std::vector<CatalogVersion> parseCatalogVersions(const nlohmann::json& plugin)
        {
            std::vector<CatalogVersion> versions;

            const auto parseSource = [](const nlohmann::json& container, CatalogVersion& out) {
                const auto source = container.value("source", nlohmann::json::object());
                if (source.value("type", std::string {}) != "git")
                    return false;
                out.gitUrl = source.value("url", std::string {});
                out.gitRef = source.value("ref", std::string {});
                return !out.gitUrl.empty();
            };

            // Schema v2: versions[] (newest first).
            for (const auto& item : plugin.value("versions", nlohmann::json::array()))
            {
                if (!item.is_object())
                    continue;
                CatalogVersion version;
                version.version = item.value("version", std::string {});
                version.notes   = item.value("notes", std::string {});
                if (parseSource(item, version))
                    versions.push_back(std::move(version));
            }

            // Schema v1 fallback (also covers v2 entries whose versions[] was empty/invalid).
            if (versions.empty())
            {
                CatalogVersion version;
                version.version = plugin.value("version", std::string {});
                if (parseSource(plugin, version))
                    versions.push_back(std::move(version));
            }
            return versions;
        }
    } // namespace

    std::string defaultCatalogUrl()
    {
        return "https://raw.githubusercontent.com/zzxzzk115/vultra-plugins/main/plugins.json";
    }

    std::filesystem::path localInstallDir(const std::filesystem::path& projectRoot, const std::string& assetRoot)
    {
        return (projectRoot / assetRoot / "plugins").lexically_normal();
    }

    std::filesystem::path managedRoot(const std::filesystem::path& projectRoot)
    {
        return (projectRoot / ".vultra" / "plugins").lexically_normal();
    }

    std::vector<std::filesystem::path> discoveryDirs(const std::filesystem::path& projectRoot,
                                                     const std::string&           assetRoot)
    {
        std::vector<std::filesystem::path> dirs {localInstallDir(projectRoot, assetRoot)};
        const auto                         lock = loadLock(projectRoot / kLockFileName);
        for (const auto& item : lock.value("plugins", nlohmann::json::array()))
        {
            const auto directory = item.value("directory", std::string {});
            if (directory.empty())
                continue;
            auto path = absoluteLockDir(projectRoot, directory);
            if (std::find(dirs.begin(), dirs.end(), path) == dirs.end())
                dirs.push_back(std::move(path));
        }
        return dirs;
    }

    std::vector<vultra::PluginManifest> discoverProjectPlugins(const std::vector<std::filesystem::path>& dirs)
    {
        std::vector<vultra::PluginManifest> result;
        std::unordered_set<std::string>     seen;
        for (const auto& dir : dirs)
        {
            for (auto manifest : vultra::discoverPlugins(dir))
            {
                if (manifest.id.empty() || !seen.insert(manifest.id).second)
                    continue;
                result.push_back(std::move(manifest));
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        return result;
    }

    std::vector<std::string> lockedPluginIds(const std::filesystem::path& projectRoot)
    {
        std::vector<std::string> ids;
        const auto               lock = loadLock(projectRoot / kLockFileName);
        for (const auto& item : lock.value("plugins", nlohmann::json::array()))
        {
            const auto id = item.value("id", std::string {});
            if (!id.empty())
                ids.push_back(id);
        }
        return ids;
    }

    std::optional<InstalledSource> installedSource(const std::filesystem::path& projectRoot,
                                                   const std::string&           pluginId)
    {
        const auto lock = loadLock(projectRoot / kLockFileName);
        for (const auto& item : lock.value("plugins", nlohmann::json::array()))
        {
            if (item.value("id", std::string {}) != pluginId)
                continue;
            const auto source = item.value("source", nlohmann::json::object());
            return InstalledSource {
                .type = source.value("type", std::string {"unknown"}),
                .url  = source.value("url", std::string {}),
                .ref  = source.value("ref", std::string {}),
            };
        }
        return std::nullopt;
    }

    void refreshLock(const std::filesystem::path&    projectRoot,
                     const std::string&              assetRoot,
                     const std::vector<std::string>& excludedIds)
    {
        saveLock(projectRoot, localInstallDir(projectRoot, assetRoot), std::nullopt, nlohmann::json {}, excludedIds);
    }

    void restoreLockedPlugins(const std::filesystem::path& projectRoot)
    {
        namespace fs = std::filesystem;
        const auto lock = loadLock(projectRoot / kLockFileName);
        for (const auto& item : lock.value("plugins", nlohmann::json::array()))
        {
            const auto source = item.value("source", nlohmann::json::object());
            if (source.value("type", std::string {}) != "git")
                continue;
            const auto url       = source.value("url", std::string {});
            const auto directory = item.value("directory", std::string {});
            if (url.empty() || directory.empty())
                continue;

            const auto      versionDir = absoluteLockDir(projectRoot, directory);
            std::error_code ec;
            if (fs::exists(versionDir / vultra::kPluginManifestFile, ec))
                continue;

            const auto  ref      = source.value("ref", std::string {});
            const auto  cacheDir = (gitCacheRoot(projectRoot) / readableRepoName(url)).lexically_normal();
            std::string cacheWarning;
            std::string status;
            if (!syncGitCache(cacheDir, url, ref, cacheWarning, status))
            {
                VULTRA_CLIENT_WARN("[PluginRepository] Cannot restore locked plugin '{}': {}",
                                   item.value("id", std::string {"?"}),
                                   status);
                continue;
            }
            const auto root = findPluginRoot(cacheDir);
            if (!root.has_value())
                continue;
            std::string copyError;
            if (!copyPluginPayload(*root, versionDir, copyError))
                VULTRA_CLIENT_WARN("[PluginRepository] Cannot restore locked plugin '{}': {}",
                                   item.value("id", std::string {"?"}),
                                   copyError);
        }
    }

    bool projectNeedsRelaunchForPlugins(const std::filesystem::path&    projectRoot,
                                        const std::string&              assetRoot,
                                        const std::vector<std::string>& enabledPlugins)
    {
        if (enabledPlugins.empty())
            return false;
        const auto manifests = discoverProjectPlugins(discoveryDirs(projectRoot, assetRoot));
        for (const auto& manifest : manifests)
        {
            if (manifest.needsRestartToApply() && manifest.supportsCurrentPlatform() &&
                std::find(enabledPlugins.begin(), enabledPlugins.end(), manifest.id) != enabledPlugins.end())
                return true;
        }
        return false;
    }

    bool fetchCatalog(const std::filesystem::path& projectRoot,
                      const std::string&           location,
                      std::vector<CatalogEntry>&   entries,
                      std::string&                 status)
    {
        namespace fs = std::filesystem;
        entries.clear();
        if (location.empty())
        {
            status = "Catalog location is empty.";
            return false;
        }

        std::error_code ec;
        fs::path        catalogPath {location};
        std::string     offlineNote;
        if (isHttpUrl(location))
        {
            catalogPath =
                (managedRoot(projectRoot) / "catalogs" / (readableRepoName(location) + ".json")).lexically_normal();
            // CDN hosts (raw.githubusercontent.com caches ~5 minutes) key their cache on the full
            // URL, so a throwaway query parameter makes a refresh actually fetch fresh content.
            std::string downloadUrl = location;
            downloadUrl += downloadUrl.find('?') == std::string::npos ? '?' : '&';
            downloadUrl += "nocache=" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                                           std::chrono::system_clock::now().time_since_epoch())
                                                           .count());
            std::string downloadStatus;
            if (!downloadTextToFile(downloadUrl, catalogPath, downloadStatus))
            {
                if (!fs::exists(catalogPath, ec))
                {
                    status = downloadStatus;
                    return false;
                }
                offlineNote = " (offline: showing cached catalog; " + downloadStatus + ")";
            }
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
            auto versions = parseCatalogVersions(plugin);
            if (versions.empty())
                continue;

            CatalogEntry entry;
            entry.id          = plugin.value("id", std::string {});
            entry.name        = plugin.value("name", std::string {});
            entry.author      = plugin.value("author", std::string {});
            entry.description = plugin.value("description", std::string {});
            entry.repository  = plugin.value("repository", std::string {});
            if (const auto it = plugin.find("platforms"); it != plugin.end() && it->is_array())
            {
                for (const auto& p : *it)
                    if (p.is_string())
                        entry.platforms.push_back(p.get<std::string>());
            }
            entry.versions = std::move(versions);
            entries.push_back(std::move(entry));
        }

        if (entries.empty())
        {
            status = "Catalog did not contain any git-backed plugins.";
            return false;
        }

        status = "Loaded " + std::to_string(entries.size()) + " catalog plugin(s)." + offlineNote;
        return true;
    }

    int compareVersions(const std::string& a, const std::string& b)
    {
        const auto nextComponent = [](const std::string& text, std::size_t& pos) {
            unsigned long value = 0;
            bool          any   = false;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
            {
                value = value * 10 + static_cast<unsigned long>(text[pos] - '0');
                any   = true;
                ++pos;
            }
            if (pos < text.size() && !std::isdigit(static_cast<unsigned char>(text[pos])))
                ++pos; // skip one separator/suffix character
            return any ? static_cast<long>(value) : -1L;
        };

        std::size_t posA = 0;
        std::size_t posB = 0;
        while (posA < a.size() || posB < b.size())
        {
            const long componentA = posA < a.size() ? nextComponent(a, posA) : 0;
            const long componentB = posB < b.size() ? nextComponent(b, posB) : 0;
            if (componentA != componentB)
                return componentA < componentB ? -1 : 1;
        }
        return 0;
    }

    ImportResult importFromGit(const std::filesystem::path& projectRoot,
                               const std::filesystem::path& pluginsDir,
                               const std::string&           url,
                               const std::string&           ref)
    {
        namespace fs = std::filesystem;
        ImportResult result;
        if (url.empty())
        {
            result.status = "Git URL is empty.";
            return result;
        }

        const auto      cacheDir = (gitCacheRoot(projectRoot) / readableRepoName(url)).lexically_normal();
        std::error_code ec;
        fs::create_directories(cacheDir.parent_path(), ec);
        if (ec)
        {
            result.status = "Failed to create git plugin cache: " + ec.message();
            return result;
        }

        std::string cacheWarning;
        if (!syncGitCache(cacheDir, url, ref, cacheWarning, result.status))
        {
            result.status = "Git import failed: " + result.status;
            return result;
        }

        const auto root = findPluginRoot(cacheDir);
        if (!root.has_value())
        {
            result.status = "Git repository did not contain " + std::string(vultra::kPluginManifestFile) + ".";
            return result;
        }

        std::string manifestError;
        auto        manifest = vultra::loadPluginManifest(*root / vultra::kPluginManifestFile, &manifestError);
        if (!manifest.has_value())
        {
            result.status = manifestError.empty() ? "git plugin manifest could not be read" : manifestError;
            return result;
        }

        // Materialize the payload as an immutable version directory (xmake-repo style):
        // <managed-root>/<id>/<version>. Rollback re-points the lock at a sibling version.
        const auto version =
            !manifest->version.empty() ? manifest->version : (ref.empty() ? std::string {"dev"} : ref);
        const auto  versionDir = (managedRoot(projectRoot) / manifest->id / version).lexically_normal();
        std::string copyError;
        if (!copyPluginPayload(*root, versionDir, copyError))
        {
            result.status = "Git import failed: " + copyError;
            return result;
        }

        auto installedManifest = vultra::loadPluginManifest(versionDir / vultra::kPluginManifestFile, &manifestError);
        if (!installedManifest.has_value())
        {
            result.status = manifestError.empty() ? "installed plugin manifest could not be read" : manifestError;
            return result;
        }

        nlohmann::json source {
            {"type", "git"},
            {"url", url},
            {"cache", fs::relative(cacheDir, projectRoot, ec).generic_string()},
        };
        if (!ref.empty())
            source["ref"] = ref;

        saveLock(projectRoot, pluginsDir, installedManifest, source);
        result.ok          = true;
        result.installedId = installedManifest->id;
        result.status      = "Installed managed plugin '" + installedManifest->name + "' (" + installedManifest->id +
                        ", v" + version + ")." + cacheWarning;
        return result;
    }

    ImportResult importFromZip(const std::filesystem::path& projectRoot,
                               const std::filesystem::path& pluginsDir,
                               const std::filesystem::path& zipPath)
    {
        namespace fs = std::filesystem;
        ImportResult    result;
        std::error_code ec;
        if (!fs::exists(zipPath, ec) || !fs::is_regular_file(zipPath, ec))
        {
            result.status = "Zip file does not exist.";
            return result;
        }

        const auto extractDir =
            (projectRoot / ".vultra" / "plugins" / "zip-imports" / safeCacheName(zipPath.stem().generic_string()))
                .lexically_normal();
        fs::remove_all(extractDir, ec);
        fs::create_directories(extractDir, ec);
        if (ec)
        {
            result.status = "Failed to create zip import cache: " + ec.message();
            return result;
        }

        std::ostringstream cmd;
#if defined(_WIN32)
        cmd << "tar -xf " << quoteCommandArg(zipPath) << " -C " << quoteCommandArg(extractDir);
#else
        cmd << "unzip -o " << quoteCommandArg(zipPath) << " -d " << quoteCommandArg(extractDir);
#endif
        if (std::system(cmd.str().c_str()) != 0)
        {
            result.status = "Failed to extract plugin zip.";
            return result;
        }

        const auto root = findPluginRoot(extractDir);
        if (!root.has_value())
        {
            result.status = "Zip did not contain " + std::string(vultra::kPluginManifestFile) + ".";
            return result;
        }

        return installLocalPluginFromRoot(
            projectRoot, pluginsDir, *root, nlohmann::json {{"type", "zip"}, {"path", zipPath.generic_string()}});
    }

    ImportResult importFromFolder(const std::filesystem::path& projectRoot,
                                  const std::filesystem::path& pluginsDir,
                                  const std::filesystem::path& sourceFolder)
    {
        ImportResult result;
        const auto   root = findPluginRoot(sourceFolder);
        if (!root.has_value())
        {
            result.status = "No " + std::string(vultra::kPluginManifestFile) + " found in folder.";
            return result;
        }

        return installLocalPluginFromRoot(
            projectRoot,
            pluginsDir,
            *root,
            nlohmann::json {{"type", "folder"}, {"path", sourceFolder.generic_string()}});
    }

    bool removeInstall(const std::filesystem::path&  projectRoot,
                       const std::string&            assetRoot,
                       const vultra::PluginManifest& manifest,
                       std::string&                  status)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (manifest.directory.empty() || !fs::exists(manifest.directory, ec))
        {
            status = "Plugin directory does not exist.";
            return false;
        }

        const auto isUnder = [&ec](const fs::path& path, const fs::path& base) {
            ec.clear();
            const auto rel  = fs::relative(path, base, ec);
            const auto text = rel.generic_string();
            return !ec && !text.empty() && text != ".." && !text.starts_with("../");
        };

        const auto localDir   = localInstallDir(projectRoot, assetRoot).lexically_normal();
        const auto managedDir = managedRoot(projectRoot).lexically_normal();
        const auto pluginDir  = manifest.directory.lexically_normal();
        const bool inLocal    = isUnder(pluginDir, localDir);
        const bool inManaged  = !inLocal && isUnder(pluginDir, managedDir);
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
            // Managed layout is <managed-root>/<id>/<version>; drop every materialized version of
            // the plugin. Legacy single-folder entries (and anything else) only drop the version
            // folder itself. The git cache under .cache/ always stays.
            auto removeDir = pluginDir;
            if (pluginDir.parent_path().filename().generic_string() == manifest.id &&
                pluginDir.parent_path().parent_path() == managedDir)
                removeDir = pluginDir.parent_path();
            fs::remove_all(removeDir, ec);
            if (ec)
            {
                status = "Failed to remove plugin: " + ec.message();
                return false;
            }
            status = "Removed managed plugin '" + manifest.name + "' (" + manifest.id + ").";
        }

        refreshLock(projectRoot, assetRoot, {manifest.id});
        return true;
    }
} // namespace vultra_app::plugins
