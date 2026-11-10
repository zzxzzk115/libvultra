#pragma once
#include <vultra/drivers/rhi/resources.hpp>

#include <chrono>
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

    class Profiler
    {
    public:
        enum class TimestampOrder
        {
            eNative,
            eCompletedCommands // Gate each marker against earlier and later encoder work.
        };

        explicit Profiler(Device& device, uint32_t eventCapacity = 64);
        static std::expected<std::unique_ptr<Profiler>, std::string>
        create(Device& device, uint32_t eventCapacity = 64, TimestampOrder order = TimestampOrder::eNative);
        ~Profiler();
        Profiler(const Profiler&)                                   = delete;
        Profiler&                        operator=(const Profiler&) = delete;
        void                             beginFrame(VriCommandBuffer* cmd);
        void                             beginPass(VriCommandBuffer* cmd, const std::string& name);
        void                             beginCommands(VriCommandBuffer* cmd); // optional boundary after graph barriers
        void                             endPass(VriCommandBuffer* cmd);
        void                             resolve(VriCommandBuffer* cmd);
        void                             collect(); // only after Frame::submitAndWait
        std::expected<void, std::string> beginPassExpected(VriCommandBuffer* cmd, const std::string& name);
        std::expected<void, std::string> beginCommandsExpected(VriCommandBuffer* cmd);
        std::expected<void, std::string> endPassExpected(VriCommandBuffer* cmd);
        std::expected<void, std::string> resolveExpected(VriCommandBuffer* cmd);
        std::expected<void, std::string> collectExpected(); // only after Frame::submitAndWait

        bool belongsTo(const Device& device) const
        {
            return &m_Device == &device;
        }

        bool hasGpuTimings() const
        {
            return m_Pool != nullptr && m_TimingsValid;
        }

        const std::vector<PassTiming>& timings() const
        {
            return m_Results;
        }

    private:
        struct Uninitialized
        {};

        Profiler(Device& device, uint32_t eventCapacity, TimestampOrder order, Uninitialized);
        std::expected<void, std::string> initialize();
        std::expected<void, std::string> writeTimestampExpected(VriCommandBuffer* commands);

        struct Record
        {
            PassTiming                            timing;
            std::chrono::steady_clock::time_point begin;
            uint32_t                              firstQuery   = 0;
            uint32_t                              commandQuery = UINT32_MAX;
            uint32_t                              lastQuery    = 0;
        };

        uint32_t                m_EventCapacity;
        TimestampOrder          m_TimestampOrder;
        Device&                 m_Device;
        VriQueryInterface       m_Api {};
        VriQueryPool*           m_Pool = nullptr;
        std::unique_ptr<Buffer> m_Readback;
        std::vector<Record>     m_Records;
        std::vector<PassTiming> m_Results;
        std::vector<uint32_t>   m_Stack;
        double                  m_TickNs       = 0;
        uint32_t                m_QueryCount   = 0;
        bool                    m_TimingsValid = true;
    };
} // namespace vultra
