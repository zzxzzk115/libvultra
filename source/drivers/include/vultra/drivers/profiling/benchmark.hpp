#pragma once

#include <vultra/drivers/profiling/profiler.hpp>

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
        // Frame and pass timestamp validity are independent.
        std::expected<void, std::string>
        addExpected(const FrameTiming& frame, std::span<const PassTiming> passes, bool gpuPassesValid);
        std::expected<void, std::string> writeExpected(const std::filesystem::path& directory,
                                                       const BenchmarkMetadata&     metadata,
                                                       const VriDeviceDesc&         device) const;

        // Legacy boundaries.
        void add(const FrameTiming& frame, std::span<const PassTiming> passes);
        void write(const std::filesystem::path& directory,
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
            bool                    gpuPassesValid = false;
        };

        std::vector<Sample> m_Samples;
    };
} // namespace vultra
