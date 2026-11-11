#include <vultra/gpu/profiling/profiler.hpp>

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

    Profiler::Profiler(Device& device, uint32_t eventCapacity) :
        m_EventCapacity(eventCapacity),
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<Profiler>, Error> Profiler::create(Device& device, uint32_t eventCapacity)
    {
        if (eventCapacity == 0 || eventCapacity > UINT32_MAX / 3)
        {
            return makeError("Profiler event capacity must fit three queries per event");
        }
        std::unique_ptr<Profiler> profiler(new Profiler(device, eventCapacity));
        profiler->m_Records.reserve(eventCapacity);
        profiler->m_Results.reserve(eventCapacity);
        profiler->m_Stack.reserve(eventCapacity);
        const auto* desc   = device.core.GetDeviceDesc(device.handle);
        profiler->m_TickNs = desc->timestampPeriodNanoseconds;
        if (!desc->hasTimestampQueries || profiler->m_TickNs <= 0)
        {
            return profiler;
        }
        auto& api = profiler->m_Api;
        if (auto found = check(vriGetInterface(device.handle, VRI_INTERFACE_QUERY, sizeof(api), &api),
                               "Get timestamp interface");
            !found)
        {
            return std::unexpected(found.error());
        }
        auto readback = Buffer::create(device,
                                       VriBufferDesc {uint64_t(eventCapacity) * 3 * sizeof(uint64_t),
                                                      0,
                                                      VriBufferUsage_TransferDst,
                                                      VriMemoryLocation_HostReadback});
        if (!readback)
        {
            return std::unexpected(readback.error());
        }
        profiler->m_Readback = std::move(*readback);
        VriQueryPoolDesc query {VriQueryType_Timestamp, eventCapacity * 3};
        if (auto created =
                check(api.CreateQueryPool(device.handle, &query, &profiler->m_Pool), "Create timestamp pool");
            !created)
        {
            profiler->m_Pool = nullptr;
            return std::unexpected(created.error());
        }
        return profiler;
    }

    Profiler::~Profiler()
    {
        m_Device.waitIdleForDestruction();
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
            m_Api.CmdResetQueries(cmd, m_Pool, 0, m_EventCapacity * 3);
        }
    }

    std::expected<void, Error> Profiler::beginPass(VriCommandBuffer* cmd, const std::string& name)
    {
        if (m_Records.size() == m_EventCapacity)
        {
            return makeError("Profiler supports at most {} events per frame", m_EventCapacity);
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
        return {};
    }

    std::expected<void, Error> Profiler::beginCommands(VriCommandBuffer* cmd)
    {
        if (m_Stack.empty() || m_Records[m_Stack.back()].commandQuery != UINT32_MAX)
        {
            return makeError("Profiler expects one command boundary in an open event");
        }
        auto& record = m_Records[m_Stack.back()];
        record.timing.cpuBarrierMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - record.begin).count();
        record.commandQuery = m_QueryCount;
        if (m_Pool)
        {
            m_Api.CmdWriteTimestamp(cmd, m_Pool, m_QueryCount++);
        }
        return {};
    }

    std::expected<void, Error> Profiler::endPass(VriCommandBuffer* cmd)
    {
        if (m_Stack.empty())
        {
            return makeError("Unmatched profiler end event");
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
        return {};
    }

    std::expected<void, Error> Profiler::resolve(VriCommandBuffer* cmd)
    {
        if (!m_Stack.empty())
        {
            return makeError("Unclosed profiler event");
        }
        if (m_Pool && !m_Records.empty())
        {
            m_Api.CmdCopyQueries(cmd, m_Pool, 0, m_QueryCount, m_Readback->handle, 0);
        }
        return {};
    }

    std::expected<void, Error> Profiler::collect()
    {
        if (!m_Stack.empty())
        {
            return makeError("Complete profiler events before collecting");
        }
        m_Results.clear();
        for (const auto& record : m_Records)
        {
            m_Results.push_back(record.timing);
        }
        if (!m_Pool || m_Results.empty())
        {
            return {};
        }
        auto* data = m_Device.core.MapBuffer(m_Readback->handle, 0, m_QueryCount * sizeof(uint64_t));
        if (!data)
        {
            return makeError("Map timestamp results failed");
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
        return {};
    }
} // namespace vultra
