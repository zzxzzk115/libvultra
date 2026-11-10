#include <vultra/drivers/rhi/buffer_readback.hpp>

#include <cstring>
#include <utility>

namespace vultra
{
    namespace
    {
        std::expected<uint64_t, std::string>
        readbackSizeExpected(Device& device, Buffer& source, uint64_t offset, uint64_t size)
        {
            if (!source.belongsTo(device) || !source.handle || !(source.desc.usage & VriBufferUsage_TransferSrc))
            {
                return std::unexpected("Buffer readback requires a same-device TransferSrc buffer");
            }
            if (offset >= source.desc.size || size > source.desc.size - offset)
            {
                return std::unexpected("Buffer readback byte range is empty or out of bounds");
            }
            const auto bytes = size ? size : source.desc.size - offset;
            if (bytes > std::vector<std::byte>().max_size())
            {
                return std::unexpected("Buffer readback byte range exceeds CPU storage limits");
            }
            return bytes;
        }
    } // namespace

    BufferReadback::BufferReadback(Device& device, uint64_t offset, uint64_t size, std::unique_ptr<Buffer> staging) :
        m_Device(device),
        m_Offset(offset),
        m_Size(size),
        m_Staging(std::move(staging))
    {
    }

    BufferReadback::~BufferReadback() = default;

    std::expected<std::unique_ptr<BufferReadback>, std::string>
    BufferReadback::create(Device& device, Buffer& source, uint64_t offset, uint64_t size)
    {
        auto bytes = readbackSizeExpected(device, source, offset, size);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        auto staging = Buffer::create(device, {*bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!staging)
        {
            return std::unexpected(staging.error());
        }
        return std::unique_ptr<BufferReadback>(new BufferReadback(device, offset, *bytes, std::move(*staging)));
    }

    std::expected<std::unique_ptr<BufferReadback>, std::string>
    BufferReadback::start(Device&                             device,
                          Buffer&                             source,
                          uint64_t                            offset,
                          uint64_t                            size,
                          std::span<const VriFenceSubmitDesc> waits,
                          std::span<const VriFenceSubmitDesc> signals)
    {
        auto result = create(device, source, offset, size);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        (*result)->m_Frame = std::move(*frame);
        auto commands      = (*result)->m_Frame->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = (*result)->recordIntoExpected(*commands, source); !recorded)
        {
            return std::unexpected(recorded.error());
        }
        if (auto submitted = (*result)->m_Frame->submitExpected(waits, signals); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        return result;
    }

    std::expected<void, std::string> BufferReadback::recordExpected(VriCommandBuffer* commands, Buffer& source)
    {
        if (m_Frame)
        {
            return std::unexpected("An asynchronous buffer readback owns its recording");
        }
        return recordIntoExpected(commands, source);
    }

    std::expected<void, std::string> BufferReadback::recordIntoExpected(VriCommandBuffer* commands, Buffer& source)
    {
        auto bytes = readbackSizeExpected(m_Device, source, m_Offset, m_Size);
        if (!commands || !bytes)
        {
            return std::unexpected(!commands ? "Buffer readback requires a command buffer" : bytes.error());
        }
        auto       state  = source.captureState();
        const auto reject = [&](const std::string& reason) -> std::expected<void, std::string>
        {
            auto reset = source.restoreUnsubmittedExpected(std::move(state));
            return std::unexpected(reason + (reset ? "" : "; restore buffer tracker: " + reset.error()));
        };
        if (auto ready = source.transitionExpected(commands,
                                                   {VriAccess_CopySourceRead, VriPipelineStage_Transfer},
                                                   m_Offset,
                                                   m_Size);
            !ready)
        {
            return reject(ready.error());
        }
        if (auto ready =
                m_Staging->transitionExpected(commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            !ready)
        {
            return reject(ready.error());
        }
        const VriBufferCopyDesc copy {m_Offset, 0, m_Size};
        m_Device.core.CmdCopyBuffer(commands, m_Staging->handle, source.handle, &copy);
        if (auto restored = source.restoreExpected(commands, state, m_Offset, m_Size); !restored)
        {
            return reject(restored.error());
        }
        m_Recorded = true;
        return {};
    }

    std::expected<void, std::string> BufferReadback::waitExpected(uint64_t timeoutNs)
    {
        if (!m_Frame)
        {
            return std::unexpected("Externally recorded buffer readback completion belongs to its caller");
        }
        return m_Frame->waitExpected(timeoutNs);
    }

    std::expected<BufferReadbackData, std::string> BufferReadback::dataExpected(uint64_t timeoutNs)
    {
        if (!m_Recorded)
        {
            return std::unexpected("Buffer readback has not been recorded");
        }
        if (m_Frame)
        {
            if (auto completed = m_Frame->waitExpected(timeoutNs); !completed)
            {
                return std::unexpected(completed.error());
            }
        }
        BufferReadbackData result {m_Offset, std::vector<std::byte>(size_t(m_Size))};
        const auto*        source = m_Device.core.MapBuffer(m_Staging->handle, 0, m_Size);
        if (!source)
        {
            return std::unexpected("Map buffer readback staging failed");
        }
        std::memcpy(result.bytes.data(), source, size_t(m_Size));
        m_Device.core.UnmapBuffer(m_Staging->handle);
        return result;
    }

    std::expected<BufferReadbackData, std::string>
    readbackBufferExpected(Device& device, Buffer& source, uint64_t offset, uint64_t size)
    {
        auto readback = BufferReadback::start(device, source, offset, size);
        if (!readback)
        {
            return std::unexpected(readback.error());
        }
        return (*readback)->dataExpected();
    }
} // namespace vultra
