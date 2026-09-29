#pragma once
#include <vultra/core/rhi/resources.hpp>

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

    struct PassTiming
    {
        std::string name;
        double      cpuMs = 0;
        double      gpuMs = 0;
    };

    class Profiler
    {
    public:
        explicit Profiler(Device& device);
        ~Profiler();
        Profiler(const Profiler&)            = delete;
        Profiler& operator=(const Profiler&) = delete;
        void      beginFrame(VriCommandBuffer* cmd);
        void      beginPass(VriCommandBuffer* cmd, const std::string& name);
        void      endPass(VriCommandBuffer* cmd);
        void      resolve(VriCommandBuffer* cmd);
        void      collect(); // only after Frame::submitAndWait

        bool hasGpuTimings() const
        {
            return m_Pool != nullptr;
        }

        const std::vector<PassTiming>& timings() const
        {
            return m_Results;
        }

    private:
        static constexpr uint32_t             kMaxPasses = 64;
        Device&                               m_Device;
        VriQueryInterface                     m_Api {};
        VriQueryPool*                         m_Pool = nullptr;
        std::unique_ptr<Buffer>               m_Readback;
        std::vector<PassTiming>               m_Records;
        std::vector<PassTiming>               m_Results;
        std::chrono::steady_clock::time_point m_Begin;
        double                                m_TickNs = 0;
        bool                                  m_Open   = false;
    };
} // namespace vultra
