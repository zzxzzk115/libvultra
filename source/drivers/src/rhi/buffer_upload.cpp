#include <vultra/drivers/rhi/buffer_upload.hpp>

#include <cstring>
#include <stdexcept>

namespace vultra
{
    std::expected<std::unique_ptr<Buffer>, std::string>
    uploadBufferExpected(Device& device, std::span<const std::byte> data, VriBufferUsageFlags usage,
                         VriAccessStage ready, uint32_t structureStride)
    {
        if (data.empty())
            return std::unexpected("Cannot upload an empty buffer");
        auto staging = Buffer::create(device,
            {data.size_bytes(), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        if (!staging)
            return std::unexpected(staging.error());
        void* mapped = device.core.MapBuffer((*staging)->handle, 0, data.size_bytes());
        if (!mapped)
            return std::unexpected("Map staging buffer failed");
        std::memcpy(mapped, data.data(), data.size_bytes());
        device.core.UnmapBuffer((*staging)->handle);
        auto result = Buffer::create(device, {data.size_bytes(), structureStride,
                                              usage | VriBufferUsage_TransferDst, VriMemoryLocation_Device});
        if (!result)
            return std::unexpected(result.error());
        auto frame = Frame::create(device);
        if (!frame)
            return std::unexpected(frame.error());
        auto cmd = (*frame)->beginExpected();
        if (!cmd)
            return std::unexpected(cmd.error());
        (*result)->transition(*cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        VriBufferCopyDesc copy {};
        copy.size = data.size_bytes();
        device.core.CmdCopyBuffer(*cmd, (*result)->handle, (*staging)->handle, &copy);
        (*result)->transition(*cmd, ready);
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            return std::unexpected(submitted.error());
        return result;
    }

    std::unique_ptr<Buffer> uploadBuffer(Device&                    device,
                                         std::span<const std::byte> data,
                                         VriBufferUsageFlags        usage,
                                         VriAccessStage             ready,
                                         uint32_t                   structureStride)
    {
        if (data.empty())
        {
            throw std::invalid_argument("Cannot upload an empty buffer");
        }
        Buffer staging(device, {data.size_bytes(), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        void*  mapped = device.core.MapBuffer(staging.handle, 0, data.size_bytes());
        if (!mapped)
        {
            throw std::runtime_error("Map staging buffer failed");
        }
        std::memcpy(mapped, data.data(), data.size_bytes());
        device.core.UnmapBuffer(staging.handle);

        auto  result = std::make_unique<Buffer>(device,
                                                VriBufferDesc {data.size_bytes(),
                                                               structureStride,
                                                               usage | VriBufferUsage_TransferDst,
                                                               VriMemoryLocation_Device});
        Frame frame(device);
        auto* cmd = frame.begin();
        result->transition(cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        VriBufferCopyDesc copy {};
        copy.size = data.size_bytes();
        device.core.CmdCopyBuffer(cmd, result->handle, staging.handle, &copy);
        result->transition(cmd, ready);
        frame.submitAndWait();
        return result;
    }
} // namespace vultra
