#pragma once

#include <vultra/gpu/profiling/profiler.hpp>

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vultra
{
    struct BenchmarkMetadata
    {
        std::string                                      experiment;
        std::string                                      sourceRevision;
        std::string                                      shaderHash;
        std::string                                      windowSystem;
        std::string                                      presentMode = "fifo";
        std::string                                      buildMode;
        bool                                             validation   = true;
        uint32_t                                         width        = 0;
        uint32_t                                         height       = 0;
        uint64_t                                         warmupFrames = 0;
        std::vector<std::pair<std::string, std::string>> parameters;
    };

    // Keeps measured frames in memory; write only after the measurement has ended.
    class BenchmarkCapture
    {
    public:
        [[nodiscard]] std::expected<void, Error> add(const FrameTiming& frame, std::span<const PassTiming> passes);
        // Refuses to overwrite an existing run directory.
        [[nodiscard]] std::expected<void, Error> write(const std::filesystem::path& directory,
                                                       const BenchmarkMetadata&     metadata,
                                                       const VriDeviceDesc&         device) const;

        size_t size() const
        {
            return m_Samples.size();
        }

    private:
        struct Sample
        {
            FrameTiming             frame;
            std::vector<PassTiming> passes;
        };

        std::vector<Sample> m_Samples;
    };
} // namespace vultra
