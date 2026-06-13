#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Source-neutral HTTP download + git/process primitives shared by the plugin repository
// (editor) and the examples repository (launcher). These were originally file-local helpers in
// plugin_repository.cpp; they carry no plugin-specific knowledge.
namespace vultra_app::net
{
    bool isHttpUrl(std::string_view value);

    // Double-quoted, backslash-escaped argument suitable for cmd.exe / POSIX shell use.
    std::string quoteCommandArg(std::string_view text);

    // Lowercased, filesystem-safe slug with a hashed suffix for uniqueness.
    std::string safeCacheName(std::string_view text);

    // "<owner>-<repo>" cache name for a repository-ish URL; hashed fallback otherwise.
    //   https://github.com/zzxzzk115/vultra-example-hello.git -> zzxzzk115-vultra-example-hello
    //   https://raw.githubusercontent.com/zzxzzk115/vultra-examples/main/examples.json
    //       -> zzxzzk115-vultra-examples
    std::string readableRepoName(std::string_view url);

    // WinHTTP GET streamed to a file (binary-safe; works for JSON and PNG). Windows-only; other
    // platforms return false with a status. Creates the destination's parent directories.
    bool downloadToFile(const std::string& url, const std::filesystem::path& destination, std::string& status);

    // Resolve an image url to a local path. For http(s) urls, download once to destFile (skipped if
    // it already exists) and return destFile. For a local path, return it if it exists. Returns an
    // empty path on failure (status set).
    [[nodiscard]] std::filesystem::path
    cachedImage(const std::filesystem::path& destFile, const std::string& url, std::string& status);
} // namespace vultra_app::net

namespace vultra_app::git
{
    // Resolve the git executable on PATH or standard Windows install locations.
    std::optional<std::filesystem::path> commandPath();

    // Run git with the given arguments. Windows: CREATE_NO_WINDOW child; otherwise std::system.
    bool run(const std::vector<std::string>& args, std::string& status);

    // Bring a managed git cache at cacheDir to url@ref (ref empty = default branch, ref non-empty =
    // a release tag checked out detached). cacheUsable is consulted only when ref is empty to allow
    // reusing a pre-existing non-git payload folder. cacheWarning collects soft fallbacks (e.g.
    // offline but usable cache). Returns false with status on hard failure.
    bool syncCache(const std::filesystem::path&                             cacheDir,
                   const std::string&                                       url,
                   const std::string&                                       ref,
                   const std::function<bool(const std::filesystem::path&)>& cacheUsable,
                   std::string&                                             cacheWarning,
                   std::string&                                             status);

    // Recursively copy source to destination, excluding a top-level .git directory.
    bool copyPayloadStripGit(const std::filesystem::path& source,
                             const std::filesystem::path& destination,
                             std::string&                 error);
} // namespace vultra_app::git
