#include "vultra/function/resource/raw_resource_loader.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/util.hpp"

#include <vasset/vasset.hpp>

// #define STB_IMAGE_IMPLEMENTATION
// Already defined in vasset_importers.cpp
#include <stb_image.h>

#define DDSKTX_IMPLEMENT
#include <dds-ktx.h>

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <ktx.h>
#include <ktxvulkan.h>

#include <magic_enum/magic_enum.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

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
    }

    std::expected<std::vector<uint8_t>, std::string> readAll(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f)
            return std::unexpected {"Could not open the file."};
        const size_t         sz = static_cast<size_t>(f.tellg());
        std::vector<uint8_t> data(sz);
        f.seekg(0);
        if (!f.read(reinterpret_cast<char*>(data.data()), sz))
            return std::unexpected {"Failed to read the file."};
        return data;
    }
} // namespace

namespace vultra
{
    namespace resource
    {
        std::expected<rhi::Texture, std::string> loadTextureVTexture(const std::filesystem::path& p,
                                                                     rhi::RenderDevice&           rd)
        {
            vasset::VTexture vtexture {};
            if (!vasset::loadTexture(p.string(), vtexture))
            {
                return std::unexpected {"Failed to load VTexture."};
            }

            auto fileFormat = vtexture.fileFormat;

            switch (fileFormat)
            {
                case vasset::VTextureFileFormat::eKTX2:
                    return loadTextureKTX2_Raw(vtexture.data, rd);
                case vasset::VTextureFileFormat::ePNG:
                case vasset::VTextureFileFormat::eJPG:
                case vasset::VTextureFileFormat::eHDR:
                    return loadTextureSTB_Raw(vtexture.data, rd);
                default:
                    return std::unexpected {"Unsupported VTexture file format."};
            }

            return std::unexpected {"Unreachable code reached."};
        }

