#include "vultra/function/resource/raw_resource_loader.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/util.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define DDSKTX_IMPLEMENT
#include <dds-ktx.h>

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <basisu/transcoder/basisu_transcoder.h>

#include <magic_enum/magic_enum.hpp>

namespace
{
    using namespace vultra;

    rhi::PixelFormat toRHI(ddsktx_format format)
    {
        switch (format)
        {
            case DDSKTX_FORMAT_BC1:
                return rhi::PixelFormat::eBC1_UNorm;
            case DDSKTX_FORMAT_BC2:
                return rhi::PixelFormat::eBC2_UNorm;
            case DDSKTX_FORMAT_BC3:
                return rhi::PixelFormat::eBC3_UNorm;
            case DDSKTX_FORMAT_BC4:
                return rhi::PixelFormat::eBC4_UNorm;
            case DDSKTX_FORMAT_BC5:
                return rhi::PixelFormat::eBC5_UNorm;
            case DDSKTX_FORMAT_BC6H:
                return rhi::PixelFormat::eBC6H_RGB16F;
            case DDSKTX_FORMAT_BC7:
                return rhi::PixelFormat::eBC7_RGBA8_UNorm;
            case DDSKTX_FORMAT_ETC2:
                return rhi::PixelFormat::eETC2_RGB8_UNorm;
            case DDSKTX_FORMAT_ETC2A:
                return rhi::PixelFormat::eETC2_RGBA8_UNorm;
            case DDSKTX_FORMAT_ETC2A1:
                return rhi::PixelFormat::eETC2_RGB8A1_UNorm;
            case DDSKTX_FORMAT_PTC12A:
                return rhi::PixelFormat::ePTC12A_RGBA8_UNorm;
            case DDSKTX_FORMAT_PTC14A:
                return rhi::PixelFormat::ePTC14A_RGBA8_SRGB;
            case DDSKTX_FORMAT_PTC12:
                return rhi::PixelFormat::ePTC12_RGBA8_UNorm;
            case DDSKTX_FORMAT_PTC14:
                return rhi::PixelFormat::ePTC14_RGBA8_UNorm;
            case DDSKTX_FORMAT_PTC22:
                return rhi::PixelFormat::ePTC22_RGBA8_UNorm;
            case DDSKTX_FORMAT_PTC24:
                return rhi::PixelFormat::ePTC24_RGBA8_UNorm;
            case DDSKTX_FORMAT_ASTC4x4:
                return rhi::PixelFormat::eASTC_4x4_UNorm;
            case DDSKTX_FORMAT_ASTC5x5:
                return rhi::PixelFormat::eASTC_5x5_UNorm;
            case DDSKTX_FORMAT_ASTC6x6:
                return rhi::PixelFormat::eASTC_6x6_UNorm;
            case DDSKTX_FORMAT_ASTC8x5:
                return rhi::PixelFormat::eASTC_8x5_UNorm;
            case DDSKTX_FORMAT_ASTC8x6:
                return rhi::PixelFormat::eASTC_8x6_UNorm;
            case DDSKTX_FORMAT_ASTC10x5:
                return rhi::PixelFormat::eASTC_10x5_UNorm;
            case DDSKTX_FORMAT_A8:
                return rhi::PixelFormat::eA8_UNorm;
            case DDSKTX_FORMAT_R8:
                return rhi::PixelFormat::eR8_UNorm;
            case DDSKTX_FORMAT_RGBA8:
                return rhi::PixelFormat::eRGBA8_UNorm;
            case DDSKTX_FORMAT_RGBA8S:
                return rhi::PixelFormat::eRGBA8_SNorm;
            case DDSKTX_FORMAT_RG16:
                return rhi::PixelFormat::eRG16_UNorm;
            case DDSKTX_FORMAT_RGB8:
                return rhi::PixelFormat::eRGB8_UNorm;
            case DDSKTX_FORMAT_R16:
                return rhi::PixelFormat::eR16_UNorm;
            case DDSKTX_FORMAT_R32F:
                return rhi::PixelFormat::eR32F;
            case DDSKTX_FORMAT_R16F:
                return rhi::PixelFormat::eR16F;
            case DDSKTX_FORMAT_RG16F:
                return rhi::PixelFormat::eRG16F;
            case DDSKTX_FORMAT_RG16S:
                return rhi::PixelFormat::eRG16I;
            case DDSKTX_FORMAT_RGBA16F:
                return rhi::PixelFormat::eRGBA16F;
            case DDSKTX_FORMAT_RGBA16:
                return rhi::PixelFormat::eRGBA16_UNorm;
            case DDSKTX_FORMAT_BGRA8:
                return rhi::PixelFormat::eBGRA8_UNorm;
            case DDSKTX_FORMAT_RGB10A2:
                return rhi::PixelFormat::eA2RGB10_UNorm;
            case DDSKTX_FORMAT_RG11B10F:
                return rhi::PixelFormat::eB10RG11_UNorm;
            case DDSKTX_FORMAT_RG8:
                return rhi::PixelFormat::eRG8_UNorm;
            case DDSKTX_FORMAT_RG8S:
                return rhi::PixelFormat::eRG8_SNorm;
            case _DDSKTX_FORMAT_COUNT:
                break;

            case _DDSKTX_FORMAT_COMPRESSED:
                throw std::runtime_error {"Undefined compressed format."};

            case DDSKTX_FORMAT_ETC1:
                throw std::runtime_error {"ETC1 format is not supported."};

            case DDSKTX_FORMAT_ATC:
            case DDSKTX_FORMAT_ATCE:
            case DDSKTX_FORMAT_ATCI:
                throw std::runtime_error {"ATC formats are not supported."};

            default:
                return rhi::PixelFormat::eUndefined;
        }

        return rhi::PixelFormat::eUndefined;
    }
} // namespace

