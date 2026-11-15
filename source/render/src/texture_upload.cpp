#include "texture_layout.hpp"

#include <vultra/render/texture_upload.hpp>

#include <basisu/transcoder/basisu_transcoder.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <mutex>
#include <numeric>
#include <set>
#include <span>
#include <utility>

namespace vultra
{
    namespace
    {
        std::expected<VriFormat, Error> gpuFormat(TextureFormat format)
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
                    return makeError("Unsupported GPU asset texture format");
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

        bool samples(const Device& device, VriFormat format)
        {
            return (device.core.GetFormatSupport(device.handle, format) & VriFormatSupport_Texture) != 0;
        }

        // UASTC goes to the first format the device samples, best preserved first: ASTC 4x4 holds UASTC
        // losslessly and BC7 nearly so, ETC2 loses more, and RGBA8 is sampled everywhere.
        VriFormat uastcTarget(const Device& device)
        {
            for (const auto format : {VriFormat_ASTC_4x4_UNORM, VriFormat_BC7_UNORM, VriFormat_ETC2_RGBA8_UNORM})
            {
                if (samples(device, format))
                {
                    return format;
                }
            }
            return VriFormat_RGBA8_UNORM;
        }

        std::expected<std::vector<std::byte>, Error> transcodeUastc(const TextureSubresourceView& resource,
                                                                    VriFormat                     target)
        {
            static std::once_flag initialized;
            std::call_once(initialized,
                           []
                           {
                               basist::basisu_transcoder_init();
                           });
            const uint32_t blocksX = (resource.size.width + 3) / 4;
            const uint32_t blocksY = (resource.size.height + 3) / 4;
            const uint64_t count   = uint64_t(blocksX) * blocksY * resource.depth;
            if (resource.bytes.size() != count * sizeof(basist::uastc_block))
            {
                return makeError("Transcode UASTC: subresource has {} bytes, expected {}",
                                 resource.bytes.size(),
                                 count * sizeof(basist::uastc_block));
            }
            const bool             unpack = target == VriFormat_RGBA8_UNORM;
            std::vector<std::byte> output(unpack ?
                                              size_t(resource.size.width) * resource.size.height * resource.depth * 4 :
                                              resource.bytes.size());
            for (uint64_t i = 0; i < count; ++i)
            {
                basist::uastc_block block;
                std::memcpy(&block, resource.bytes.data() + i * sizeof(block), sizeof(block));
                bool transcoded = false;
                switch (target)
                {
                    case VriFormat_ASTC_4x4_UNORM:
                        transcoded = basist::transcode_uastc_to_astc(block, output.data() + i * sizeof(block));
                        break;
                    case VriFormat_BC7_UNORM:
                        transcoded = basist::transcode_uastc_to_bc7(block, output.data() + i * sizeof(block));
                        break;
                    case VriFormat_ETC2_RGBA8_UNORM:
                        transcoded = basist::transcode_uastc_to_etc2_rgba(block, output.data() + i * sizeof(block));
                        break;
                    default: {
                        std::array<basist::color32, 16> texels;
                        transcoded = basist::unpack_uastc(block, texels.data(), false);
                        // Copy the part of the block inside the image; partial blocks pad the edge.
                        const uint32_t slice = uint32_t(i / (uint64_t(blocksX) * blocksY));
                        const uint32_t bx    = uint32_t(i % blocksX) * 4;
                        const uint32_t by    = uint32_t(i / blocksX % blocksY) * 4;
                        for (uint32_t y = 0; y < 4 && by + y < resource.size.height; ++y)
                        {
                            for (uint32_t x = 0; x < 4 && bx + x < resource.size.width; ++x)
                            {
                                const size_t texel =
                                    (size_t(slice) * resource.size.height + by + y) * resource.size.width + bx + x;
                                std::memcpy(output.data() + texel * 4, &texels[y * 4 + x], 4);
                            }
                        }
                        break;
                    }
                }
                if (!transcoded)
                {
                    return makeError("Transcode UASTC block {}", i);
                }
            }
            return output;
        }

