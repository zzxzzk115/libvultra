#include "editor_app/project_file_watcher.hpp"

#include <algorithm>

namespace vultra_app
{
    namespace
    {
        constexpr auto kPollInterval = std::chrono::milliseconds(750);

        std::string normalizedKey(const std::filesystem::path& path)
        {
            return path.lexically_normal().generic_string();
        }
    } // namespace

    ProjectFileWatcher::~ProjectFileWatcher()
    {
        stop();
    }

    void ProjectFileWatcher::setRoot(const std::filesystem::path& assetRoot)
    {
        const auto normalized = assetRoot.empty() ? std::filesystem::path {} : assetRoot.lexically_normal();

        std::unique_lock lock(m_Mutex);
        if (normalized == m_Root)
            return;

        m_StopRequested.store(true, std::memory_order_release);
        m_Cv.notify_all();
        lock.unlock();
        if (m_Worker.joinable())
            m_Worker.join();

        lock.lock();
        m_StopRequested.store(false, std::memory_order_release);
        m_Root = normalized;
        m_Changed.store(true, std::memory_order_release);
        m_Generation.fetch_add(1, std::memory_order_acq_rel);

        if (!m_Root.empty())
            startLocked(m_Root);
    }

    void ProjectFileWatcher::stop()
    {
        {
            std::scoped_lock lock(m_Mutex);
            m_StopRequested.store(true, std::memory_order_release);
            m_Root.clear();
        }
        m_Cv.notify_all();
        if (m_Worker.joinable())
            m_Worker.join();
        m_StopRequested.store(false, std::memory_order_release);
    }

    bool ProjectFileWatcher::consumeChanged()
    {
        return m_Changed.exchange(false, std::memory_order_acq_rel);
    }

    void ProjectFileWatcher::startLocked(const std::filesystem::path& assetRoot)
    {
        m_Worker = std::thread([this, assetRoot]() { workerMain(assetRoot); });
    }

    void ProjectFileWatcher::workerMain(std::filesystem::path assetRoot)
    {
        Snapshot previous = scanRoot(assetRoot);

        std::unique_lock lock(m_Mutex);
        while (!m_StopRequested.load(std::memory_order_acquire))
        {
            if (m_Cv.wait_for(lock, kPollInterval, [this]() {
                    return m_StopRequested.load(std::memory_order_acquire);
                }))
            {
                break;
            }

            lock.unlock();
            Snapshot current = scanRoot(assetRoot);
            const bool changed = current != previous;
            if (changed)
                previous = std::move(current);
            lock.lock();

            if (changed)
            {
                m_Changed.store(true, std::memory_order_release);
                m_Generation.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    }

    ProjectFileWatcher::Snapshot ProjectFileWatcher::scanRoot(const std::filesystem::path& root)
    {
        Snapshot snapshot;
        std::error_code ec;
        if (root.empty() || !std::filesystem::exists(root, ec) || ec)
            return snapshot;

        for (auto it = std::filesystem::recursive_directory_iterator(root,
                                                                     std::filesystem::directory_options::
                                                                         skip_permission_denied,
                                                                     ec);
             it != std::filesystem::recursive_directory_iterator {};
             it.increment(ec))
        {
            if (ec)
                break;

            const auto& entry = *it;
            const auto path = entry.path();
            const auto name = path.filename().generic_string();
            if (name == ".vultra" || name == ".git")
            {
                if (entry.is_directory(ec) && !ec)
                    it.disable_recursion_pending();
                ec.clear();
                continue;
            }

            snapshot.emplace(normalizedKey(path),
                             FileState {
                                 .stamp     = fileStamp(entry),
                                 .directory = entry.is_directory(ec) && !ec,
                             });
            ec.clear();
        }
        return snapshot;
    }

    uint64_t ProjectFileWatcher::fileStamp(const std::filesystem::directory_entry& entry)
    {
        std::error_code ec;
        const auto      time = entry.last_write_time(ec);
        if (ec)
            return 0;
        return static_cast<uint64_t>(time.time_since_epoch().count());
    }
} // namespace vultra_app
