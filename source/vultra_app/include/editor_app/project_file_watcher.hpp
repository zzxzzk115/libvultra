#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace vultra_app
{
    class ProjectFileWatcher
    {
    public:
        ProjectFileWatcher() = default;
        ~ProjectFileWatcher();

        ProjectFileWatcher(const ProjectFileWatcher&) = delete;
        ProjectFileWatcher& operator=(const ProjectFileWatcher&) = delete;

        void setRoot(const std::filesystem::path& assetRoot);
        void stop();

        [[nodiscard]] bool consumeChanged();
        [[nodiscard]] uint64_t generation() const { return m_Generation.load(std::memory_order_acquire); }

    private:
        struct FileState
        {
            uint64_t stamp {0};
            bool     directory {false};

            friend bool operator==(const FileState& lhs, const FileState& rhs)
            {
                return lhs.stamp == rhs.stamp && lhs.directory == rhs.directory;
            }
        };

        using Snapshot = std::unordered_map<std::string, FileState>;

        static Snapshot scanRoot(const std::filesystem::path& root);
        static uint64_t fileStamp(const std::filesystem::directory_entry& entry);

        void startLocked(const std::filesystem::path& assetRoot);
        void workerMain(std::filesystem::path assetRoot);

        mutable std::mutex      m_Mutex;
        std::condition_variable m_Cv;
        std::thread             m_Worker;
        std::filesystem::path   m_Root;
        std::atomic_bool        m_StopRequested {false};
        std::atomic_bool        m_Changed {false};
        std::atomic_uint64_t    m_Generation {0};
    };
} // namespace vultra_app
