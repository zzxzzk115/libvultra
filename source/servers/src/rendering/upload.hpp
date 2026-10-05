#pragma once

#include "texture_layout.hpp"

#include <vultra/assets/texture_import.hpp>
#include <vultra/drivers/rhi/resources.hpp>

#include <cstring>
#include <memory>
#include <span>

namespace vultra
{
    inline VriFormat gpuFormat(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::eR8Unorm:
                return VriFormat_R8_UNORM;
            case TextureFormat::eRg8Unorm:
                return VriFormat_RG8_UNORM;
            case TextureFormat::eRgba8Unorm:
                return VriFormat_RGBA8_UNORM;
            case TextureFormat::eRgba8Srgb:
                return VriFormat_RGBA8_SRGB;
            case TextureFormat::eBgra8Unorm:
                return VriFormat_BGRA8_UNORM;
            case TextureFormat::eBgra8Srgb:
                return VriFormat_BGRA8_SRGB;
            case TextureFormat::eRgba16Sfloat:
                return VriFormat_RGBA16_SFLOAT;
            case TextureFormat::eRgba32Sfloat:
                return VriFormat_RGBA32_SFLOAT;
            case TextureFormat::eBc1Unorm:
                return VriFormat_BC1_UNORM;
            case TextureFormat::eBc2Unorm:
                return VriFormat_BC2_UNORM;
            case TextureFormat::eBc3Unorm:
                return VriFormat_BC3_UNORM;
            case TextureFormat::eBc4Unorm:
                return VriFormat_BC4_UNORM;
            case TextureFormat::eBc5Unorm:
                return VriFormat_BC5_UNORM;
            case TextureFormat::eBc6hUfloat:
                return VriFormat_BC6H_UFLOAT;
            case TextureFormat::eBc7Unorm:
                return VriFormat_BC7_UNORM;
            default:
                throw std::invalid_argument("Unsupported GPU asset texture format");
        }
    }

    inline std::unique_ptr<Texture>
    uploadTexture(Device& device, TextureFormat format, uint32_t texelBytes, const std::vector<TextureLevel>& levels)
    {
        if (levels.empty() || levels.front().size.empty())
        {
            throw std::invalid_argument("Texture upload requires pixels");
        }
        const uint32_t block = asset_detail::textureLayout(format).block;
        auto           desc  = colorTexture(levels.front().size, gpuFormat(format));
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
                                                        VriAccessStage             ready,
                                                        uint32_t                   structureStride = 0)
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

        auto          result = std::make_unique<vultra::Buffer>(device,
                                                                VriBufferDesc {data.size_bytes(),
                                                                               structureStride,
                                                                               usage | VriBufferUsage_TransferDst,
                                                                               VriMemoryLocation_Device});
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
