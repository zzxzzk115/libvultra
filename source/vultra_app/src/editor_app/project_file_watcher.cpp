#include "editor_app/project_file_watcher.hpp"

#include <algorithm>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

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

    ProjectFileWatcher::~ProjectFileWatcher() { stop(); }

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
#if defined(_WIN32)
            if (m_NativeHandle)
                CancelIoEx(static_cast<HANDLE>(m_NativeHandle), nullptr);
#endif
            m_Root.clear();
        }
        m_Cv.notify_all();
        if (m_Worker.joinable())
            m_Worker.join();
        m_StopRequested.store(false, std::memory_order_release);
    }

    bool ProjectFileWatcher::consumeChanged() { return m_Changed.exchange(false, std::memory_order_acq_rel); }

    void ProjectFileWatcher::startLocked(const std::filesystem::path& assetRoot)
    {
        m_Worker = std::thread([this, assetRoot]() { workerMain(assetRoot); });
    }

    void ProjectFileWatcher::workerMain(std::filesystem::path assetRoot)
    {
#if defined(_WIN32)
        auto handle = CreateFileW(assetRoot.wstring().c_str(),
                                  FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                  nullptr);
        if (handle != INVALID_HANDLE_VALUE)
        {
            {
                std::scoped_lock lock(m_Mutex);
                m_NativeHandle = handle;
            }

            std::vector<unsigned char> buffer(64 * 1024);
            OVERLAPPED                 overlapped {};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (overlapped.hEvent)
            {
                while (!m_StopRequested.load(std::memory_order_acquire))
                {
                    ResetEvent(overlapped.hEvent);
                    DWORD      bytesReturned = 0;
                    const BOOL ok            = ReadDirectoryChangesW(handle,
                                                          buffer.data(),
                                                          static_cast<DWORD>(buffer.size()),
                                                          TRUE,
                                                          FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                              FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                                          nullptr,
                                                          &overlapped,
                                                          nullptr);
                    if (!ok)
                        break;

                    const DWORD waitResult = WaitForSingleObject(overlapped.hEvent, INFINITE);
                    if (m_StopRequested.load(std::memory_order_acquire))
                    {
                        CancelIoEx(handle, &overlapped);
                        break;
                    }
                    if (waitResult != WAIT_OBJECT_0)
                    {
                        CancelIoEx(handle, &overlapped);
                        break;
                    }

                    if (GetOverlappedResult(handle, &overlapped, &bytesReturned, FALSE) && bytesReturned > 0)
                    {
                        m_Changed.store(true, std::memory_order_release);
                        m_Generation.fetch_add(1, std::memory_order_acq_rel);
                    }
                }
                CloseHandle(overlapped.hEvent);
            }

            {
                std::scoped_lock lock(m_Mutex);
                if (m_NativeHandle == handle)
                    m_NativeHandle = nullptr;
            }
            CloseHandle(handle);
            return;
        }
#endif

        workerMainPolling(std::move(assetRoot));
    }

    void ProjectFileWatcher::workerMainPolling(std::filesystem::path assetRoot)
    {
        Snapshot previous = scanRoot(assetRoot);

        std::unique_lock lock(m_Mutex);
        while (!m_StopRequested.load(std::memory_order_acquire))
        {
            if (m_Cv.wait_for(
                    lock, kPollInterval, [this]() { return m_StopRequested.load(std::memory_order_acquire); }))
            {
                break;
            }

            lock.unlock();
            Snapshot   current = scanRoot(assetRoot);
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
        Snapshot        snapshot;
        std::error_code ec;
        if (root.empty() || !std::filesystem::exists(root, ec) || ec)
            return snapshot;

        for (auto it = std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator {};
             it.increment(ec))
        {
            if (ec)
                break;

            const auto& entry = *it;
            const auto  path  = entry.path();
            const auto  name  = path.filename().generic_string();
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