        std::expected<rhi::Texture, std::string> loadTextureSTB(const std::filesystem::path& p, rhi::RenderDevice& rd)
        {
            stbi_set_flip_vertically_on_load(false);

            auto* file = fopen(p.string().c_str(), "rb");
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
                void* ptr =
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

        std::expected<rhi::Texture, std::string> loadTextureSTB_Raw(const std::vector<uint8_t>& bintex,
                                                                    rhi::RenderDevice&          rd)
        {
            stbi_set_flip_vertically_on_load(false);

            const auto hdr = stbi_is_hdr_from_memory(bintex.data(), static_cast<int>(bintex.size()));

            int32_t width;
            int32_t height;

            struct Deleter
            {
                void operator()(void* pixels) const { stbi_image_free(pixels); }
            };
            ScopeWithDeleter<void, Deleter> pixels;
            {
                void* ptr =
                    hdr ?
                        static_cast<void*>(stbi_loadf_from_memory(
                            bintex.data(), static_cast<int>(bintex.size()), &width, &height, nullptr, STBI_rgb_alpha)) :
                        static_cast<void*>(stbi_load_from_memory(
                            bintex.data(), static_cast<int>(bintex.size()), &width, &height, nullptr, STBI_rgb_alpha));
                pixels.reset(ptr);
            }

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
            const char* err   = nullptr;
            int         width = 0, height = 0;
            float*      data = nullptr;

            if (LoadEXR(&data, &width, &height, p.string().c_str(), &err) != TINYEXR_SUCCESS)
            {
                return std::unexpected {std::string(err ? err : "Failed to load EXR.")};
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

        std::expected<rhi::Texture, std::string> loadTextureEXR_Raw(const std::vector<uint8_t>& bintex,
                                                                    rhi::RenderDevice&          rd)
        {
            const char* err   = nullptr;
            int         width = 0, height = 0;
            float*      data = nullptr;

            if (LoadEXRFromMemory(&data, &width, &height, bintex.data(), static_cast<int>(bintex.size()), &err) !=
                TINYEXR_SUCCESS)
            {
                return std::unexpected {std::string(err ? err : "Failed to load EXR.")};
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

            // C++ file I/O
            auto fileBytes = readAll(path);
            if (!fileBytes)
                return std::unexpected {fileBytes.error()};

            ddsktx_texture_info tc {0};

            if (!ddsktx_parse(&tc, fileBytes->data(), static_cast<int>(fileBytes->size()), nullptr))
            {
                return std::unexpected {"Failed to parse texture file."};
            }

            auto extent = rhi::Extent2D {static_cast<uint32_t>(tc.width), static_cast<uint32_t>(tc.height)};

            if (tc.num_layers > 1)
            {
                VULTRA_CORE_WARN("[TextureLoader] KTX/DDS texture with multiple layers is not supported yet. Fallback "
                                 "to single layer.");
            }

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

            for (int mip = 0; mip < tc.num_mips; ++mip)
            {
                for (int layer = 0; layer < tc.num_layers; ++layer)
                {
                    for (int face = 0; face < (tc.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP ? DDSKTX_CUBE_FACE_COUNT : 1);
                         ++face)
                    {
                        ddsktx_sub_data subData;
                        ddsktx_get_sub(
                            &tc, &subData, fileBytes->data(), static_cast<int>(fileBytes->size()), layer, face, mip);
                        if (!subData.buff)
                        {
                            return std::unexpected {"Failed to get texture sub-data."};
                        }

                        const auto uploadSize       = subData.size_bytes;
                        const auto srcStagingBuffer = rd.createStagingBuffer(uploadSize, subData.buff);
                        if (!srcStagingBuffer)
                        {
                            return std::unexpected {"Failed to create staging buffer."};
                        }

                        std::array<vk::BufferImageCopy, 1> copyRegions {};
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

        std::expected<rhi::Texture, std::string> loadTextureKTX_DDS_Raw(const std::vector<uint8_t>& bintex,
                                                                        rhi::RenderDevice&          rd)
        {
            // C++ file I/O
            auto fileBytes = std::expected<std::vector<uint8_t>, std::string> {bintex};

            if (!fileBytes)
                return std::unexpected {fileBytes.error()};

            ddsktx_texture_info tc {0};

            if (!ddsktx_parse(&tc, fileBytes->data(), static_cast<int>(fileBytes->size()), nullptr))
            {
                return std::unexpected {"Failed to parse texture file."};
            }

            auto extent = rhi::Extent2D {static_cast<uint32_t>(tc.width), static_cast<uint32_t>(tc.height)};

            if (tc.num_layers > 1)
            {
                VULTRA_CORE_WARN("[TextureLoader] KTX/DDS texture with multiple layers is not supported yet. Fallback "
                                 "to single layer.");
            }

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

            for (int mip = 0; mip < tc.num_mips; ++mip)
            {
                for (int layer = 0; layer < tc.num_layers; ++layer)
                {
                    for (int face = 0; face < (tc.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP ? DDSKTX_CUBE_FACE_COUNT : 1);
                         ++face)
                    {
                        ddsktx_sub_data subData;
                        ddsktx_get_sub(
                            &tc, &subData, fileBytes->data(), static_cast<int>(fileBytes->size()), layer, face, mip);
                        if (!subData.buff)
                        {
                            return std::unexpected {"Failed to get texture sub-data."};
                        }

                        const auto uploadSize       = subData.size_bytes;
                        const auto srcStagingBuffer = rd.createStagingBuffer(uploadSize, subData.buff);
                        if (!srcStagingBuffer)
                        {
                            return std::unexpected {"Failed to create staging buffer."};
                        }
                        std::array<vk::BufferImageCopy, 1> copyRegions {};
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

        // https://docs.vulkan.org/samples/latest/samples/performance/texture_compression_basisu/README.html
        std::expected<rhi::Texture, std::string> loadTextureKTX2(const std::filesystem::path& path,
                                                                 rhi::RenderDevice&           rd)
        {
            // Load from file using libktx
            ktxTexture2*   kTexture  = nullptr;
            KTX_error_code ktxResult = ktxTexture2_CreateFromNamedFile(
                path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);

            if (ktxResult != KTX_SUCCESS || !kTexture)
            {
                return std::unexpected {"ktxTexture2_CreateFromNamedFile failed: " +
                                        std::string(ktxErrorString(ktxResult))};
            }

            if (ktxTexture2_NeedsTranscoding(kTexture))
            {
                ktxResult = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
                if (ktxResult != KTX_SUCCESS)
                {
                    return std::unexpected {"Could not transcode the input texture to the selected target format: " +
                                            std::string(ktxErrorString(ktxResult))};
                }
            }

            // Get texture info
            uint32_t width     = kTexture->baseWidth;
            uint32_t height    = kTexture->baseHeight;
            uint32_t levels    = kTexture->numLevels > 0 ? kTexture->numLevels : 1;
            uint32_t layers    = kTexture->numLayers > 0 ? kTexture->numLayers : 1;
            bool     isCubemap = (kTexture->numFaces == 6);

            // Translate KTX format to RHI format
            VkFormat         vkFormat    = ktxTexture_GetVkFormat(reinterpret_cast<ktxTexture*>(kTexture));
            rhi::PixelFormat pixelFormat = static_cast<rhi::PixelFormat>(vkFormat);
            if (pixelFormat == rhi::PixelFormat::eUndefined)
            {
                ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
                return std::unexpected {"Unsupported pixel format: " +
                                        std::string(magic_enum::enum_name(pixelFormat).data())};
            }

            // Build RHI texture
            std::optional<uint32_t> numLayers = std::nullopt;
            if (isCubemap)
            {
                numLayers = layers * 6;
            }
            else if (layers > 1)
            {
                numLayers = layers;
            }
            auto tex = rhi::Texture::Builder {}
                           .setExtent({width, height})
                           .setPixelFormat(pixelFormat)
                           .setNumMipLevels(levels)
                           .setNumLayers(numLayers)
                           .setUsageFlags(rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst)
                           .setupOptimalSampler(true)
                           .build(rd);

            if (!tex)
            {
                ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
                return std::unexpected {"Failed to create RHI texture."};
            }

            // Upload each subresource
            for (uint32_t level = 0; level < levels; ++level)
            {
                for (uint32_t layer = 0; layer < layers; ++layer)
                {
                    for (uint32_t face = 0; face < (isCubemap ? 6u : 1u); ++face)
                    {
                        ktx_size_t offset;
                        ktxResult = ktxTexture_GetImageOffset(
                            reinterpret_cast<ktxTexture*>(kTexture), level, layer, face, &offset);
                        if (ktxResult != KTX_SUCCESS)
                        {
                            ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
                            return std::unexpected {"ktxTexture_GetImageOffset failed."};
                        }

                        const uint8_t* src  = kTexture->pData + offset;
                        ktx_size_t     size = ktxTexture_GetImageSize(reinterpret_cast<ktxTexture*>(kTexture), level);

                        auto staging = rd.createStagingBuffer(size, src);
                        if (!staging)
                        {
                            ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
                            return std::unexpected {"Failed to create staging buffer."};
                        }

                        vk::BufferImageCopy copy {};
                        copy.bufferOffset                    = 0;
                        copy.bufferRowLength                 = 0;
                        copy.bufferImageHeight               = 0;
                        copy.imageSubresource.aspectMask     = rhi::getAspectMask(pixelFormat);
                        copy.imageSubresource.mipLevel       = level;
                        copy.imageSubresource.baseArrayLayer = layer * (isCubemap ? 6 : 1) + face;
                        copy.imageSubresource.layerCount     = 1;
                        copy.imageOffset                     = vk::Offset3D {0, 0, 0};
                        copy.imageExtent                     = vk::Extent3D {std::max(1u, kTexture->baseWidth >> level),
                                                         std::max(1u, kTexture->baseHeight >> level),
                                                         1u};

                        std::array<vk::BufferImageCopy, 1> regions {copy};
                        rhi::upload(rd, staging, regions, tex, false);
                    }
                }
            }

            ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
            return tex;
        }

        std::expected<rhi::Texture, std::string> loadTextureKTX2_Raw(const std::vector<uint8_t>& bintex,
                                                                     rhi::RenderDevice&          rd)
        {
            // Load from memory using libktx
            ktxTexture2*   kTexture  = nullptr;
            KTX_error_code ktxResult = ktxTexture2_CreateFromMemory(bintex.data(),
                                                                    static_cast<ktx_size_t>(bintex.size()),
                                                                    KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                                                    &kTexture);

            if (ktxResult != KTX_SUCCESS || !kTexture)
            {
                return std::unexpected {"ktxTexture2_CreateFromMemory failed: " +
                                        std::string(ktxErrorString(ktxResult))};
            }

            if (ktxTexture2_NeedsTranscoding(kTexture))
            {
                ktxResult = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
                if (ktxResult != KTX_SUCCESS)
                {
                    return std::unexpected {"Could not transcode the input texture to the selected target format: " +
                                            std::string(ktxErrorString(ktxResult))};
                }
            }

            // Get texture info
            uint32_t width     = kTexture->baseWidth;
            uint32_t height    = kTexture->baseHeight;
            uint32_t levels    = kTexture->numLevels > 0 ? kTexture->numLevels : 1;
            uint32_t layers    = kTexture->numLayers > 0 ? kTexture->numLayers : 1;
            bool     isCubemap = (kTexture->numFaces == 6);

            // Translate KTX format to RHI format
            VkFormat         vkFormat    = ktxTexture_GetVkFormat(reinterpret_cast<ktxTexture*>(kTexture));
            rhi::PixelFormat pixelFormat = static_cast<rhi::PixelFormat>(vkFormat);
            if (pixelFormat == rhi::PixelFormat::eUndefined)
            {
                ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
                return std::unexpected {"Unsupported pixel format: " +
                                        std::string(magic_enum::enum_name(pixelFormat).data())};
            }

            // Build RHI texture
            std::optional<uint32_t> numLayers = std::nullopt;
            if (isCubemap)
            {
                numLayers = layers * 6;
            }
            else if (layers > 1)
            {
                numLayers = layers;
            }

            auto tex = rhi::Texture::Builder {}
                           .setExtent({width, height})
                           .setPixelFormat(pixelFormat)
                           .setNumMipLevels(levels)
                           .setNumLayers(numLayers)
                           .setUsageFlags(rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst)
                           .setupOptimalSampler(true)
                           .build(rd);

            if (!tex)
            {
                ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
                return std::unexpected {"Failed to create RHI texture."};
            }

            // Upload each subresource
            for (uint32_t level = 0; level < levels; ++level)
            {
                for (uint32_t layer = 0; layer < layers; ++layer)
                {
                    for (uint32_t face = 0; face < (isCubemap ? 6u : 1u); ++face)
                    {
                        ktx_size_t offset;
                        ktxResult = ktxTexture_GetImageOffset(
                            reinterpret_cast<ktxTexture*>(kTexture), level, layer, face, &offset);
                        if (ktxResult != KTX_SUCCESS)
                        {
                            ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
                            return std::unexpected {"ktxTexture_GetImageOffset failed."};
                        }

                        const uint8_t* src  = kTexture->pData + offset;
                        ktx_size_t     size = ktxTexture_GetImageSize(reinterpret_cast<ktxTexture*>(kTexture), level);

                        auto staging = rd.createStagingBuffer(size, src);
                        if (!staging)
                        {
                            ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
                            return std::unexpected {"Failed to create staging buffer."};
                        }

                        vk::BufferImageCopy copy {};
                        copy.bufferOffset                    = 0;
                        copy.bufferRowLength                 = 0;
                        copy.bufferImageHeight               = 0;
                        copy.imageSubresource.aspectMask     = rhi::getAspectMask(pixelFormat);
                        copy.imageSubresource.mipLevel       = level;
                        copy.imageSubresource.baseArrayLayer = layer * (isCubemap ? 6 : 1) + face;
                        copy.imageSubresource.layerCount     = 1;
                        copy.imageOffset                     = vk::Offset3D {0, 0, 0};
                        copy.imageExtent                     = vk::Extent3D {std::max(1u, kTexture->baseWidth >> level),
                                                         std::max(1u, kTexture->baseHeight >> level),
                                                         1u};

                        std::array<vk::BufferImageCopy, 1> regions {copy};
                        rhi::upload(rd, staging, regions, tex, false);
                    }
                }
            }

            ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(kTexture));
            return tex;
        }

        std::expected<rhi::Texture, std::string>
        loadTextureRaw(const std::string& ext, const std::vector<uint8_t>& bintex, rhi::RenderDevice& rd)
        {
            if (ext == ".ktx2")
            {
                return loadTextureKTX2_Raw(bintex, rd);
            }
            else if (ext == ".ktx" || ext == ".dds")
            {
                return loadTextureKTX_DDS_Raw(bintex, rd);
            }
            else if (ext == ".exr")
            {
                return loadTextureEXR_Raw(bintex, rd);
            }
            else if (ext == ".hdr" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
                     ext == ".tga" || ext == ".gif" || ext == ".psd" || ext == ".pic")
            {
                return loadTextureSTB_Raw(bintex, rd);
            }
            else
            {
                return std::unexpected {"Unsupported texture format: " + ext};
            }
        }
    } // namespace resource
} // namespace vultra
