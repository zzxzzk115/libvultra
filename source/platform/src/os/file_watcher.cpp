#include <vultra/platform/os/file_watcher.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005 4068) // upstream FileWatch macros and #pragma mark
#endif
#include <FileWatch.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <system_error>

namespace vultra
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        // Editors write a file in several steps; report it once the burst is over.
        constexpr auto kDebounce = std::chrono::milliseconds(150);

        bool ignored(const std::filesystem::path& path)
        {
            for (const auto& part : path)
            {
                if (part == "build" || part == ".git" || part == ".vultra")
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    struct FileWatcher::State
    {
        std::mutex                                         mutex;
        std::map<std::filesystem::path, Clock::time_point> changed;
        // Destroyed first: FileWatch joins its threads before the queue above goes away.
        std::map<std::filesystem::path, std::unique_ptr<filewatch::FileWatch<std::string>>> watchers;
    };

    FileWatcher::FileWatcher() :
        m_State(std::make_unique<State>())
    {
    }

    FileWatcher::~FileWatcher() = default;

    std::expected<void, Error> FileWatcher::watch(std::span<const std::filesystem::path> directories)
    {
        // std::filesystem and FileWatch report failures only as std::system_error (filesystem_error derives
        // from it). This is the boundary that converts them.
        try
        {
            watchDirectories(directories);
        }
        catch (const std::system_error& error)
        {
            return makeError("Watch directories: {}", error.what());
        }
        return {};
    }

    void FileWatcher::watchDirectories(std::span<const std::filesystem::path> directories)
    {
        std::set<std::filesystem::path> wanted;
        for (const auto& directory : directories)
        {
            if (!std::filesystem::is_directory(directory))
            {
                continue;
            }
            const auto root = std::filesystem::absolute(directory).lexically_normal();
            wanted.insert(root);
#if defined(__linux__) || defined(__APPLE__)
            // inotify watches one directory, and FileWatch's macOS backend drops FSEvents from subdirectories;
            // only the Windows backend watches subtrees.
            for (auto entry = std::filesystem::recursive_directory_iterator(root);
                 entry != std::filesystem::recursive_directory_iterator();
                 ++entry)
            {
                if (!entry->is_directory())
                {
                    continue;
                }
                if (ignored(entry->path().filename()))
                {
                    entry.disable_recursion_pending();
                    continue;
                }
                wanted.insert(entry->path());
            }
#endif
        }
        std::erase_if(m_State->watchers,
                      [&](const auto& entry)
                      {
                          return !wanted.contains(entry.first);
                      });
        for (const auto& directory : wanted)
        {
            if (m_State->watchers.contains(directory))
            {
                continue;
            }
            auto* state = m_State.get();
            m_State->watchers.emplace(directory,
                                      std::make_unique<filewatch::FileWatch<std::string>>(
                                          directory.string(),
                                          [state, directory](const std::string& path, filewatch::Event event)
                                          {
                                              // The old name of a rename no longer exists.
                                              if (event == filewatch::Event::renamed_old || ignored(path))
                                              {
                                                  return;
                                              }
                                              const std::lock_guard lock(state->mutex);
                                              state->changed[(directory / path).lexically_normal()] = Clock::now();
                                          }));
        }
    }

    std::vector<std::filesystem::path> FileWatcher::poll()
    {
        std::vector<std::filesystem::path> ready;
        const auto                         now = Clock::now();
        const std::lock_guard              lock(m_State->mutex);
        for (auto entry = m_State->changed.begin(); entry != m_State->changed.end();)
        {
            if (now - entry->second >= kDebounce)
            {
                ready.push_back(entry->first);
                entry = m_State->changed.erase(entry);
            }
            else
            {
                ++entry;
            }
        }
        return ready;
    }
} // namespace vultra
