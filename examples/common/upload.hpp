#pragma once

#include <vultra/drivers/rhi/resources.hpp>

#include <cstring>
#include <memory>
#include <span>

namespace sample
{
    inline std::unique_ptr<vultra::Buffer> uploadBuffer(vultra::Device&            device,
                                                        std::span<const std::byte> data,
                                                        VriBufferUsageFlags        usage,
                                                        VriAccessStage             ready)
    {
        if (data.empty())
        {
            throw std::invalid_argument("Cannot upload an empty buffer");
        }
        vultra::Buffer staging(device,
                               {data.size_bytes(), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        void*          mapped = device.core.MapBuffer(staging.handle, 0, data.size_bytes());
        if (!mapped)
        {
            throw std::runtime_error("Map staging buffer failed");
        }
        std::memcpy(mapped, data.data(), data.size_bytes());
        device.core.UnmapBuffer(staging.handle);

        auto result = std::make_unique<vultra::Buffer>(
            device,
            VriBufferDesc {data.size_bytes(), 0, usage | VriBufferUsage_TransferDst, VriMemoryLocation_Device});
        vultra::Frame frame(device);
        auto*         cmd = frame.begin();
        result->transition(cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        VriBufferCopyDesc copy {};
        copy.size = data.size_bytes();
        device.core.CmdCopyBuffer(cmd, result->handle, staging.handle, &copy);
        result->transition(cmd, ready);
        frame.submitAndWait();
        return result;
    }
} // namespace sample
