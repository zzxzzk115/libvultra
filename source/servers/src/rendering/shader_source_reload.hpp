#pragma once

#include <vultra/assets/shader_asset.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005 4068) // Upstream FileWatch macros and pragmas.
#endif
#include <FileWatch.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>

namespace vultra::detail
{
    // Host-only state. Watch callbacks and compilation jobs never retain GPU materials or preparation callbacks.
    struct ShaderSourceReload
    {
        using Watch            = filewatch::FileWatch<std::string>;
        using PublishCandidate = std::function<bool(ShaderAsset, uint64_t)>;

        struct Job
        {
            uint64_t                        revision = 0;
            std::atomic_bool                done {false};
            std::unique_ptr<ShaderAsset>    candidate;
            std::string                     diagnostics;
            std::unique_ptr<vtask::TaskSet> task;
        };

        ShaderSourceReload(std::filesystem::path file, ShaderCompileOptions options, std::atomic<uint64_t>& revision);
        ~ShaderSourceReload();
        void refreshWatch(const ShaderAsset& asset);
        bool dependenciesCurrent(const ShaderAsset& asset) const;
        bool poll(const ShaderAsset& asset, const PublishCandidate& publish, std::string& diagnostics);

        std::filesystem::path                 file;
        ShaderCompileOptions                  options;
        std::atomic<uint64_t>&                revision;
        uint64_t                              observed = 0;
        uint64_t                              started  = 0;
        std::chrono::steady_clock::time_point changed {};
        // vtask counts the calling thread; two threads provide one background compiler worker.
        vtask::Scheduler                                        scheduler {2};
        std::unique_ptr<Job>                                    job;
        std::map<std::filesystem::path, std::unique_ptr<Watch>> watchers;
    };
} // namespace vultra::detail
