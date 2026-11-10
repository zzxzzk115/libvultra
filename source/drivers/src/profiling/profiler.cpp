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

    Profiler::Profiler(Device& device, uint32_t eventCapacity, TimestampOrder order, Uninitialized) :
        m_EventCapacity(eventCapacity),
        m_TimestampOrder(order),
        m_Device(device)
    {
    }

    Profiler::Profiler(Device& device, uint32_t eventCapacity) :
        Profiler(device, eventCapacity, TimestampOrder::eNative, Uninitialized {})
    {
        if (auto status = initialize(); !status)
        {
            throw std::runtime_error(status.error());
        }
    }

    std::expected<std::unique_ptr<Profiler>, std::string>
    Profiler::create(Device& device, uint32_t eventCapacity, TimestampOrder order)
    {
        auto profiler = std::unique_ptr<Profiler>(new Profiler(device, eventCapacity, order, Uninitialized {}));
        if (auto status = profiler->initialize(); !status)
        {
            return std::unexpected(status.error());
        }
        return profiler;
    }

    std::expected<void, std::string> Profiler::initialize()
    {
        if (m_EventCapacity == 0 || m_EventCapacity > UINT32_MAX / 3)
        {
            return std::unexpected("Profiler event capacity must fit three queries per event");
        }
        m_Records.reserve(m_EventCapacity);
        m_Results.reserve(m_EventCapacity);
        m_Stack.reserve(m_EventCapacity);
        const auto* desc = m_Device.core.GetDeviceDesc(m_Device.handle);
        m_TickNs         = desc->timestampPeriodNanoseconds;
        if (!desc->hasTimestampQueries || m_TickNs <= 0)
        {
            return {};
        }
        if (m_TimestampOrder == TimestampOrder::eCompletedCommands &&
            (!desc->hasAliasMemoryBarrier || !m_Device.core.CmdAliasMemoryBarrier))
        {
            return std::unexpected("Ordered timestamps require a native encoder barrier");
        }
        if (vriGetInterface(m_Device.handle, VRI_INTERFACE_QUERY, sizeof(m_Api), &m_Api) != VriResult_Success)
        {
            return std::unexpected("Get timestamp interface failed");
        }
        auto readback = Buffer::create(m_Device,
                                       VriBufferDesc {uint64_t(m_EventCapacity) * 3 * sizeof(uint64_t),
                                                      0,
                                                      VriBufferUsage_TransferDst,
                                                      VriMemoryLocation_HostReadback});
        if (!readback)
        {
            return std::unexpected("Create timestamp readback buffer: " + readback.error());
        }
        m_Readback = std::move(*readback);
        VriQueryPoolDesc query {VriQueryType_Timestamp, m_EventCapacity * 3};
        if (m_Api.CreateQueryPool(m_Device.handle, &query, &m_Pool) != VriResult_Success || !m_Pool)
        {
            return std::unexpected("Create timestamp pool failed");
        }
        return {};
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
        m_Results.clear();
        m_Stack.clear();
        m_QueryCount   = 0;
        m_TimingsValid = true;
        if (m_Pool)
        {
            m_Api.CmdResetQueries(cmd, m_Pool, 0, m_EventCapacity * 3);
        }
    }

    std::expected<void, std::string> Profiler::writeTimestampExpected(VriCommandBuffer* commands)
    {
        if (!m_Pool)
        {
            return {};
        }
        const auto ordered = m_TimestampOrder == TimestampOrder::eCompletedCommands;
        if (ordered)
        {
            const auto status = m_Device.core.CmdAliasMemoryBarrier(commands);
            if (status != VriResult_Success)
            {
                return std::unexpected("Order GPU work before timestamp: " + std::to_string(status));
            }
        }
        m_Api.CmdWriteTimestamp(commands, m_Pool, m_QueryCount++);
        if (ordered)
        {
            const auto status = m_Device.core.CmdAliasMemoryBarrier(commands);
            if (status != VriResult_Success)
            {
                return std::unexpected("Order GPU work after timestamp: " + std::to_string(status));
            }
        }
        return {};
    }

    std::expected<void, std::string> Profiler::beginPassExpected(VriCommandBuffer* cmd, const std::string& name)
    {
        if (m_Records.size() == m_EventCapacity)
        {
            return std::unexpected(std::format("Profiler supports at most {} events per frame", m_EventCapacity));
        }
        Record record;
        record.timing.name   = name;
        record.timing.parent = m_Stack.empty() ? UINT32_MAX : m_Stack.back();
        record.timing.depth  = uint32_t(m_Stack.size());
        record.begin         = std::chrono::steady_clock::now();
        record.firstQuery    = m_QueryCount;
        m_Stack.push_back(uint32_t(m_Records.size()));
        m_Records.push_back(std::move(record));
        return writeTimestampExpected(cmd);
    }

    std::expected<void, std::string> Profiler::beginCommandsExpected(VriCommandBuffer* cmd)
    {
        if (m_Stack.empty() || m_Records[m_Stack.back()].commandQuery != UINT32_MAX)
        {
            return std::unexpected("Profiler expects one command boundary in an open event");
        }
        auto& record = m_Records[m_Stack.back()];
        record.timing.cpuBarrierMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - record.begin).count();
        record.commandQuery = m_QueryCount;
        return writeTimestampExpected(cmd);
    }

    std::expected<void, std::string> Profiler::endPassExpected(VriCommandBuffer* cmd)
    {
        if (m_Stack.empty())
        {
            return std::unexpected("Unmatched profiler end event");
        }
        auto& record = m_Records[m_Stack.back()];
        record.timing.cpuMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - record.begin).count();
        record.lastQuery = m_QueryCount;
        if (auto written = writeTimestampExpected(cmd); !written)
        {
            return written;
        }
        m_Stack.pop_back();
        return {};
    }

    std::expected<void, std::string> Profiler::resolveExpected(VriCommandBuffer* cmd)
    {
        if (!m_Stack.empty())
        {
            return std::unexpected("Unclosed profiler event");
        }
        if (m_Pool && !m_Records.empty())
        {
            m_Api.CmdCopyQueries(cmd, m_Pool, 0, m_QueryCount, m_Readback->handle, 0);
        }
        return {};
    }

    std::expected<void, std::string> Profiler::collectExpected()
    {
        if (!m_Stack.empty())
        {
            return std::unexpected("Complete profiler events before collecting");
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
            m_Results.clear();
            return std::unexpected("Map timestamp results failed");
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
            const auto& record   = m_Records[i];
            const auto  first    = tick(record.firstQuery);
            const auto  last     = tick(record.lastQuery);
            const auto  boundary = record.commandQuery != UINT32_MAX ? tick(record.commandQuery) : first;
            if (!first || !last || !boundary || last < first || boundary < first || boundary > last)
            {
                // The backend did not supply an ordered timestamp sequence. Without the counter's
                // valid bit count a wrap cannot be distinguished from an unavailable query.
                m_TimingsValid = false;
                break;
            }
            m_Results[i].gpuMs = double(last - first) * m_TickNs / 1e6;
            if (record.commandQuery != UINT32_MAX)
            {
                m_Results[i].gpuBarrierMs = double(boundary - first) * m_TickNs / 1e6;
            }
        }
        m_Device.core.UnmapBuffer(m_Readback->handle);
        if (!m_TimingsValid)
        {
            for (auto& result : m_Results)
            {
                result.gpuMs        = 0;
                result.gpuBarrierMs = 0;
            }
        }
        return {};
    }

    void Profiler::beginPass(VriCommandBuffer* cmd, const std::string& name)
    {
        if (auto result = beginPassExpected(cmd, name); !result)
        {
            throw std::runtime_error(result.error());
        }
    }

    void Profiler::beginCommands(VriCommandBuffer* cmd)
    {
        if (auto result = beginCommandsExpected(cmd); !result)
        {
            throw std::logic_error(result.error());
        }
    }

    void Profiler::endPass(VriCommandBuffer* cmd)
    {
        if (auto result = endPassExpected(cmd); !result)
        {
            throw std::logic_error(result.error());
        }
    }

    void Profiler::resolve(VriCommandBuffer* cmd)
    {
        if (auto result = resolveExpected(cmd); !result)
        {
            throw std::logic_error(result.error());
        }
    }

    void Profiler::collect()
    {
        if (auto result = collectExpected(); !result)
        {
            throw std::runtime_error(result.error());
        }
    }
} // namespace vultra