        std::expected<std::unique_ptr<Texture>, Error> uploadTexture(Device&                            device,
                                                                     const TextureUploadView&           data,
                                                                     VriFormat                          format,
                                                                     const asset_detail::TextureLayout& layout)
        {
            if (data.subresources.empty() || data.layers == 0 || data.mips == 0 || data.mips > 32)
            {
                return makeError("Texture asset requires a complete subresource chain");
            }
            const auto& first = data.subresources.front();
            if (first.mip != 0 || first.layer != 0 || first.size.empty() || first.depth == 0)
            {
                return makeError("Texture asset must start with mip zero, layer zero");
            }
            auto desc = colorTexture(first.size, format);
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
                    return makeError("Invalid texture asset dimension");
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
                return makeError("Texture asset dimensions and subresources disagree");
            }
            if (data.mips > std::bit_width(std::max({first.size.width, first.size.height, first.depth})))
            {
                return makeError("Texture asset mip count exceeds its dimensions");
            }
            desc.depth    = first.depth;
            desc.layerNum = data.layers;
            desc.mipNum   = data.mips;
            desc.usage    = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc;
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
                    return makeError("Texture asset mip is outside its chain");
                }
                const Extent expected {std::max(first.size.width >> resource.mip, 1u),
                                       std::max(first.size.height >> resource.mip, 1u)};
                const auto   depth = volume ? std::max(first.depth >> resource.mip, 1u) : 1u;
                if (resource.layer >= data.layers || resource.size != expected || resource.depth != depth ||
                    !seen.emplace(resource.mip, resource.layer).second)
                {
                    return makeError("Invalid or duplicate texture asset subresource");
                }
                const uint32_t packedRow = (resource.size.width + layout.block - 1) / layout.block * layout.bytes;
                const uint32_t rows      = (resource.size.height + layout.block - 1) / layout.block;
                if (resource.bytes.size() != uint64_t(packedRow) * rows * resource.depth)
                {
                    return makeError("Texture asset subresource byte count");
                }
                const auto rowBytes = (packedRow + rowAlignment - 1) / rowAlignment * rowAlignment;
                copies.push_back({total, rowBytes, rows, packedRow});
                total = (total + uint64_t(rowBytes) * rows * resource.depth + 511) & ~uint64_t(511);
            }
            auto staging = Buffer::create(device, {total, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            if (!staging)
            {
                return std::unexpected(staging.error());
            }
            auto* mapped = static_cast<std::byte*>(device.core.MapBuffer(staging->handle, 0, total));
            if (!mapped)
            {
                return makeError("Map texture asset staging buffer");
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
            device.core.UnmapBuffer(staging->handle);
            auto created = Texture::create(device, desc);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            auto texture = std::make_unique<Texture>(std::move(*created));
            auto frame   = Frame::create(device);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            const auto commands = (*frame)->begin();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            texture->transition(*commands,
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
                device.core.CmdUploadBufferToTexture(*commands, texture->handle, staging->handle, &copy);
            }
            texture->transition(*commands,
                                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
            if (auto submitted = (*frame)->submitAndWait(); !submitted)
            {
                return std::unexpected(submitted.error());
            }
            return texture;
        }

        std::expected<std::unique_ptr<Texture>, Error> uploadTexture(Device& device, const TextureUploadView& data)
        {
            if (data.format == TextureFormat::eUastc4x4)
            {
                const VriFormat target = uastcTarget(device);
                // The transcoded bytes outlive the upload view that refers to them.
                std::vector<std::vector<std::byte>> transcoded;
                transcoded.reserve(data.subresources.size());
                TextureUploadView view = data;
                for (auto& resource : view.subresources)
                {
                    auto bytes = transcodeUastc(resource, target);
                    if (!bytes)
                    {
                        return std::unexpected(bytes.error());
                    }
                    resource.bytes = transcoded.emplace_back(std::move(*bytes));
                }
                // The transcoded blocks keep UASTC's 4x4 blocks of 16 bytes; RGBA8 has 4-byte texels.
                const asset_detail::TextureLayout layout = target == VriFormat_RGBA8_UNORM ?
                                                               asset_detail::TextureLayout {1, 4} :
                                                               asset_detail::TextureLayout {4, 16};
                return uploadTexture(device, view, target, layout);
            }
            const auto format = gpuFormat(data.format);
            if (!format)
            {
                return std::unexpected(format.error());
            }
            // Authored block-compressed textures (DDS) are uploaded as they are; not every device samples them.
            if (!samples(device, *format))
            {
                return makeError("This device does not sample texture asset format {}", uint32_t(data.format));
            }
            const auto layout = asset_detail::textureLayout(data.format);
            if (!layout)
            {
                return std::unexpected(layout.error());
            }
            return uploadTexture(device, data, *format, *layout);
        }
    } // namespace

    std::expected<std::unique_ptr<Texture>, Error> uploadTextureAsset(Device& device, const TextureAssetData& data)
    {
        TextureUploadView view {data.dimension, data.format, data.layers, data.mips, {}};
        view.subresources.reserve(data.subresources.size());
        for (const auto& resource : data.subresources)
        {
            view.subresources.push_back({resource.size, resource.depth, resource.mip, resource.layer, resource.bytes});
        }
        return uploadTexture(device, view);
    }

    std::expected<std::unique_ptr<Texture>, Error> uploadTextureAsset(Device& device, const TextureData& data)
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
