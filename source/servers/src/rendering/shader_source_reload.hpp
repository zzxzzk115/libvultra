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

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace vultra::detail
{
    // Host-only state. Watch callbacks and compile jobs never retain GPU materials or preparation callbacks.
    struct ShaderSourceReload
    {
        using Watch            = filewatch::FileWatch<std::string>;
        using PublishCandidate = std::function<std::expected<bool, std::string>(ShaderAsset, uint64_t)>;

        struct Job
        {
            struct Native;
            uint64_t                                revision = 0;
            std::atomic_bool                        done {false};
            std::expected<ShaderAsset, std::string> candidate {std::unexpected("Shader compile has not completed")};
            std::filesystem::path                   file;
            ShaderCompileOptions                    options;
            // The job owns exactly one compiler thread and joins it before its result/input storage expires.
            std::unique_ptr<Native> worker;

            Job();
            ~Job();
            std::expected<void, std::string> startExpected();
            void                             compile();
        };

        struct WatchPlan
        {
            std::set<std::filesystem::path>                         desired;
            std::map<std::filesystem::path, std::unique_ptr<Watch>> additions;
            std::vector<ShaderDependency>                           dependencies;
        };

        ShaderSourceReload(std::filesystem::path file, ShaderCompileOptions options, std::atomic<uint64_t>& revision);
        ~ShaderSourceReload();
        std::expected<WatchPlan, std::string> prepareAcceptedExpected(const ShaderAsset& asset);
        void                                  accept(WatchPlan plan);
        std::expected<void, std::string>      observeCandidateExpected(const ShaderAsset& asset);
        std::expected<bool, std::string>      dependenciesCurrentExpected(const ShaderAsset& asset) const;
        std::expected<bool, std::string>      pollExpected(const PublishCandidate& publish, std::string& diagnostics);

        std::filesystem::path                 file;
        ShaderCompileOptions                  options;
        std::atomic<uint64_t>&                revision;
        uint64_t                              observed = 0;
        uint64_t                              started  = 0;
        std::chrono::steady_clock::time_point changed {};
        std::unique_ptr<Job>                  job;

    private:
        std::expected<WatchPlan, std::string> prepareDirectoriesExpected(std::vector<ShaderDependency> dependencies);
        std::expected<void, std::string>      refreshExpected();
        void                                  commitDirectories(WatchPlan& plan);
        std::vector<ShaderDependency>         m_AcceptedDependencies;
        std::vector<ShaderDependency>         m_RetryDependencies;
        bool                                  m_WatchPending = false;
        // Join callbacks before their borrowed revision counter can expire.
        std::map<std::filesystem::path, std::unique_ptr<Watch>> m_Watchers;
    };
} // namespace vultra::detail
