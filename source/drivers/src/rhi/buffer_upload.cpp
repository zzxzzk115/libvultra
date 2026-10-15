#include <vultra/drivers/rhi/buffer_upload.hpp>

#include <cstring>
#include <stdexcept>

namespace vultra
{
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
