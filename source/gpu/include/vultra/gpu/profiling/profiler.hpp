#pragma once
#include <vultra/gpu/resources.hpp>

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vultra
{
    // Aggregate completed frames; publish a title suffix on the first frame and every half second.
    class FrameStatistics
    {
    public:
        std::optional<std::string> addFrame(double seconds, double cpuMs, std::optional<double> gpuMs);

    private:
        double   m_Seconds   = 0;
        double   m_CpuMs     = 0;
        double   m_GpuMs     = 0;
        uint64_t m_Frames    = 0;
        bool     m_HasGpu    = true;
        bool     m_Published = false;
    };

    struct FrameTiming
    {
        uint64_t              frameIndex      = 0;
        double                totalMs         = 0;
        double                updateMs        = 0;
        double                acquireMs       = 0;
        double                prepareRecordMs = 0;
        double                submitWaitMs    = 0;
        double                postRenderMs    = 0;
        double                presentMs       = 0;
        std::optional<double> gpuMs;
    };

    struct PassTiming
    {
        std::string name;
        double      cpuMs        = 0;
        double      gpuMs        = 0;
        double      cpuBarrierMs = 0;
        double      gpuBarrierMs = 0;
        uint32_t    parent       = UINT32_MAX; // Index in this frame's event list; times include child events.
        uint32_t    depth        = 0;
    };

    // Recorders keep a reference while a frame is open, so the profiler lives behind a unique_ptr.
    class Profiler
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<Profiler>, Error> create(Device&  device,
                                                                                    uint32_t eventCapacity = 64);
        ~Profiler();
        Profiler(const Profiler&)                                           = delete;
        Profiler&                                operator=(const Profiler&) = delete;
        void                                     beginFrame(VriCommandBuffer* cmd);
        [[nodiscard]] std::expected<void, Error> beginPass(VriCommandBuffer* cmd, const std::string& name);
        // Optional boundary after graph barriers.
        [[nodiscard]] std::expected<void, Error> beginCommands(VriCommandBuffer* cmd);
        [[nodiscard]] std::expected<void, Error> endPass(VriCommandBuffer* cmd);
        [[nodiscard]] std::expected<void, Error> resolve(VriCommandBuffer* cmd);
        // Only after Frame::submitAndWait.
        [[nodiscard]] std::expected<void, Error> collect();

        bool hasGpuTimings() const
        {
            return m_Pool != nullptr;
        }

        const std::vector<PassTiming>& timings() const
        {
            return m_Results;
        }

    private:
        struct Record
        {
            PassTiming                            timing;
            std::chrono::steady_clock::time_point begin;
            uint32_t                              firstQuery   = 0;
            uint32_t                              commandQuery = UINT32_MAX;
            uint32_t                              lastQuery    = 0;
        };

        Profiler(Device& device, uint32_t eventCapacity);

        uint32_t                m_EventCapacity;
        Device&                 m_Device;
        VriQueryInterface       m_Api {};
        VriQueryPool*           m_Pool = nullptr;
        std::optional<Buffer>   m_Readback;
        std::vector<Record>     m_Records;
        std::vector<PassTiming> m_Results;
        std::vector<uint32_t>   m_Stack;
        double                  m_TickNs     = 0;
        uint32_t                m_QueryCount = 0;
    };
} // namespace vultra
