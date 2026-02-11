#include "vultra/function/resource/raw_resource_loader.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/function/renderer/mesh_manager.hpp"
#include "vultra/function/renderer/mesh_utils.hpp"
#include "vultra/function/renderer/texture_manager.hpp"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <magic_enum.hpp>

// #define STB_IMAGE_IMPLEMENTATION
// Already defined in vasset_importers.cpp
#include <stb_image.h>

// #define DDSKTX_IMPLEMENT
// Already defined in vasset_importers.cpp
#include <dds-ktx.h>

// #define TINYEXR_IMPLEMENTATION
// Already defined in vasset_importers.cpp
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

    const rhi::BlendState kNoBlend = {
        .enabled = false,
    };

    const rhi::BlendState kAlphaBlend = {
        .enabled  = true,
        .srcColor = rhi::BlendFactor::eSrcAlpha,
        .dstColor = rhi::BlendFactor::eOneMinusSrcAlpha,
        .colorOp  = rhi::BlendOp::eAdd,
        .srcAlpha = rhi::BlendFactor::eOne,
        .dstAlpha = rhi::BlendFactor::eZero,
        .alphaOp  = rhi::BlendOp::eAdd,
    };

    const rhi::BlendState kAdditiveBlend = {
        .enabled  = true,
        .srcColor = rhi::BlendFactor::eOne,
        .dstColor = rhi::BlendFactor::eOne,
        .colorOp  = rhi::BlendOp::eAdd,
        .srcAlpha = rhi::BlendFactor::eOne,
        .dstAlpha = rhi::BlendFactor::eZero,
        .alphaOp  = rhi::BlendOp::eAdd,
    };

    rhi::BlendState toBlendState(aiBlendMode mode)
    {
        switch (mode)
        {
            case aiBlendMode_Default:
                return kAlphaBlend;
            case aiBlendMode_Additive:
                return kAdditiveBlend;
            default:
                VULTRA_CORE_WARN("[MeshLoader] Unknown aiBlendMode {}, using default blend state",
                                 magic_enum::enum_name(mode).data());
                return toBlendState(aiBlendMode::aiBlendMode_Default);
        }
    }

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
    namespace gfx
    {
        struct MatPropValue
        {
            std::variant<aiString, int, float, double, bool, aiColor3D, aiColor4D, std::vector<uint8_t>, aiBlendMode>
                 value;
            bool parsed = false;
        };

        struct MatPropKey
        {
            std::string key;
            unsigned    semantic = 0;
            unsigned    index    = 0;
            bool        operator==(const MatPropKey& other) const noexcept
            {
                return key == other.key && semantic == other.semantic && index == other.index;
            }
        };
        struct MatPropKeyHash
        {
            size_t operator()(const MatPropKey& k) const noexcept
            {
                size_t h1 = std::hash<std::string> {}(k.key);
                size_t h2 = std::hash<unsigned> {}(k.semantic);
                size_t h3 = std::hash<unsigned> {}(k.index);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };
        using MatPropMap = std::unordered_map<MatPropKey, MatPropValue, MatPropKeyHash>;

        MatPropMap parseMaterialProperties(const aiMaterial* material)
        {
            MatPropMap result;
            if (!material)
                return result;

            for (unsigned i = 0; i < material->mNumProperties; ++i)
            {
                aiMaterialProperty* prop = material->mProperties[i];
                MatPropKey          key {prop->mKey.C_Str(), prop->mSemantic, prop->mIndex};
                MatPropValue        entry {};

                switch (prop->mType)
                {
                    case aiPTI_String: {
                        aiString value;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                            entry.value = std::move(value);
                        break;
                    }
                    case aiPTI_Float: {
                        if (prop->mDataLength / sizeof(float) == 3)
                        {
                            aiColor3D value;
                            if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) ==
                                aiReturn_SUCCESS)
                                entry.value = std::move(value);
                        }
                        else if (prop->mDataLength / sizeof(float) == 4)
                        {
                            aiColor4D value;
                            if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) ==
                                aiReturn_SUCCESS)
                                entry.value = std::move(value);
                        }
                        else
                        {
                            float value;
                            if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) ==
                                aiReturn_SUCCESS)
                                entry.value = std::move(value);
                        }
                        break;
                    }
                    case aiPTI_Double: {
                        double value;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                            entry.value = std::move(value);
                        break;
                    }
                    case aiPTI_Integer: {
                        int value;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                            entry.value = std::move(value);
                        bool bvalue;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, bvalue) ==
                            aiReturn_SUCCESS)
                            entry.value = std::move(bvalue);
                        aiBlendMode bmvalue;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, bmvalue) ==
                            aiReturn_SUCCESS)
                            entry.value = std::move(bmvalue);
                        break;
                    }
                    case aiPTI_Buffer: {
                        std::vector<uint8_t> buf(prop->mDataLength);
                        std::memcpy(buf.data(), prop->mData, prop->mDataLength);
                        entry.value = std::move(buf);
                        break;
                    }
                    default: {
                        VULTRA_CORE_WARN("[MeshLoader] Unknown material property type {} for key {}",
                                         static_cast<int>(prop->mType),
                                         prop->mKey.C_Str());
                        break;
                    }
                }
                result.emplace(std::move(key), std::move(entry));
            }
            return result;
        }

        template<typename T>
        bool tryGet(MatPropMap& props, const char* key, unsigned semantic, unsigned index, T& out)
        {
            MatPropKey mk {key, semantic, index};
            auto       it = props.find(mk);
            if (it != props.end())
            {
                it->second.parsed = true;
                if (auto p = std::get_if<T>(&it->second.value))
                {
                    out = *p;
                    return true;
                }
            }
            return false;
        }
    } // namespace gfx
} // namespace vultra

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
                case vasset::VTextureFileFormat::eKTX:
                case vasset::VTextureFileFormat::eDDS:
                    return loadTextureKTX_DDS_Raw(vtexture.data, rd);
                case vasset::VTextureFileFormat::eKTX2:
                    return loadTextureKTX2_Raw(vtexture.data, rd);
                case vasset::VTextureFileFormat::eEXR:
                    return loadTextureEXR_Raw(vtexture.data, rd);
                case vasset::VTextureFileFormat::ePNG:
                case vasset::VTextureFileFormat::eJPG:
                case vasset::VTextureFileFormat::eJPEG:
                case vasset::VTextureFileFormat::eHDR:
                case vasset::VTextureFileFormat::eBMP:
                case vasset::VTextureFileFormat::eGIF:
                case vasset::VTextureFileFormat::ePIC:
                case vasset::VTextureFileFormat::ePSD:
                case vasset::VTextureFileFormat::eTGA:
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

        std::expected<vasset::VMaterial, std::string> loadMaterial_VMaterial(const std::filesystem::path& p)
        {
            vasset::VMaterial mat {};

            if (!vasset::loadMaterial(p.generic_string(), mat))
            {
                return std::unexpected {"Failed to load VMaterial: " + p.generic_string()};
            }

            return mat;
        }

        std::expected<gfx::DefaultMesh, std::string> loadMesh_VMesh(const std::filesystem::path& p,
                                                                    rhi::RenderDevice&           rd)
        {
            vasset::VMesh mesh {};

            if (!vasset::loadMesh(p.generic_string(), mesh))
            {
                return std::unexpected {"Failed to load VMesh: " + p.generic_string()};
            }

            // TODO: Convert VMesh to DefaultMesh
            gfx::DefaultMesh defaultMesh {};
            defaultMesh.vertexFormat = gfx::SimpleVertex::getVertexFormat();

            return defaultMesh;
        }

        std::expected<gfx::DefaultMesh, std::string> loadMesh_Raw(const std::filesystem::path& p, rhi::RenderDevice& rd)
        {
            Assimp::Importer importer;
            const aiScene*   scene =
                importer.ReadFile(p.generic_string(),
                                  aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_PreTransformVertices |
                                      aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_GenUVCoords);

            if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode || !scene->HasMeshes())
            {
                VULTRA_CORE_ERROR("[MeshLoader] Assimp error: {}", importer.GetErrorString());
                return {};
            }

            gfx::DefaultMesh mesh {};
            mesh.vertexFormat = gfx::SimpleVertex::getVertexFormat();

            uint32_t vertexOffset = 0;
            uint32_t indexOffset  = 0;

            const auto loadTexture = [&p, &rd](const aiMaterial* material, const aiTextureType type) -> uint32_t {
                assert(material);

                // lambda to load a texture from a path
                auto loadTextureFromPath = [&rd](const std::filesystem::path& path) -> uint32_t {
                    auto texture = resource::loadResource<gfx::TextureManager>(path.generic_string());
                    if (texture)
                    {
                        // Find index if possible
                        auto loadedTextures = rd.getAllLoadedTextures();
                        for (uint32_t i = 0; i < loadedTextures.size(); ++i)
                        {
                            if (loadedTextures[i] == texture.get())
                                return i;
                        }

                        // Cache as new
                        rd.addLoadedTexture(texture);
                        return static_cast<uint32_t>(rd.getAllLoadedTextures().size() - 1);
                    }
                    return 0;
                };

                if (material->GetTextureCount(type) > 0)
                {
                    aiString path {};
                    material->GetTexture(type, 0, &path);
                    // If using optimized textures, try to load .ktx2 first
                    if (gfx::MeshManager::getGlobalLoadingSettings().useOptimizedTextures)
                    {
                        auto basisPath = ("imported" / p.parent_path() / path.data).replace_extension(".ktx2");
                        if (std::filesystem::exists(basisPath))
                        {
                            return loadTextureFromPath(basisPath);
                        }
                    }

                    // Fallback to original texture path
                    return loadTextureFromPath((p.parent_path() / path.data).generic_string());
                }

                VULTRA_CORE_WARN("[MeshLoader] Material has no texture of type {}", magic_enum::enum_name(type).data());
                return 0; // Fallback to white 1x1
            };

            const auto processMesh = [&mesh, &scene, loadTexture](
                                         const aiMesh* aiMesh, uint32_t& vertexOffset, uint32_t& indexOffset) {
                VULTRA_CORE_TRACE("[MeshLoader] Processing SubMesh: {}, with {} vertices, {} faces, material index {}, "
                                  "material name {}",
                                  aiMesh->mName.C_Str(),
                                  aiMesh->mNumVertices,
                                  aiMesh->mNumFaces,
                                  aiMesh->mMaterialIndex,
                                  aiMesh->mMaterialIndex < scene->mNumMaterials ?
                                      scene->mMaterials[aiMesh->mMaterialIndex]->GetName().C_Str() :
                                      "Unknown");

                // Copy vertices
                std::vector<gfx::SimpleVertex> subMeshVertices;
                for (unsigned int v = 0; v < aiMesh->mNumVertices; ++v)
                {
                    gfx::SimpleVertex vertex {};
                    vertex.position = {aiMesh->mVertices[v].x, aiMesh->mVertices[v].y, aiMesh->mVertices[v].z};

                    if (aiMesh->HasVertexColors(0))
                    {
                        vertex.color = {aiMesh->mColors[0][v].r, aiMesh->mColors[0][v].g, aiMesh->mColors[0][v].b};
                    }
                    else
                    {
                        vertex.color = {1.0f, 1.0f, 1.0f};
                    }

                    if (aiMesh->HasNormals())
                    {
                        vertex.normal = {aiMesh->mNormals[v].x, aiMesh->mNormals[v].y, aiMesh->mNormals[v].z};
                    }

                    if (aiMesh->HasTextureCoords(0))
                    {
                        vertex.texCoord = {aiMesh->mTextureCoords[0][v].x, aiMesh->mTextureCoords[0][v].y};
                    }

                    if (aiMesh->HasTangentsAndBitangents())
                    {
                        glm::vec3 tangent   = {aiMesh->mTangents[v].x, aiMesh->mTangents[v].y, aiMesh->mTangents[v].z};
                        glm::vec3 bitangent = {
                            aiMesh->mBitangents[v].x, aiMesh->mBitangents[v].y, aiMesh->mBitangents[v].z};
                        glm::vec4 tangentWithHandness = glm::vec4(
                            tangent, glm::dot(glm::cross(vertex.normal, bitangent), tangent) < 0.0f ? -1.0f : 1.0f);
                        vertex.tangent = tangentWithHandness;
                    }

                    mesh.vertices.push_back(vertex);
                    subMeshVertices.push_back(vertex);
                }

                // Copy indices
                for (unsigned int f = 0; f < aiMesh->mNumFaces; ++f)
                {
                    const aiFace& face = aiMesh->mFaces[f];
                    assert(face.mNumIndices == 3); // Ensure the mesh is triangulated
                    mesh.indices.push_back(face.mIndices[0]);
                    mesh.indices.push_back(face.mIndices[1]);
                    mesh.indices.push_back(face.mIndices[2]);
                }

                // Fill in sub-mesh data
                gfx::SubMesh subMesh {};
                // subMesh.topology = static_cast<rhi::PrimitiveTopology>(aiMesh->mPrimitiveTypes);
                subMesh.name          = aiMesh->mName.C_Str();
                subMesh.vertexOffset  = vertexOffset;
                subMesh.indexOffset   = indexOffset;
                subMesh.vertexCount   = aiMesh->mNumVertices;
                subMesh.indexCount    = aiMesh->mNumFaces * 3;
                subMesh.materialIndex = aiMesh->mMaterialIndex;

                // Cook AABB
                subMesh.aabb = AABB::build(subMeshVertices);

                mesh.subMeshes.push_back(subMesh);

                vertexOffset += subMesh.vertexCount;
                indexOffset += subMesh.indexCount;
            };

            const auto processMaterials = [&mesh, &loadTexture](const aiScene* aiScene) {
                mesh.materials.resize(aiScene->mNumMaterials);

                for (unsigned int i = 0; i < aiScene->mNumMaterials; ++i)
                {
                    const aiMaterial* material = aiScene->mMaterials[i];
                    if (!material)
                        continue;

                    auto             props = gfx::parseMaterialProperties(material);
                    gfx::PBRMaterial pbrMat {};

                    // Name
                    aiString name;
                    if (tryGet(props, AI_MATKEY_NAME, name))
                    {
                        pbrMat.name = name.C_Str();
                    }

                    // Read color properties
                    aiColor3D kd(1, 1, 1), ks(0, 0, 0), ke(0, 0, 0), ka(0, 0, 0);
                    tryGet(props, AI_MATKEY_COLOR_DIFFUSE, kd);
                    tryGet(props, AI_MATKEY_COLOR_SPECULAR, ks);
                    tryGet(props, AI_MATKEY_COLOR_EMISSIVE, ke);
                    tryGet(props, AI_MATKEY_COLOR_AMBIENT, ka);

                    // Read scalar properties
                    float Ns = 0.0f, d = 1.0f, Ni = 1.5f, emissiveIntensity = 1.0f;
                    tryGet(props, AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity);
                    tryGet(props, AI_MATKEY_SHININESS, Ns);
                    tryGet(props, AI_MATKEY_OPACITY, d);
                    tryGet(props, AI_MATKEY_REFRACTI, Ni);

                    // Alpha masking & Blend mode
                    aiString alphaMode;
                    if (tryGet(props, AI_MATKEY_GLTF_ALPHAMODE, alphaMode))
                    {
                        auto alphaModeStr = std::string(alphaMode.C_Str());
                        if (alphaModeStr == "MASK")
                            pbrMat.alphaMode = rhi::AlphaMode::eMask;
                        else if (alphaModeStr == "BLEND")
                            pbrMat.alphaMode = rhi::AlphaMode::eBlend;
                        else
                            pbrMat.alphaMode = rhi::AlphaMode::eOpaque;
                    }
                    float alphaCutoff = 0.5f;
                    if (tryGet(props, AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff))
                    {
                        pbrMat.alphaCutoff = alphaCutoff;
                    }
                    aiBlendMode blendMode = aiBlendMode_Default;
                    if (tryGet(props, AI_MATKEY_BLEND_FUNC, blendMode))
                    {
                        pbrMat.blendState = toBlendState(blendMode);
                    }
                    else
                    {
                        pbrMat.blendState = d < 1.0f ? toBlendState(aiBlendMode::aiBlendMode_Default) : kNoBlend;
                    }

                    pbrMat.baseColor = glm::vec4(kd.r, kd.g, kd.b, 1.0);
                    pbrMat.opacity   = d;

                    pbrMat.metallicFactor = std::clamp(
                        (0.2126f * ks.r + 0.7152f * ks.g + 0.0722f * ks.b - 0.04f) / (1.0f - 0.04f), 0.0f, 1.0f);
                    pbrMat.roughnessFactor        = glm::clamp(std::sqrt(2.0f / (Ns + 2.0f)), 0.04f, 1.0f);
                    pbrMat.emissiveColorIntensity = glm::vec4(ke.r, ke.g, ke.b, emissiveIntensity);
                    pbrMat.ior                    = Ni;
                    pbrMat.ambientColor           = glm::vec4(ka.r, ka.g, ka.b, 1.0f);

                    // Override with common PBR extensions if present
                    aiColor4D baseColorFactorPBR(1, 1, 1, 1), emissiveIntensityPBR(0, 0, 0, 1);
                    float     metallicFactorPBR  = 0.0f;
                    float     roughnessFactorPBR = 0.5f;
                    if (tryGet(props, AI_MATKEY_BASE_COLOR, baseColorFactorPBR))
                    {
                        pbrMat.baseColor =
                            glm::vec4(baseColorFactorPBR.r, baseColorFactorPBR.g, baseColorFactorPBR.b, 1.0);
                    }
                    if (tryGet(props, AI_MATKEY_METALLIC_FACTOR, metallicFactorPBR))
                    {
                        pbrMat.metallicFactor = metallicFactorPBR;
                    }
                    if (tryGet(props, AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactorPBR))
                    {
                        pbrMat.roughnessFactor = roughnessFactorPBR;
                    }
                    if (tryGet(props, AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensityPBR))
                    {
                        pbrMat.emissiveColorIntensity = glm::vec4(emissiveIntensityPBR.r,
                                                                  emissiveIntensityPBR.g,
                                                                  emissiveIntensityPBR.b,
                                                                  emissiveIntensityPBR.a);
                    }

                    // Textures
                    pbrMat.albedoIndex            = loadTexture(material, aiTextureType_DIFFUSE);
                    pbrMat.alphaMaskIndex         = loadTexture(material, aiTextureType_OPACITY);
                    pbrMat.metallicIndex          = loadTexture(material, aiTextureType_METALNESS);
                    pbrMat.roughnessIndex         = loadTexture(material, aiTextureType_DIFFUSE_ROUGHNESS);
                    pbrMat.specularIndex          = loadTexture(material, aiTextureType_SPECULAR);
                    pbrMat.normalIndex            = loadTexture(material, aiTextureType_NORMALS);
                    pbrMat.aoIndex                = loadTexture(material, aiTextureType_LIGHTMAP);
                    pbrMat.emissiveIndex          = loadTexture(material, aiTextureType_EMISSIVE);
                    pbrMat.metallicRoughnessIndex = loadTexture(material, aiTextureType_GLTF_METALLIC_ROUGHNESS);

                    // Double sided?
                    bool doubleSided = false;
                    if (tryGet(props, AI_MATKEY_TWOSIDED, doubleSided))
                    {
                        pbrMat.doubleSided = doubleSided;
                    }

                    // Unhandled properties warning
                    for (auto& [k, v] : props)
                    {
                        if (!v.parsed)
                        {
                            VULTRA_CORE_WARN(
                                "[MeshLoader] Material {} has unhandled property: key='{}' semantic={} index={}",
                                pbrMat.name,
                                k.key,
                                k.semantic,
                                k.index);
                        }
                    }

                    // Log material info
                    VULTRA_CORE_TRACE("[MeshLoader] Loaded Material[{}]: [{}]", i, pbrMat.toString());

                    mesh.materials[i] = pbrMat;
                }
            };

            std::function<void(const aiNode*, const aiScene*)> processNode;
            processNode = [&processMesh, &processNode, &vertexOffset, &indexOffset, &mesh](const aiNode*  aiNode,
                                                                                           const aiScene* aiScene) {
                for (uint32_t i = 0; i < aiNode->mNumMeshes; ++i)
                {
                    aiMesh* aiMesh = aiScene->mMeshes[aiNode->mMeshes[i]];
                    processMesh(aiMesh, vertexOffset, indexOffset);
                }

                for (uint32_t i = 0; i < aiNode->mNumChildren; ++i)
                {
                    processNode(aiNode->mChildren[i], aiScene);
                }
            };

            processMaterials(scene);
            processNode(scene->mRootNode, scene);

            // Cook AABB
            mesh.aabb = AABB::build(mesh.vertices);

            // Generate meshlets
            generateMeshlets(mesh);

            mesh.vertexBuffer = createRef<rhi::VertexBuffer>(
                std::move(rd.createVertexBuffer(mesh.getVertexStride(), mesh.getVertexCount())));
            auto stagingVertexBuffer =
                rd.createStagingBuffer(mesh.getVertexStride() * mesh.getVertexCount(), mesh.vertices.data());

            rhi::Buffer stagingIndexBuffer {};
            if (mesh.indices.size() > 0)
            {
                mesh.indexBuffer = createRef<rhi::IndexBuffer>(std::move(
                    rd.createIndexBuffer(static_cast<rhi::IndexType>(mesh.getIndexStride()), mesh.getIndexCount())));
                stagingIndexBuffer =
                    rd.createStagingBuffer(mesh.getIndexStride() * mesh.getIndexCount(), mesh.indices.data());
            }

            rd.execute(
                [&](rhi::CommandBuffer& cb) {
                    cb.copyBuffer(
                        stagingVertexBuffer, *mesh.vertexBuffer, vk::BufferCopy {0, 0, stagingVertexBuffer.getSize()});
                    if (stagingIndexBuffer)
                    {
                        cb.copyBuffer(
                            stagingIndexBuffer, *mesh.indexBuffer, vk::BufferCopy {0, 0, stagingIndexBuffer.getSize()});
                    }
                },
                true);

            // Build material buffer (for bindless descriptors)
            mesh.buildMaterialBuffer(rd);

            // Build meshlet buffers for each sub-mesh
            for (auto& sm : mesh.subMeshes)
            {
                sm.buildMeshletBuffers(rd);
            }

            mesh.buildRenderMesh(rd);

            // Find light meshes (meshes with emissive materials)
            for (const auto& sm : mesh.subMeshes)
            {
                if (sm.materialIndex < mesh.materials.size())
                {
                    const auto& mat = mesh.materials[sm.materialIndex];
                    if (mat.emissiveColorIntensity.r > 0.0f || mat.emissiveColorIntensity.g > 0.0f ||
                        mat.emissiveColorIntensity.b > 0.0f || mat.emissiveIndex > 0)
                    {
                        // This sub-mesh is emissive, add its vertices to light mesh
                        std::vector<gfx::SimpleVertex> vertices;
                        for (uint32_t vi = 0; vi < sm.vertexCount; ++vi)
                        {
                            const auto& v = mesh.vertices[sm.vertexOffset + vi];
                            vertices.push_back(v);
                        }
                        mesh.lights.push_back(
                            {.vertices = std::move(vertices), .colorIntensity = mat.emissiveColorIntensity});
                        VULTRA_CORE_INFO(
                            "[MeshLoader] Found light mesh in sub-mesh {}, material {}, total light vertices {}",
                            sm.name,
                            mesh.materials[sm.materialIndex].name,
                            mesh.lights.back().vertices.size());
                    }
                }
            }

            return mesh;
        }
    } // namespace resource
} // namespace vultra
