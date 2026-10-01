#include <vultra/drivers/profiling/profiler.hpp>

#include <cstring>
#include <format>

namespace vultra
{
    std::optional<std::string> FrameStatistics::addFrame(double seconds, double cpuMs, std::optional<double> gpuMs)
    {
        m_Seconds += seconds;
        m_CpuMs += cpuMs;
        m_GpuMs += gpuMs.value_or(0);
        m_HasGpu = m_HasGpu && gpuMs.has_value();
        ++m_Frames;
        if (m_Published && m_Seconds < 0.5)
        {
            return std::nullopt;
        }
        const double fps = m_Seconds > 0 ? double(m_Frames) / m_Seconds : 0;
        const auto   gpu = m_HasGpu ? std::format("{:.2f} ms", m_GpuMs / double(m_Frames)) : "N/A";
        auto suffix = std::format(" | FPS: {:.1f} | CPU: {:.2f} ms | GPU: {}", fps, m_CpuMs / double(m_Frames), gpu);
        m_Seconds   = 0;
        m_CpuMs     = 0;
        m_GpuMs     = 0;
        m_Frames    = 0;
        m_HasGpu    = true;
        m_Published = true;
        return suffix;
    }

    Profiler::Profiler(Device& device) :
        m_Device(device)
    {
        const auto* desc = device.core.GetDeviceDesc(device.handle);
        m_TickNs         = desc->timestampPeriodNanoseconds;
        if (!desc->hasTimestampQueries || m_TickNs <= 0)
        {
            return;
        }
        check(vriGetInterface(device.handle, VRI_INTERFACE_QUERY, sizeof(m_Api), &m_Api), "Get timestamp interface");
        m_Readback = std::make_unique<Buffer>(device,
                                              VriBufferDesc {kMaxPasses * 3 * sizeof(uint64_t),
                                                             0,
                                                             VriBufferUsage_TransferDst,
                                                             VriMemoryLocation_HostReadback});
        VriQueryPoolDesc query {VriQueryType_Timestamp, kMaxPasses * 3};
        check(m_Api.CreateQueryPool(device.handle, &query, &m_Pool), "Create timestamp pool");
    }

    Profiler::~Profiler()
    {
        m_Device.waitIdle();
        if (m_Pool)
        {
            m_Api.DestroyQueryPool(m_Pool);
        }
    }

    void Profiler::beginFrame(VriCommandBuffer* cmd)
    {
        m_Records.clear();
        m_SplitPasses.clear();
        m_QueryCount = 0;
        m_Open       = false;
        m_Split      = false;
        if (m_Pool)
        {
            m_Api.CmdResetQueries(cmd, m_Pool, 0, kMaxPasses * 3);
        }
    }

    void Profiler::beginPass(VriCommandBuffer* cmd, const std::string& name)
    {
        if (m_Open || m_Records.size() == kMaxPasses)
        {
            throw std::runtime_error("Profiler expects <=64 non-nested passes");
        }
        m_Open  = true;
        m_Split = false;
        if (m_Pool)
        {
            m_Api.CmdWriteTimestamp(cmd, m_Pool, m_QueryCount++);
        }
        m_Records.push_back({name});
        m_Begin = std::chrono::steady_clock::now();
    }

    void Profiler::beginCommands(VriCommandBuffer* cmd)
    {
        if (!m_Open || m_Split)
        {
            throw std::logic_error("Profiler expects one command boundary in an open pass");
        }
        m_Records.back().cpuBarrierMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Begin).count();
        if (m_Pool)
        {
            m_Api.CmdWriteTimestamp(cmd, m_Pool, m_QueryCount++);
        }
        m_Split = true;
    }

    void Profiler::endPass(VriCommandBuffer* cmd)
    {
        if (!m_Open)
        {
            throw std::logic_error("Unmatched profiler EndPass");
        }
        m_Records.back().cpuMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Begin).count();
        if (m_Pool)
        {
            m_Api.CmdWriteTimestamp(cmd, m_Pool, m_QueryCount++);
        }
        m_SplitPasses.push_back(m_Split);
        m_Open = false;
    }

    void Profiler::resolve(VriCommandBuffer* cmd)
    {
        if (m_Open)
        {
            throw std::logic_error("Unclosed profiler pass");
        }
        if (m_Pool && !m_Records.empty())
        {
            m_Api.CmdCopyQueries(cmd, m_Pool, 0, m_QueryCount, m_Readback->handle, 0);
        }
    }

    void Profiler::collect()
    {
        m_Results = m_Records;
        if (!m_Pool || m_Results.empty())
        {
            return;
        }
        auto* data = m_Device.core.MapBuffer(m_Readback->handle, 0, m_QueryCount * sizeof(uint64_t));
        if (!data)
        {
            throw std::runtime_error("Map timestamp results failed");
        }
        uint32_t query = 0;
        for (size_t i = 0; i < m_Results.size(); ++i)
        {
            uint64_t   ticks[3] {};
            const auto count = m_SplitPasses[i] ? 3u : 2u;
            std::memcpy(ticks, static_cast<const char*>(data) + query * sizeof(uint64_t), count * sizeof(uint64_t));
            m_Results[i].gpuMs = double(ticks[count - 1] - ticks[0]) * m_TickNs / 1e6;
            if (m_SplitPasses[i])
            {
                m_Results[i].gpuBarrierMs = double(ticks[1] - ticks[0]) * m_TickNs / 1e6;
            }
            query += count;
        }
        m_Device.core.UnmapBuffer(m_Readback->handle);
    }
} // namespace vultra
