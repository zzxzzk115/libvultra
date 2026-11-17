#include <vultra/gpu/buffer_upload.hpp>

#include <cstring>

namespace vultra
{
    std::expected<Buffer, Error> uploadBuffer(Device&                    device,
                                              std::span<const std::byte> data,
                                              VriBufferUsageFlags        usage,
                                              VriAccessStage             ready,
                                              uint32_t                   structureStride)
    {
        if (data.empty())
        {
            return makeError("Cannot upload an empty buffer");
        }
        auto staging =
            Buffer::create(device, {data.size_bytes(), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        if (!staging)
        {
            return std::unexpected(staging.error());
        }
        void* mapped = device.core.MapBuffer(staging->handle, 0, data.size_bytes());
        if (!mapped)
        {
            return makeError("Map staging buffer failed");
        }
        std::memcpy(mapped, data.data(), data.size_bytes());
        device.core.UnmapBuffer(staging->handle);

        auto result = Buffer::create(device,
                                     VriBufferDesc {data.size_bytes(),
                                                    structureStride,
                                                    usage | VriBufferUsage_TransferDst,
                                                    VriMemoryLocation_Device});
        if (!result)
        {
            return std::unexpected(result.error());
        }
        const auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto cmd = (*frame)->begin();
        if (!cmd)
        {
            return std::unexpected(cmd.error());
        }
        result->transition(*cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        VriBufferCopyDesc copy {};
        copy.size = data.size_bytes();
        device.core.CmdCopyBuffer(*cmd, result->handle, staging->handle, &copy);
        result->transition(*cmd, ready);
        if (auto submitted = (*frame)->submitAndWait(); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        return result;
    }

    std::expected<std::vector<std::byte>, Error> readbackBuffer(Device& device, Buffer& buffer)
    {
        if (!(buffer.desc.usage & VriBufferUsage_TransferSrc) || buffer.desc.size == 0)
        {
            return makeError("Buffer readback requires a non-empty TransferSrc buffer");
        }
        auto staging =
            Buffer::create(device, {buffer.desc.size, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!staging)
        {
            return std::unexpected(staging.error());
        }
        const auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto cmd = (*frame)->begin();
        if (!cmd)
        {
            return std::unexpected(cmd.error());
        }
        const auto previous = buffer.state;
        buffer.transition(*cmd, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        VriBufferCopyDesc copy {};
        copy.size = buffer.desc.size;
        device.core.CmdCopyBuffer(*cmd, staging->handle, buffer.handle, &copy);
        buffer.transition(*cmd, previous);
        if (auto submitted = (*frame)->submitAndWait(); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        const auto* data = static_cast<const std::byte*>(device.core.MapBuffer(staging->handle, 0, buffer.desc.size));
        if (!data)
        {
            return makeError("Map buffer readback failed");
        }
        std::vector<std::byte> result(data, data + buffer.desc.size);
        device.core.UnmapBuffer(staging->handle);
        return result;
    }
} // namespace vultra
