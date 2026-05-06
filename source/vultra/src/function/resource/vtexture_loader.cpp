#include "vultra/function/resource/vtexture_loader.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/rhi/util.hpp"

#include <vbase/core/scope_exit.hpp>

#define DDSKTX_API static
#define DDSKTX_IMPLEMENT
#include <dds-ktx.h>
#include <ktx.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <tinyexr.h>

#include <magic_enum.hpp>
#include <algorithm>
#include <utility>

namespace vultra::resource
{
    namespace
    {
        // ============================================================
        // DDS / KTX
        // ============================================================
        rhi::PixelFormat toRHI(ddsktx_format format)
        {
            using enum rhi::PixelFormat;

            switch (format)
            {
                case DDSKTX_FORMAT_BC1:
                    return eBC1_UNorm;
                case DDSKTX_FORMAT_BC2:
                    return eBC2_UNorm;
                case DDSKTX_FORMAT_BC3:
                    return eBC3_UNorm;
                case DDSKTX_FORMAT_BC4:
                    return eBC4_UNorm;
                case DDSKTX_FORMAT_BC5:
                    return eBC5_UNorm;
                case DDSKTX_FORMAT_BC6H:
                    return eBC6H_RGB16F;
                case DDSKTX_FORMAT_BC7:
                    return eBC7_RGBA8_UNorm;

                case DDSKTX_FORMAT_RGBA8:
                    return eRGBA8_UNorm;

                default:
                    return eUndefined;
            }
        }

        rhi::PixelFormat toRHI(vasset::VTextureFormat format, uint32_t vkFormatHint = 0)
        {
            using enum rhi::PixelFormat;

            using VF = vasset::VTextureFormat;
            if (format == VF::eR8)
                return eR8_UNorm;
            if (format == VF::eRG8)
                return eRG8_UNorm;
            if (format == VF::eRG8S)
                return eRG8_SNorm;
            if (format == VF::eRGB8)
                return eRGB8_UNorm;
            if (format == VF::eRGBA8)
                return eRGBA8_UNorm;
            if (format == VF::eBGRA8)
                return eBGRA8_UNorm;

            // eR16 and eRGBA16 share a legacy enum value in VTextureFormat (70).
            // Use vkFormat hint from parsed KTX2 payload to disambiguate when available.
            if (format == VF::eR16 || format == VF::eRGBA16)
            {
                return vkFormatHint == 91u ? eRGBA16_UNorm : eR16_UNorm;
            }

            if (format == VF::eR16F)
                return eR16F;
            if (format == VF::eR32F)
                return eR32F;
            if (format == VF::eRG16)
                return eRG16_UNorm;
            if (format == VF::eRG16F)
                return eRG16F;
            if (format == VF::eRGBA16F)
                return eRGBA16F;
            if (format == VF::eRGBA32F)
                return eRGBA32F;

            if (format == VF::eBC1)
                return eBC1_UNorm;
            if (format == VF::eBC2)
                return eBC2_UNorm;
            if (format == VF::eBC3)
                return eBC3_UNorm;
            if (format == VF::eBC4)
                return eBC4_UNorm;
            if (format == VF::eBC5)
                return eBC5_UNorm;
            if (format == VF::eBC6H)
                return eBC6H_RGB16F;
            if (format == VF::eBC7)
                return eBC7_RGBA8_UNorm;

            return eUndefined;
        }

        vbase::Result<rhi::Texture, std::string> loadKTX_DDS(const std::vector<uint8_t>& bintex, rhi::RenderDevice& rd)
        {
            ddsktx_texture_info tc {0};

            if (!ddsktx_parse(&tc, bintex.data(), static_cast<int>(bintex.size()), nullptr))
            {
                return vbase::Result<rhi::Texture, std::string>::err("Failed to parse texture file.");
            }

            auto extent = rhi::Extent2D {static_cast<uint32_t>(tc.width), static_cast<uint32_t>(tc.height)};

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
                return vbase::Result<rhi::Texture, std::string>::err("Failed to create texture.");
            }

