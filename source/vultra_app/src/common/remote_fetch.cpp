#include "common/remote_fetch.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#    include <windows.h>
#    include <winhttp.h>
#endif

namespace vultra_app
{
    namespace
    {
        namespace fs = std::filesystem;

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
            cmd << net::quoteCommandArg(executable.generic_string());
            for (const auto& arg : args)
                cmd << " " << net::quoteCommandArg(arg);
            if (std::system(cmd.str().c_str()) != 0)
            {
                status = "Process failed.";
                return false;
            }
            return true;
        }
#endif

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
#endif
    } // namespace

    namespace net
    {
        bool isHttpUrl(std::string_view value)
        {
            return value.starts_with("https://") || value.starts_with("http://");
        }

        std::string quoteCommandArg(std::string_view text)
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
                name = "cache";
            std::ostringstream suffix;
            suffix << "-" << std::hex << std::hash<std::string_view> {}(text);
            return name + suffix.str();
        }

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

#if defined(_WIN32)
        bool downloadToFile(const std::string& url, const fs::path& destination, std::string& status)
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
                status = "Invalid URL.";
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
                status = "Download failed.";
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
                status = "Download returned HTTP " + std::to_string(statusCode) + ".";
                return false;
            }

            std::ofstream out(destination, std::ios::binary);
            if (!out)
            {
                status = "Failed to open download cache for writing.";
                return false;
            }

            for (;;)
            {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request.handle, &available))
                {
                    status = "Download failed while reading.";
                    return false;
                }
                if (available == 0)
                    break;

                std::vector<char> buffer(available);
                DWORD             read = 0;
                if (!WinHttpReadData(request.handle, buffer.data(), available, &read))
                {
                    status = "Download failed while receiving data.";
                    return false;
                }
                out.write(buffer.data(), static_cast<std::streamsize>(read));
            }

            return true;
        }
#else
        bool downloadToFile(const std::string&, const fs::path&, std::string& status)
        {
            status = "Remote downloads are not implemented on this platform yet.";
            return false;
        }
#endif

        fs::path cachedImage(const fs::path& destFile, const std::string& url, std::string& status)
        {
            if (url.empty())
            {
                status = "No image url.";
                return {};
            }
            std::error_code ec;
            if (!isHttpUrl(url))
            {
                // Local-path image (e.g. a file:// catalog used during testing).
                const fs::path local {url};
                if (fs::exists(local, ec))
                    return local;
                status = "Image not found.";
                return {};
            }
            if (fs::exists(destFile, ec))
                return destFile;
            if (!downloadToFile(url, destFile, status))
                return {};
            return destFile;
        }
    } // namespace net

    namespace git
    {
        std::optional<fs::path> commandPath()
        {
#if defined(_WIN32)
            return resolveExecutablePath("git.exe");
#else
            return resolveExecutablePath("git");
#endif
        }

        bool run(const std::vector<std::string>& args, std::string& status)
        {
            const auto git = commandPath();
            if (!git.has_value())
            {
                status = "Git executable was not found by the editor process. Add Git to PATH or install it in "
                         "C:/Program Files/Git.";
                return false;
            }
            return runProcess(*git, args, status);
        }

        bool syncCache(const fs::path&                            cacheDir,
                       const std::string&                         url,
                       const std::string&                         ref,
                       const std::function<bool(const fs::path&)>& cacheUsable,
                       std::string&                               cacheWarning,
                       std::string&                               status)
        {
            std::error_code ec;
            const bool      isGitCheckout = fs::exists(cacheDir / ".git", ec);

            if (!isGitCheckout)
            {
                // A non-git cache folder (e.g. a release payload) can be reused as-is, but cannot
                // be switched to a specific ref. Re-clone in that case.
                if (fs::exists(cacheDir, ec) && ref.empty() && cacheUsable && cacheUsable(cacheDir))
                {
                    cacheWarning = " Using cached files.";
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
                return run(args, status);
            }

            const auto cache = cacheDir.generic_string();
            if (ref.empty())
            {
                std::string pullStatus;
                if (!run({"-C", cache, "pull", "--ff-only"}, pullStatus))
                    cacheWarning = " Using cached checkout because update failed: " + pullStatus;
                return true;
            }

            // Pin to the requested release tag: fetch it (force handles shallow clones and moved
            // tags), then check it out detached. If fetching fails (offline), a local checkout of
            // the same ref still succeeds.
            std::string fetchStatus;
            const bool  fetched =
                run({"-C", cache, "fetch", "--depth", "1", "--force", "origin", "tag", ref}, fetchStatus);
            if (!run({"-C", cache, "checkout", "--force", ref}, status))
            {
                if (!fetched && fetchStatus != status)
                    status += " (tag fetch also failed: " + fetchStatus + ")";
                return false;
            }
            if (!fetched)
                cacheWarning = " Using locally cached ref because fetch failed.";
            return true;
        }

        bool copyPayloadStripGit(const fs::path& source, const fs::path& destination, std::string& error)
        {
            std::error_code ec;
            fs::remove_all(destination, ec);
            if (ec)
            {
                error = "failed to replace existing folder: " + ec.message();
                return false;
            }
            fs::create_directories(destination, ec);
            if (ec)
            {
                error = "failed to create folder: " + ec.message();
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
                    error = "failed to copy files: " + ec.message();
                    return false;
                }
            }
            if (ec)
            {
                error = "failed to enumerate files: " + ec.message();
                return false;
            }
            return true;
        }
    } // namespace git
} // namespace vultra_app
