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
        m_Records.reserve(kMaxPasses);
        m_Results.reserve(kMaxPasses);
        m_Stack.reserve(kMaxPasses);
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
        m_Stack.clear();
        m_QueryCount = 0;
        if (m_Pool)
        {
            m_Api.CmdResetQueries(cmd, m_Pool, 0, kMaxPasses * 3);
        }
    }

    void Profiler::beginPass(VriCommandBuffer* cmd, const std::string& name)
    {
        if (m_Records.size() == kMaxPasses)
        {
            throw std::runtime_error("Profiler supports at most 64 events per frame");
        }
        Record record;
        record.timing.name   = name;
        record.timing.parent = m_Stack.empty() ? UINT32_MAX : m_Stack.back();
        record.timing.depth  = uint32_t(m_Stack.size());
        record.begin         = std::chrono::steady_clock::now();
        record.firstQuery    = m_QueryCount;
        m_Stack.push_back(uint32_t(m_Records.size()));
        m_Records.push_back(std::move(record));
        if (m_Pool)
        {
            m_Api.CmdWriteTimestamp(cmd, m_Pool, m_QueryCount++);
        }
    }

    void Profiler::beginCommands(VriCommandBuffer* cmd)
    {
        if (m_Stack.empty() || m_Records[m_Stack.back()].commandQuery != UINT32_MAX)
        {
            throw std::logic_error("Profiler expects one command boundary in an open event");
        }
        auto& record = m_Records[m_Stack.back()];
        record.timing.cpuBarrierMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - record.begin).count();
        record.commandQuery = m_QueryCount;
        if (m_Pool)
        {
            m_Api.CmdWriteTimestamp(cmd, m_Pool, m_QueryCount++);
        }
    }

    void Profiler::endPass(VriCommandBuffer* cmd)
    {
        if (m_Stack.empty())
        {
            throw std::logic_error("Unmatched profiler end event");
        }
        auto& record = m_Records[m_Stack.back()];
        record.timing.cpuMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - record.begin).count();
        record.lastQuery = m_QueryCount;
        if (m_Pool)
        {
            m_Api.CmdWriteTimestamp(cmd, m_Pool, m_QueryCount++);
        }
        m_Stack.pop_back();
    }

    void Profiler::resolve(VriCommandBuffer* cmd)
    {
        if (!m_Stack.empty())
        {
            throw std::logic_error("Unclosed profiler event");
        }
        if (m_Pool && !m_Records.empty())
        {
            m_Api.CmdCopyQueries(cmd, m_Pool, 0, m_QueryCount, m_Readback->handle, 0);
        }
    }

    void Profiler::collect()
    {
        if (!m_Stack.empty())
        {
            throw std::logic_error("Complete profiler events before collecting");
        }
        m_Results.clear();
        for (const auto& record : m_Records)
        {
            m_Results.push_back(record.timing);
        }
        if (!m_Pool || m_Results.empty())
        {
            return;
        }
        auto* data = m_Device.core.MapBuffer(m_Readback->handle, 0, m_QueryCount * sizeof(uint64_t));
        if (!data)
        {
            throw std::runtime_error("Map timestamp results failed");
        }
        const auto tick = [&](uint32_t query)
        {
            uint64_t result = 0;
            std::memcpy(&result, static_cast<const char*>(data) + query * sizeof(uint64_t), sizeof(result));
            return result;
        };
        // Nested timestamps interleave. Each event keeps its own query indices instead of assuming adjacency.
        for (size_t i = 0; i < m_Results.size(); ++i)
        {
            const auto& record = m_Records[i];
            m_Results[i].gpuMs = double(tick(record.lastQuery) - tick(record.firstQuery)) * m_TickNs / 1e6;
            if (record.commandQuery != UINT32_MAX)
            {
                m_Results[i].gpuBarrierMs =
                    double(tick(record.commandQuery) - tick(record.firstQuery)) * m_TickNs / 1e6;
            }
        }
        m_Device.core.UnmapBuffer(m_Readback->handle);
    }
} // namespace vultra
