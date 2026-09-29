#pragma once

#include "../asset/texture_layout.hpp"

#include <vultra/core/rhi/resources.hpp>
#include <vultra/function/asset/texture_import.hpp>

#include <cstring>
#include <memory>
#include <span>

namespace vultra
{
    inline std::unique_ptr<Texture>
    uploadTexture(Device& device, VriFormat format, uint32_t texelBytes, const std::vector<TextureLevel>& levels)
    {
        if (levels.empty() || levels.front().size.empty())
        {
            throw std::invalid_argument("Texture upload requires pixels");
        }
        const uint32_t block = asset_detail::textureLayout(format).block;
        auto           desc  = colorTexture(levels.front().size, format);
        desc.mipNum          = uint32_t(levels.size());
        desc.usage = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc;
        auto                  texture = std::make_unique<Texture>(device, desc);
        std::vector<uint64_t> offsets;
        uint64_t              bytes = 0;
        for (const auto& level : levels)
        {
            offsets.push_back(bytes);
            if (level.bytes.size() != uint64_t((level.size.width + block - 1) / block) *
                                          ((level.size.height + block - 1) / block) * texelBytes)
            {
                throw std::invalid_argument("Texture upload byte count");
            }
            const uint32_t rowBytes = (((level.size.width + block - 1) / block) * texelBytes + 255) & ~255u;
            bytes = (bytes + uint64_t(rowBytes) * ((level.size.height + block - 1) / block) + 511) & ~uint64_t(511);
        }
        Buffer staging(device, {bytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto*  mapped = static_cast<std::byte*>(device.core.MapBuffer(staging.handle, 0, bytes));
        if (!mapped)
        {
            throw std::runtime_error("Map texture upload");
        }
        for (size_t mip = 0; mip < levels.size(); ++mip)
        {
            const auto&    level    = levels[mip];
            const uint32_t rowBytes = (((level.size.width + block - 1) / block) * texelBytes + 255) & ~255u;
            for (uint32_t y = 0; y < (level.size.height + block - 1) / block; ++y)
            {
                std::memcpy(mapped + offsets[mip] + uint64_t(y) * rowBytes,
                            level.bytes.data() + uint64_t(y) * ((level.size.width + block - 1) / block) * texelBytes,
                            ((level.size.width + block - 1) / block) * texelBytes);
            }
        }
        device.core.UnmapBuffer(staging.handle);
        Frame frame(device);
        auto* cmd = frame.begin();
        texture->transition(cmd,
                            {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
        for (uint32_t mip = 0; mip < levels.size(); ++mip)
        {
            const auto               size = levels[mip].size;
            VriBufferTextureCopyDesc copy {};
            copy.bufferOffset = offsets[mip];
            copy.bufferRowLength =
                ((((size.width + block - 1) / block) * texelBytes + 255) & ~255u) / texelBytes * block;
            copy.bufferImageHeight = (size.height + block - 1) / block * block;
            copy.texture           = {mip, 0, 1, VriImageAspect_Color, 0, 0, 0, size.width, size.height, 1};
            device.core.CmdUploadBufferToTexture(cmd, texture->handle, staging.handle, &copy);
        }
        texture->transition(cmd,
                            {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
        frame.submitAndWait();
        return texture;
    }

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
} // namespace vultra
