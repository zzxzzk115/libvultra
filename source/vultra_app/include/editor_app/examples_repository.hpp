#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Remote sample/example catalog for the project launcher. Mirrors the plugin repository's
// git-backed, offline-resilient pattern (built on common/remote_fetch primitives), but examples
// are full openable Vultra projects rather than plugins, and each catalog entry carries a
// thumbnail url shown in the launcher's Samples grid.
//
// Launcher-global cache layout (rooted at the launcher working directory / cacheRoot):
//
//   .vultra/examples/
//     catalogs/<owner>-<repo>.json   downloaded catalog cache
//     thumbnails/<id>.png            downloaded preview thumbnails
//     .cache/<owner>-<repo>/         git clone cache used while forking
namespace vultra_app::examples
{
    [[nodiscard]] std::string defaultExamplesCatalogUrl();

    struct ExampleEntry
    {
        std::string              id;
        std::string              name;
        std::string              author;
        std::string              description;
        std::string              repository;
        std::string              thumbnailUrl;
        std::vector<std::string> tags;
        std::string              version;
        std::string              gitUrl;
        std::string              gitRef; // empty = default branch
    };

    // Fetch and parse the examples catalog from an http(s) URL or a local path. Remote downloads are
    // cached under <cacheRoot>/.vultra/examples/catalogs; on download failure a cached copy is used
    // so the grid still renders offline.
    bool fetchExamplesCatalog(const std::filesystem::path& cacheRoot,
                              const std::string&           location,
                              std::vector<ExampleEntry>&   entries,
                              std::string&                 status);

    // Download (once) and return the local cache path of an entry's thumbnail, or empty on failure.
    // Accepts a local file path as thumbnailUrl too (for testing against a file:// catalog).
    [[nodiscard]] std::filesystem::path cachedThumbnail(const std::filesystem::path& cacheRoot,
                                                        const ExampleEntry&          entry,
                                                        std::string&                 status);

    struct ForkResult
    {
        bool                  ok {false};
        std::string           status;
        std::filesystem::path projectFile; // path to the cloned project's .vproject on success
    };

    // Clone entry.gitUrl@gitRef into destDir (must be empty or nonexistent), strip .git, and locate
    // the cloned project's .vproject. destDir becomes a standalone, openable project. The clone is
    // cached under <cacheRoot>/.vultra/examples/.cache for reuse across forks.
    [[nodiscard]] ForkResult forkExample(const std::filesystem::path& cacheRoot,
                                         const ExampleEntry&          entry,
                                         const std::filesystem::path& destDir);
} // namespace vultra_app::examples