            for (int mip = 0; mip < tc.num_mips; ++mip)
            {
                for (int layer = 0; layer < tc.num_layers; ++layer)
                {
                    for (int face = 0; face < (tc.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP ? DDSKTX_CUBE_FACE_COUNT : 1);
                         ++face)
                    {
                        ddsktx_sub_data subData;
                        ddsktx_get_sub(&tc, &subData, bintex.data(), static_cast<int>(bintex.size()), layer, face, mip);
                        if (!subData.buff)
                        {
                            return vbase::Result<rhi::Texture, std::string>::err("Failed to get texture sub-data.");
                        }

                        const auto uploadSize       = subData.size_bytes;
                        const auto srcStagingBuffer = rd.createStagingBuffer(uploadSize, subData.buff);
                        if (!srcStagingBuffer)
                        {
                            return vbase::Result<rhi::Texture, std::string>::err("Failed to create staging buffer.");
                        }
                        std::array<rhi::BufferImageCopy, 1> copyRegions {};
                        copyRegions[0].bufferOffset      = 0;
                        copyRegions[0].bufferRowLength   = 0;
                        copyRegions[0].bufferImageHeight = 0;
                        copyRegions[0].aspectMask        = rhi::ImageAspectFlags::eColor;
                        copyRegions[0].mipLevel          = static_cast<uint32_t>(mip);
                        const uint32_t faceCount         =
                            static_cast<uint32_t>(tc.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP ? DDSKTX_CUBE_FACE_COUNT : 1);
                        copyRegions[0].baseArrayLayer    = static_cast<uint32_t>(layer) * faceCount + static_cast<uint32_t>(face);
                        copyRegions[0].layerCount        = 1;
                        copyRegions[0].imageOffsetX      = 0;
                        copyRegions[0].imageOffsetY      = 0;
                        copyRegions[0].imageOffsetZ      = 0;
                        copyRegions[0].imageExtentWidth  = static_cast<uint32_t>(subData.width);
                        copyRegions[0].imageExtentHeight = static_cast<uint32_t>(subData.height);
                        copyRegions[0].imageExtentDepth  = std::max(1u, static_cast<uint32_t>(tc.depth));
                        rhi::upload(rd, srcStagingBuffer, copyRegions, texture, false);
                    }
                }
            }

            return vbase::Result<rhi::Texture, std::string>::ok(std::move(texture));
        }

        // ============================================================
        // STB
        // ============================================================
        vbase::Result<rhi::Texture, std::string> loadSTB(const std::vector<uint8_t>& bin, rhi::RenderDevice& rd)
        {
            stbi_set_flip_vertically_on_load(false);

            int w, h;

            const bool hdr = stbi_is_hdr_from_memory(bin.data(), static_cast<int>(bin.size()));

            void* pixels = hdr ? reinterpret_cast<void*>(stbi_loadf_from_memory(
                                     bin.data(), static_cast<int>(bin.size()), &w, &h, nullptr, STBI_rgb_alpha)) :
                                 reinterpret_cast<void*>(stbi_load_from_memory(
                                     bin.data(), static_cast<int>(bin.size()), &w, &h, nullptr, STBI_rgb_alpha));

            if (!pixels)
                return vbase::Result<rhi::Texture, std::string>::err(stbi_failure_reason());

            vbase::ScopeExit freePixels([&] { stbi_image_free(pixels); });

            const rhi::Extent2D extent {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};

            auto mip = rhi::calcMipLevels(extent);
            if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
            {
                // WebGPU runtime mip generation is not implemented yet.
                mip = 1u;
            }

            auto format = hdr ? rhi::PixelFormat::eRGBA32F : rhi::PixelFormat::eRGBA8_UNorm;

            auto tex = rhi::Texture::Builder {}
                           .setExtent(extent)
                           .setPixelFormat(format)
                           .setNumMipLevels(mip)
                           .setUsageFlags(rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled |
                                          (mip > 1 ? rhi::ImageUsage::eTransferSrc : rhi::ImageUsage {}))
                           .setupOptimalSampler(true)
                           .build(rd);

            if (!tex)
                return vbase::Result<rhi::Texture, std::string>::err("Texture create failed");

            size_t size = w * h * 4 * (hdr ? sizeof(float) : 1);

            auto staging = rd.createStagingBuffer(size, pixels);

            rhi::upload(rd, staging, {}, tex, mip > 1);

            return vbase::Result<rhi::Texture, std::string>::ok(std::move(tex));
        }