namespace vultra
{
    namespace resource
    {
        std::expected<rhi::Texture, std::string> loadTextureSTB(const std::filesystem::path& p, rhi::RenderDevice& rd)
        {
            stbi_set_flip_vertically_on_load(false);

            auto* file = stbi__fopen(p.string().c_str(), "rb");
            if (!file)
            {
                return std::unexpected {"Could not open the file."};
            }

            const auto hdr = stbi_is_hdr_from_file(file);

            int32_t width;
            int32_t height;

            struct Deleter
            {
                void operator()(void* pixels) const { stbi_image_free(pixels); }
            };
            ScopeWithDeleter<void, Deleter> pixels;
            {
                auto* ptr =
                    hdr ? static_cast<void*>(stbi_loadf_from_file(file, &width, &height, nullptr, STBI_rgb_alpha)) :
                          static_cast<void*>(stbi_load_from_file(file, &width, &height, nullptr, STBI_rgb_alpha));
                pixels.reset(ptr);
            }
            fclose(file);

            if (!pixels)
            {
                return std::unexpected {stbi_failure_reason()};
            }

            const auto extent       = rhi::Extent2D {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            const auto numMipLevels = rhi::calcMipLevels(extent);

            const auto generateMipmaps = numMipLevels > 1;

            auto usageFlags = rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled;
            if (generateMipmaps)
                usageFlags |= rhi::ImageUsage::eTransferSrc;

            const auto pixelFormat = hdr ? rhi::PixelFormat::eRGBA32F : rhi::PixelFormat::eRGBA8_UNorm;
            auto       texture     = rhi::Texture::Builder {}
                               .setExtent(extent)
                               .setPixelFormat(pixelFormat)
                               .setNumMipLevels(numMipLevels)
                               .setNumLayers(std::nullopt)
                               .setUsageFlags(usageFlags)
                               .setupOptimalSampler(true)
                               .build(rd);
            if (!texture)
            {
                return std::unexpected {
                    std::format("Unsupported pixel format: {}.", magic_enum::enum_name(pixelFormat))};
            }

            const auto pixelSize        = static_cast<int32_t>(hdr ? sizeof(float) : sizeof(uint8_t));
            const auto uploadSize       = width * height * STBI_rgb_alpha * pixelSize;
            const auto srcStagingBuffer = rd.createStagingBuffer(uploadSize, pixels.get());
            rhi::upload(rd, srcStagingBuffer, {}, texture, generateMipmaps);

            return texture;
        }

        std::expected<rhi::Texture, std::string> loadTextureEXR(const std::filesystem::path& p, rhi::RenderDevice& rd)
        {
            const char* err = nullptr;
            int         width, height;
            float*      data = nullptr;

            if (LoadEXR(&data, &width, &height, p.string().c_str(), &err) != TINYEXR_SUCCESS)
            {
                return std::unexpected {std::string(err)};
            }

            const auto extent       = rhi::Extent2D {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            const auto numMipLevels = rhi::calcMipLevels(extent);

            auto usageFlags = rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled;
            if (numMipLevels > 1)
                usageFlags |= rhi::ImageUsage::eTransferSrc;

            auto texture = rhi::Texture::Builder {}
                               .setExtent(extent)
                               .setPixelFormat(rhi::PixelFormat::eRGBA32F)
                               .setNumMipLevels(numMipLevels)
                               .setNumLayers(std::nullopt)
                               .setUsageFlags(usageFlags)
                               .setupOptimalSampler(true)
                               .build(rd);
            if (!texture)
            {
                free(data);
                return std::unexpected {"Failed to create texture."};
            }

            const auto uploadSize       = width * height * 4 * sizeof(float);
            const auto srcStagingBuffer = rd.createStagingBuffer(uploadSize, data);
            free(data);

            rhi::upload(rd, srcStagingBuffer, {}, texture, numMipLevels > 1);

            return texture;
        }

        std::expected<rhi::Texture, std::string> loadTextureKTX_DDS(const std::filesystem::path& p,
                                                                    rhi::RenderDevice&           rd)
        {
            auto pathStr = p.string();

#ifndef _WIN32
            // Ensure the path is in the correct format for KTX/DDS parsing
            // \\ -> /
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
#endif

            auto path = std::filesystem::path {pathStr};

            auto* file = fopen(path.string().c_str(), "rb");
            if (!file)
            {
                return std::unexpected {"Could not open the file."};
            }

            const auto           size = static_cast<int>(std::filesystem::file_size(path));
            std::vector<uint8_t> fileData(size);
            fread(fileData.data(), 1, size, file);
            fclose(file);

            ddsktx_texture_info tc {0};

            if (!ddsktx_parse(&tc, fileData.data(), size, nullptr))
            {
                return std::unexpected {"Failed to parse texture file."};
            }

            auto extent = rhi::Extent2D {static_cast<uint32_t>(tc.width), static_cast<uint32_t>(tc.height)};

            if (tc.num_layers > 1)
            {
                VULTRA_CORE_WARN("[TextureLoader] KTX/DDS texture with multiple layers is not supported yet. Fallback "
                                 "to single layer.");
            }

            // Create the texture from the parsed information
            rhi::Texture texture = rhi::Texture::Builder {}
                                       .setExtent(extent)
                                       .setPixelFormat(toRHI(tc.format))
                                       .setNumMipLevels(tc.num_mips)
                                       .setNumLayers(std::nullopt)
                                       .setUsageFlags(rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst)
                                       .setupOptimalSampler(true)
                                       .build(rd);
            if (!texture)
            {
                return std::unexpected {"Failed to create texture."};
            }

            // For each mip level, array layer, and cube face (if applicable)
            for (int mip = 0; mip < tc.num_mips; ++mip)
            {
                for (int layer = 0; layer < tc.num_layers; ++layer)
                {
                    for (int face = 0; face < (tc.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP ? DDSKTX_CUBE_FACE_COUNT : 1);
                         ++face)
                    {
                        // Read the texture data
                        ddsktx_sub_data subData;
                        ddsktx_get_sub(&tc, &subData, fileData.data(), size, layer, face, mip);
                        if (!subData.buff)
                        {
                            return std::unexpected {"Failed to get texture sub-data."};
                        }

                        // Allocate staging buffer for the texture data
                        const auto uploadSize       = subData.size_bytes;
                        const auto srcStagingBuffer = rd.createStagingBuffer(uploadSize, subData.buff);
                        if (!srcStagingBuffer)
                        {
                            return std::unexpected {"Failed to create staging buffer."};
                        }

                        // Upload the texture data to the GPU
                        std::array<vk::BufferImageCopy, 1> copyRegions;
                        copyRegions[0].bufferOffset                    = 0;
                        copyRegions[0].bufferRowLength                 = 0;
                        copyRegions[0].bufferImageHeight               = 0;
                        copyRegions[0].imageSubresource.aspectMask     = rhi::getAspectMask(texture.getPixelFormat());
                        copyRegions[0].imageSubresource.mipLevel       = static_cast<uint32_t>(mip);
                        copyRegions[0].imageSubresource.baseArrayLayer = static_cast<uint32_t>(layer);
                        copyRegions[0].imageSubresource.layerCount     = tc.num_layers;
                        copyRegions[0].imageOffset                     = vk::Offset3D {0, 0, 0};
                        copyRegions[0].imageExtent                     = vk::Extent3D {
                            static_cast<uint32_t>(subData.width),
                            static_cast<uint32_t>(subData.height),
                            static_cast<uint32_t>(tc.depth),
                        };
                        rhi::upload(rd, srcStagingBuffer, copyRegions, texture, false);
                    }
                }
            }

            return texture;
        }

        using namespace basist;

        std::expected<rhi::Texture, std::string> loadTextureBasisU(const std::filesystem::path& path,
                                                                   rhi::RenderDevice&           rd)
        {
            // TODO: Auto select targetFmt, implement texture loading
            return {};
        }
    } // namespace resource
} // namespace vultra