#pragma once

#include <vultra/core/base/error.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace vultra
{
    // Watches directory trees for content changes on desktop platforms. Operating-system callbacks only queue
    // paths; poll() hands them to the caller's thread, so reload work never runs on a watcher thread.
    class FileWatcher
    {
    public:
        FileWatcher();
        ~FileWatcher();
        FileWatcher(const FileWatcher&)            = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;

        // Replaces the watched set. Each directory is watched with its subdirectories, except directories
        // named build, .git or .vultra. Directories that do not exist are ignored.
        [[nodiscard]] std::expected<void, Error> watch(std::span<const std::filesystem::path> directories);

        // Absolute paths that changed and then stayed unchanged for the debounce interval, each reported once.
        // An editor that saves through a temporary file and a rename is reported under the final name.
        std::vector<std::filesystem::path> poll();

    private:
        void watchDirectories(std::span<const std::filesystem::path> directories);

        struct State;
        std::unique_ptr<State> m_State;
    };
} // namespace vultra