        // ============================================================
        // EXR
        // ============================================================
        vbase::Result<rhi::Texture, std::string> loadEXR(const std::vector<uint8_t>& bin, rhi::RenderDevice& rd)
        {

            float* pixels;

            int w, h;

            const char* err;

            if (LoadEXRFromMemory(&pixels, &w, &h, bin.data(), static_cast<int>(bin.size()), &err) != TINYEXR_SUCCESS)
            {
                return vbase::Result<rhi::Texture, std::string>::err(err ? err : "EXR failed");
            }

            vbase::ScopeExit freePixels([&] { free(pixels); });

            const rhi::Extent2D extent {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};

            auto mip = rhi::calcMipLevels(extent);
            if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
            {
                // WebGPU runtime mip generation is not implemented yet.
                mip = 1u;
            }

            auto tex = rhi::Texture::Builder {}
                           .setExtent(extent)
                           .setPixelFormat(rhi::PixelFormat::eRGBA32F)
                           .setNumMipLevels(mip)
                           .setUsageFlags(rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled)
                           .setupOptimalSampler(true)
                           .build(rd);

            auto staging = rd.createStagingBuffer(w * h * 4 * sizeof(float), pixels);

            rhi::upload(rd, staging, {}, tex, mip > 1);

            return vbase::Result<rhi::Texture, std::string>::ok(std::move(tex));
        }

