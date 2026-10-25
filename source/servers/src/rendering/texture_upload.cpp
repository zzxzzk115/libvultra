#include "texture_layout.hpp"

#include <vultra/servers/rendering/texture_upload.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <numeric>
#include <set>
#include <span>

namespace vultra
{
    namespace
    {
        VriFormat gpuFormat(TextureFormat format)
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

        struct TextureSubresourceView
        {
            Extent                     size;
            uint32_t                   depth;
            uint32_t                   mip;
            uint32_t                   layer;
            std::span<const std::byte> bytes;
        };

        struct TextureUploadView
        {
            TextureDimension                    dimension;
            TextureFormat                       format;
            uint32_t                            layers;
            uint32_t                            mips;
            std::vector<TextureSubresourceView> subresources;
        };

        std::unique_ptr<Texture> uploadTexture(Device& device, const TextureUploadView& data)
        {
            if (data.subresources.empty() || data.layers == 0 || data.mips == 0 || data.mips > 32)
            {
                throw std::invalid_argument("Texture asset requires a complete subresource chain");
            }
            const auto& first = data.subresources.front();
            if (first.mip != 0 || first.layer != 0 || first.size.empty() || first.depth == 0)
            {
                throw std::invalid_argument("Texture asset must start with mip zero, layer zero");
            }
            auto desc = colorTexture(first.size, gpuFormat(data.format));
            switch (data.dimension)
            {
                case TextureDimension::e2D:
                    desc.type = VriTextureType_2D;
                    break;
                case TextureDimension::e2DArray:
                    desc.type = VriTextureType_2DArray;
                    break;
                case TextureDimension::e3D:
                    desc.type = VriTextureType_3D;
                    break;
                case TextureDimension::eCube:
                    desc.type = VriTextureType_Cube;
                    break;
                case TextureDimension::eCubeArray:
                    desc.type = VriTextureType_CubeArray;
                    break;
                default:
                    throw std::invalid_argument("Invalid texture asset dimension");
            }
            const bool volume = data.dimension == TextureDimension::e3D;
            const bool cube =
                data.dimension == TextureDimension::eCube || data.dimension == TextureDimension::eCubeArray;
            if ((volume && data.layers != 1) || (!volume && first.depth != 1) ||
                (data.dimension == TextureDimension::e2D && data.layers != 1) ||
                (cube && (data.layers % 6 != 0 || first.size.width != first.size.height)) ||
                (data.dimension == TextureDimension::eCube && data.layers != 6) ||
                data.subresources.size() != uint64_t(data.layers) * data.mips)
            {
                throw std::invalid_argument("Texture asset dimensions and subresources disagree");
            }
            if (data.mips > std::bit_width(std::max({first.size.width, first.size.height, first.depth})))
            {
                throw std::invalid_argument("Texture asset mip count exceeds its dimensions");
            }
            desc.depth    = first.depth;
            desc.layerNum = data.layers;
            desc.mipNum   = data.mips;
            desc.usage    = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc;
            const auto layout = asset_detail::textureLayout(data.format);
            const auto rowAlignment =
                std::lcm(layout.bytes,
                         std::max(1u, device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment));

            struct Copy
            {
                uint64_t offset;
                uint32_t rowBytes;
                uint32_t rows;
                uint32_t packedRow;
            };

            std::vector<Copy>                       copies;
            std::set<std::pair<uint32_t, uint32_t>> seen;
            uint64_t                                total = 0;
            for (const auto& resource : data.subresources)
            {
                if (resource.mip >= data.mips)
                {
                    throw std::invalid_argument("Texture asset mip is outside its chain");
                }
                const Extent expected {std::max(first.size.width >> resource.mip, 1u),
                                       std::max(first.size.height >> resource.mip, 1u)};
                const auto   depth = volume ? std::max(first.depth >> resource.mip, 1u) : 1u;
                if (resource.layer >= data.layers || resource.size != expected || resource.depth != depth ||
                    !seen.emplace(resource.mip, resource.layer).second)
                {
                    throw std::invalid_argument("Invalid or duplicate texture asset subresource");
                }
                const uint32_t packedRow = (resource.size.width + layout.block - 1) / layout.block * layout.bytes;
                const uint32_t rows      = (resource.size.height + layout.block - 1) / layout.block;
                if (resource.bytes.size() != uint64_t(packedRow) * rows * resource.depth)
                {
                    throw std::invalid_argument("Texture asset subresource byte count");
                }
                const auto rowBytes = (packedRow + rowAlignment - 1) / rowAlignment * rowAlignment;
                copies.push_back({total, rowBytes, rows, packedRow});
                total = (total + uint64_t(rowBytes) * rows * resource.depth + 511) & ~uint64_t(511);
            }
            Buffer staging(device, {total, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            auto*  mapped = static_cast<std::byte*>(device.core.MapBuffer(staging.handle, 0, total));
            if (!mapped)
            {
                throw std::runtime_error("Map texture asset staging buffer");
            }
            for (size_t i = 0; i < copies.size(); ++i)
            {
                const auto& copy     = copies[i];
                const auto& resource = data.subresources[i];
                for (uint64_t row = 0; row < uint64_t(copy.rows) * resource.depth; ++row)
                {
                    std::memcpy(mapped + copy.offset + row * copy.rowBytes,
                                resource.bytes.data() + row * copy.packedRow,
                                copy.packedRow);
                }
            }
            device.core.UnmapBuffer(staging.handle);
            auto  texture = std::make_unique<Texture>(device, desc);
            Frame frame(device);
            auto* commands = frame.begin();
            texture->transition(commands,
                                {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            for (size_t i = 0; i < copies.size(); ++i)
            {
                const auto&              resource   = data.subresources[i];
                const auto&              layoutCopy = copies[i];
                VriBufferTextureCopyDesc copy {};
                copy.bufferOffset      = layoutCopy.offset;
                copy.bufferRowLength   = layoutCopy.rowBytes / layout.bytes * layout.block;
                copy.bufferImageHeight = layoutCopy.rows * layout.block;
                copy.texture           = {resource.mip,
                                          resource.layer,
                                          1,
                                          VriImageAspect_Color,
                                          0,
                                          0,
                                          0,
                                          resource.size.width,
                                          resource.size.height,
                                          resource.depth};
                device.core.CmdUploadBufferToTexture(commands, texture->handle, staging.handle, &copy);
            }
            texture->transition(commands,
                                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
            frame.submitAndWait();
            return texture;
        }
    } // namespace

    std::unique_ptr<Texture> uploadTextureAsset(Device& device, const TextureAssetData& data)
    {
        TextureUploadView view {data.dimension, data.format, data.layers, data.mips, {}};
        view.subresources.reserve(data.subresources.size());
        for (const auto& resource : data.subresources)
        {
            view.subresources.push_back({resource.size, resource.depth, resource.mip, resource.layer, resource.bytes});
        }
        return uploadTexture(device, view);
    }

    std::unique_ptr<Texture> uploadTextureAsset(Device& device, const TextureData& data)
    {
        TextureUploadView view {TextureDimension::e2D, data.format, 1, uint32_t(data.levels.size()), {}};
        view.subresources.reserve(data.levels.size());
        for (uint32_t mip = 0; mip < data.levels.size(); ++mip)
        {
            const auto& level = data.levels[mip];
            view.subresources.push_back({level.size, 1, mip, 0, level.bytes});
        }
        return uploadTexture(device, view);
    }
} // namespace vultra
