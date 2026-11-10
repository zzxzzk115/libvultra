#include "rhi/shader_cache.hpp"
#include "shader_source_reload.hpp"

#include <vultra/core/base/logger.hpp>

#ifdef _WIN32
#include <process.h>

#include <windows.h>
#else
#include <pthread.h>
#endif

#include <cerrno>
#include <system_error>

namespace vultra::detail
{
    namespace
    {
        std::vector<ShaderDependency> dependencies(const ShaderAsset& asset)
        {
            std::vector<ShaderDependency> result;
            for (const auto& subshader : asset.subshaders)
            {
                for (const auto& pass : subshader.passes)
                {
                    for (const auto& [name, program] : pass.programs)
                    {
                        result.insert(result.end(), program.dependencies.begin(), program.dependencies.end());
                    }
                }
            }
            return result;
        }
    } // namespace

    struct ShaderSourceReload::Job::Native
    {
#ifdef _WIN32
        HANDLE handle = nullptr;

        static unsigned __stdcall enter(void* value)
        {
            static_cast<Job*>(value)->compile();
            return 0;
        }
#else
        pthread_t handle {};
        bool      running = false;

        static void* enter(void* value)
        {
            static_cast<Job*>(value)->compile();
            return nullptr;
        }
#endif
        std::expected<void, std::string> startExpected(Job& job)
        {
#ifdef _WIN32
            handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, enter, &job, 0, nullptr));
            if (!handle)
            {
                return std::unexpected("Start shader compiler thread: " +
                                       std::error_code(errno, std::generic_category()).message());
            }
#else
            const auto status = pthread_create(&handle, nullptr, enter, &job);
            if (status != 0)
            {
                return std::unexpected("Start shader compiler thread: " +
                                       std::error_code(status, std::generic_category()).message());
            }
            running = true;
#endif
            return {};
        }

        ~Native()
        {
#ifdef _WIN32
            if (handle)
            {
                if (WaitForSingleObject(handle, INFINITE) != WAIT_OBJECT_0)
                {
                    std::terminate(); // A live uniquely owned compiler thread must remain joinable.
                }
                CloseHandle(handle);
            }
#else
            if (running && pthread_join(handle, nullptr) != 0)
            {
                std::terminate(); // The owner is never joined by its compiler worker.
            }
#endif
        }
    };

    ShaderSourceReload::Job::Job() = default;

    ShaderSourceReload::Job::~Job()
    {
        worker.reset();
    }

    std::expected<void, std::string> ShaderSourceReload::Job::startExpected()
    {
        worker = std::make_unique<Native>();
        return worker->startExpected(*this);
    }

    void ShaderSourceReload::Job::compile()
    {
        candidate = ShaderAsset::compileExpected(file, options);
        done.store(true, std::memory_order_release);
    }

    ShaderSourceReload::ShaderSourceReload(std::filesystem::path  file,
                                           ShaderCompileOptions   options,
                                           std::atomic<uint64_t>& revision) :
        file(std::move(file)),
        options(std::move(options)),
        revision(revision)
    {
    }

    ShaderSourceReload::~ShaderSourceReload()
    {
        m_Watchers.clear();
        job.reset();
    }

    std::expected<ShaderSourceReload::WatchPlan, std::string>
    ShaderSourceReload::prepareDirectoriesExpected(std::vector<ShaderDependency> inputs)
    {
        auto roots = shaderWatchDirectoriesExpected(inputs, file, options);
        if (!roots)
        {
            return std::unexpected(roots.error());
        }
        WatchPlan plan;
        plan.desired      = std::move(*roots);
        plan.dependencies = std::move(inputs);
#if defined(__linux__)
        // inotify observes one directory; Windows and FSEvents already observe the authored subtree.
        std::error_code                                     error;
        std::filesystem::recursive_directory_iterator       entry(file.parent_path(), error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && entry != end)
        {
            if (entry->is_directory(error))
            {
                const auto name = entry->path().filename();
                if (name == "build" || name == ".git" || name == ".vultra")
                {
                    entry.disable_recursion_pending();
                }
                else
                {
                    plan.desired.insert(entry->path());
                }
            }
            if (!error)
            {
                entry.increment(error);
            }
        }
        if (error)
        {
            return std::unexpected("Enumerate shader watch tree: " + error.message());
        }
#endif
        for (const auto& root : plan.desired)
        {
            const auto existing = m_Watchers.find(root);
            if (existing != m_Watchers.end() && existing->second->statusExpected())
            {
                continue;
            }
            auto watch = Watch::createExpected(
                root.string(),
                [this](const std::string& path, filewatch::Event) -> std::expected<void, std::string>
                {
                    for (const auto& part : std::filesystem::path(path))
                    {
                        if (part == "build" || part == ".git" || part == ".vultra")
                        {
                            return {};
                        }
                    }
                    // Includes can use arbitrary suffixes; search-directory changes can change resolution.
                    revision.fetch_add(1, std::memory_order_relaxed);
                    return {};
                });
            if (!watch)
            {
                return std::unexpected("Start game shader watch for " + root.string() + ": " + watch.error());
            }
            plan.additions.emplace(root, std::move(*watch));
        }
        return plan;
    }

    void ShaderSourceReload::commitDirectories(WatchPlan& plan)
    {
        std::erase_if(m_Watchers,
                      [&](const auto& entry)
                      {
                          return !plan.desired.contains(entry.first) || plan.additions.contains(entry.first);
                      });
        m_Watchers.merge(plan.additions);
    }

    std::expected<ShaderSourceReload::WatchPlan, std::string>
    ShaderSourceReload::prepareAcceptedExpected(const ShaderAsset& asset)
    {
        return prepareDirectoriesExpected(dependencies(asset));
    }

    void ShaderSourceReload::accept(WatchPlan plan)
    {
        commitDirectories(plan);
        m_AcceptedDependencies = std::move(plan.dependencies);
        m_RetryDependencies.clear();
        m_WatchPending = false;
    }

    std::expected<void, std::string> ShaderSourceReload::refreshExpected()
    {
        auto inputs = m_AcceptedDependencies;
        inputs.insert(inputs.end(), m_RetryDependencies.begin(), m_RetryDependencies.end());
        auto plan = prepareDirectoriesExpected(std::move(inputs));
        if (!plan)
        {
            return std::unexpected(plan.error());
        }
        commitDirectories(*plan);
        m_WatchPending = false;
        return {};
    }

    std::expected<void, std::string> ShaderSourceReload::observeCandidateExpected(const ShaderAsset& asset)
    {
        m_RetryDependencies = dependencies(asset);
        m_WatchPending      = true;
        return refreshExpected();
    }

    std::expected<bool, std::string> ShaderSourceReload::dependenciesCurrentExpected(const ShaderAsset& asset) const
    {
        return shaderDependenciesCurrentExpected(dependencies(asset), file, options);
    }

    std::expected<bool, std::string> ShaderSourceReload::pollExpected(const PublishCandidate& publish,
                                                                      std::string&            diagnostics)
    {
        if (m_WatchPending)
        {
            if (auto refreshed = refreshExpected(); !refreshed)
            {
                return std::unexpected(refreshed.error());
            }
        }
        for (const auto& [root, watch] : m_Watchers)
        {
            if (auto status = watch->statusExpected(); !status)
            {
                m_WatchPending = true;
                revision.fetch_add(1, std::memory_order_relaxed);
                return std::unexpected("Game shader watch for " + root.string() + ": " + status.error());
            }
        }
        const auto sourceRevision = revision.load(std::memory_order_relaxed);
        if (sourceRevision != observed)
        {
            observed       = sourceRevision;
            m_WatchPending = true;
            if (auto refreshed = refreshExpected(); !refreshed)
            {
                return std::unexpected(refreshed.error());
            }
            changed = std::chrono::steady_clock::now();
        }
        bool published = false;
        if (job && job->done.load(std::memory_order_acquire))
        {
            auto completed = std::move(job);
            completed->worker.reset();
            if (completed->revision == revision.load(std::memory_order_relaxed))
            {
                if (!completed->candidate)
                {
                    diagnostics = completed->candidate.error();
                    return std::unexpected(diagnostics);
                }
                auto result = publish(std::move(*completed->candidate), completed->revision);
                if (!result)
                {
                    return std::unexpected(result.error());
                }
                published = *result;
            }
            else
            {
                Logger::core().info("Discarded stale shader compile revision {}", completed->revision);
            }
        }
        if (!job && sourceRevision != started &&
            std::chrono::steady_clock::now() - changed >= std::chrono::milliseconds(150))
        {
            auto candidate      = std::make_unique<Job>();
            candidate->revision = sourceRevision;
            candidate->file     = file;
            candidate->options  = options;
            if (auto launched = candidate->startExpected(); !launched)
            {
                return std::unexpected(launched.error());
            }
            Logger::core().info("Compiling shader revision {}", sourceRevision);
            started = sourceRevision;
            job     = std::move(candidate);
        }
        return published;
    }
} // namespace vultra::detail