        // ============================================================
        // KTX2
        // ============================================================
        vbase::Result<rhi::Texture, std::string> loadKTX2(const vasset::VTexture& v, rhi::RenderDevice& rd)
        {
            ktxTexture2* tex = nullptr;

            if (ktxTexture2_CreateFromMemory(v.data.data(), v.data.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex) !=
                KTX_SUCCESS)
            {
                return vbase::Result<rhi::Texture, std::string>::err("ktx load fail");
            }

            vbase::ScopeExit freeTex([&] { ktxTexture_Destroy((ktxTexture*)tex); });

            auto chooseBasisTarget = [&rd]() {
                const auto usage = rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst;
                if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                {
                    // Current WebGPU format mapping path only guarantees uncompressed RGBA.
                    // Prefer correctness/stability first, then add compressed-format mappings later.
                    return std::pair {KTX_TTF_RGBA32, rhi::PixelFormat::eRGBA8_UNorm};
                }
                if (rhi::isFormatSupported(rd, rhi::PixelFormat::eBC7_RGBA8_UNorm, usage))
                {
                    return std::pair {KTX_TTF_BC7_RGBA, rhi::PixelFormat::eBC7_RGBA8_UNorm};
                }
                if (rhi::isFormatSupported(rd, rhi::PixelFormat::eBC3_UNorm, usage))
                {
                    return std::pair {KTX_TTF_BC3_RGBA, rhi::PixelFormat::eBC3_UNorm};
                }
                if (rhi::isFormatSupported(rd, rhi::PixelFormat::eBC1_UNorm, usage))
                {
                    return std::pair {KTX_TTF_BC1_RGB, rhi::PixelFormat::eBC1_UNorm};
                }
                return std::pair {KTX_TTF_RGBA32, rhi::PixelFormat::eRGBA8_UNorm};
            };

            const bool needsTranscoding = ktxTexture2_NeedsTranscoding(tex) != 0;
            const bool useBasisUPath    = v.compressedBasisU;

            if (useBasisUPath != needsTranscoding)
            {
                return vbase::Result<rhi::Texture, std::string>::err(
                    "KTX2 metadata mismatch: compressedBasisU does not match payload. Please reimport/repack textures.");
            }

            rhi::PixelFormat pixelFormat = rhi::PixelFormat::eUndefined;
            if (useBasisUPath)
            {
                auto [basisTarget, basisPixelFormat] = chooseBasisTarget();
                pixelFormat                           = basisPixelFormat;

                const auto transcodeResult = ktxTexture2_TranscodeBasis(tex, basisTarget, 0);
                if (transcodeResult != KTX_SUCCESS)
                {
                    return vbase::Result<rhi::Texture, std::string>::err("KTX2 transcode failed");
                }
            }
            else
            {
                pixelFormat = toRHI(v.format, tex->vkFormat);
                if (pixelFormat == rhi::PixelFormat::eUndefined)
                {
                    return vbase::Result<rhi::Texture, std::string>::err("Unsupported KTX2 pixel format");
                }
            }

            auto rhiTex = rhi::Texture::Builder {}
                              .setExtent({tex->baseWidth, tex->baseHeight})
                              .setPixelFormat(pixelFormat)
                              .setNumMipLevels(tex->numLevels)
                              .setUsageFlags(rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled)
                              .setupOptimalSampler(true)
                              .build(rd);
            if (!rhiTex)
            {
                return vbase::Result<rhi::Texture, std::string>::err("Failed to create texture.");
            }

            // If the KTX2 has mipmaps, we will upload each mip level separately.
            if (tex->numLevels > 1)
            {
                for (uint32_t mip = 0; mip < tex->numLevels; mip++)
                {
                    for (uint32_t layer = 0; layer < tex->numLayers; layer++)
                    {
                        for (uint32_t face = 0; face < tex->numFaces; face++)
                        {
                            ktx_size_t offset;

                            ktxTexture_GetImageOffset((ktxTexture*)tex, mip, layer, face, &offset);

                            auto staging = rd.createStagingBuffer(ktxTexture_GetImageSize((ktxTexture*)tex, mip),
                                                                  tex->pData + offset);

                            // Arrange copy regions for this mip level, layer and face.
                            std::array<rhi::BufferImageCopy, 1> copyRegions {};
                            copyRegions[0].bufferOffset      = 0;
                            copyRegions[0].bufferRowLength   = 0;
                            copyRegions[0].bufferImageHeight = 0;
                            copyRegions[0].aspectMask        = rhi::ImageAspectFlags::eColor;
                            copyRegions[0].mipLevel          = static_cast<uint32_t>(mip);
                            copyRegions[0].baseArrayLayer =
                                static_cast<uint32_t>(layer) * tex->numFaces + static_cast<uint32_t>(face);
                            copyRegions[0].layerCount        = 1;
                            copyRegions[0].imageOffsetX      = 0;
                            copyRegions[0].imageOffsetY      = 0;
                            copyRegions[0].imageOffsetZ      = 0;
                            copyRegions[0].imageExtentWidth  = std::max(1u, static_cast<uint32_t>(tex->baseWidth >> mip));
                            copyRegions[0].imageExtentHeight = std::max(1u, static_cast<uint32_t>(tex->baseHeight >> mip));
                            copyRegions[0].imageExtentDepth  = std::max(1u, static_cast<uint32_t>(tex->baseDepth));

                            rhi::upload(rd, staging, copyRegions, rhiTex, false);
                        }
                    }
                }
            }
            // If the KTX2 does not have mipmaps, we let gpu generate them.
            else
            {
                ktx_size_t offset;

                ktxTexture_GetImageOffset((ktxTexture*)tex, 0, 0, 0, &offset);

                auto staging =
                    rd.createStagingBuffer(ktxTexture_GetImageSize((ktxTexture*)tex, 0), tex->pData + offset);

                const bool generateMipmaps = rd.getBackendApi() != rhi::RenderBackendApi::eWebGPU;
                rhi::upload(rd, staging, {}, rhiTex, generateMipmaps);
            }

            return vbase::Result<rhi::Texture, std::string>::ok(std::move(rhiTex));
        }
    } // namespace

    // ============================================================
    // PUBLIC API
    // ============================================================
    vbase::Result<rhi::Texture, std::string> loadTextureFromVTexture(const vasset::VTexture& v, rhi::RenderDevice& rd)
    {
        using enum vasset::VTextureFileFormat;

        switch (v.fileFormat)
        {
            case eKTX2:
                return loadKTX2(v, rd);

            case eEXR:
                return loadEXR(v.data, rd);

            case ePNG:
            case eJPG:
            case eJPEG:
            case eHDR:
            case eBMP:
            case eTGA:
            case eGIF:
            case ePSD:
            case ePIC:
                return loadSTB(v.data, rd);

            case eKTX:
            case eDDS:
                return loadKTX_DDS(v.data, rd);

            default:
                return vbase::Result<rhi::Texture, std::string>::err("Unsupported format");
        }
    }
} // namespace vultra::resource
