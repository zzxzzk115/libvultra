#pragma once

#include <vultra/servers/rendering/texture_blit.hpp>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numeric>

namespace vultra::testing
{
    inline std::expected<void, std::string> textureBlitProbe(Device& device, const ShaderProgram& program)
    {
        auto invalid       = program;
        invalid.parameters = {};
        if (TextureBlit::create(device, VriFormat_RGBA32_SFLOAT, 2, invalid))
        {
            return std::unexpected("Blit accepted an incompatible push-constant layout");
        }
        auto blit = TextureBlit::create(device, VriFormat_RGBA32_SFLOAT, 2, program);
        if (!blit)
        {
            return std::unexpected(blit.error());
        }
        constexpr Extent sourceSize {2, 2};
        constexpr Extent targetSize {18, 8};
        using Pixel = std::array<float, 4>;
        const std::array<Pixel, 4> halfPixels {Pixel {0, .25f, .5f, 1},
                                               Pixel {.25f, .5f, .75f, .5f},
                                               Pixel {.5f, .75f, 1, .25f},
                                               Pixel {1, .5f, .25f, 0}};
        const std::array<Pixel, 4> floatPixels {Pixel {100000.125f, 2, -5.25f, -.12345679f},
                                                Pixel {200000.25f, -4, 7.5f, .25f},
                                                Pixel {300000.5f, 8, 1.25f, .5f},
                                                Pixel {400000.75f, 16, 10.5f, .75f}};
        const auto*                caps = device.core.GetDeviceDesc(device.handle);
        const uint64_t             rowAlignment =
            std::lcm(uint64_t(16), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
        const uint64_t sourceRow = (sourceSize.width * 16 + rowAlignment - 1) / rowAlignment * rowAlignment;
        const uint64_t row       = (targetSize.width * 16 + rowAlignment - 1) / rowAlignment * rowAlignment;
        std::array<std::unique_ptr<Texture>, 2> sources;
        std::array<std::unique_ptr<Buffer>, 2>  uploads;
        for (size_t i = 0; i < sources.size(); ++i)
        {
            const auto format = i == 0 ? VriFormat_RGBA16_SFLOAT : VriFormat_RGBA32_SFLOAT;
            auto       source = Texture::create(device, colorTexture(sourceSize, format));
            auto       upload = Buffer::create(
                device,
                {sourceRow * sourceSize.height, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            if (!source || !upload)
            {
                return std::unexpected("Create filtered-blit source resources failed");
            }
            sources[i]  = std::move(*source);
            uploads[i]  = std::move(*upload);
            auto* bytes = static_cast<std::byte*>(device.core.MapBuffer(uploads[i]->handle, 0, uploads[i]->desc.size));
            if (!bytes)
            {
                return std::unexpected("Map filtered-blit upload failed");
            }
            const uint64_t componentSize = i == 0 ? 2 : 4;
            const auto&    pixels        = i == 0 ? halfPixels : floatPixels;
            for (uint32_t y = 0; y < sourceSize.height; ++y)
            {
                for (uint32_t x = 0; x < sourceSize.width; ++x)
                {
                    for (size_t channel = 0; channel < 4; ++channel)
                    {
                        auto*      destination = bytes + y * sourceRow + (x * 4 + channel) * componentSize;
                        const auto value       = pixels[y * sourceSize.width + x][channel];
                        if (i == 0)
                        {
                            const auto packed = glm::packHalf1x16(value);
                            std::memcpy(destination, &packed, sizeof(packed));
                        }
                        else
                        {
                            std::memcpy(destination, &value, sizeof(value));
                        }
                    }
                }
            }
            device.core.UnmapBuffer(uploads[i]->handle);
        }
        auto target = Texture::create(device, colorTexture(targetSize, VriFormat_RGBA32_SFLOAT));
        auto readback =
            Buffer::create(device,
                           {row * targetSize.height, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto frame = Frame::create(device);
        if (!target || !readback || !frame)
        {
            return std::unexpected("Create filtered-blit output resources failed");
        }
        if (!(*blit)->setSourceExpected(0, *sources[0]) || !(*blit)->setSourceExpected(1, *sources[0]) ||
            (*blit)->setSourceExpected(2, *sources[0]))
        {
            return std::unexpected("Blit source-slot validation failed");
        }
        auto integer = Texture::create(device, colorTexture(sourceSize, VriFormat_R32_UINT));
        if (!integer || (*blit)->setSourceExpected(0, **integer))
        {
            return std::unexpected("Blit accepted an integer image through its floating-point shader");
        }

        // Force the software path even on devices with native float32 filtering, then fail its first sampler.
        static decltype(device.core.GetFormatSupport) nativeFormatSupport;
        static decltype(device.core.CreateSampler)    nativeCreateSampler;
        nativeFormatSupport          = device.core.GetFormatSupport;
        nativeCreateSampler          = device.core.CreateSampler;
        const auto forceUnfilterable = [](const VriDevice* native, VriFormat format)
        {
            const auto support = nativeFormatSupport(native, format);
            return format == VriFormat_RGBA32_SFLOAT ? support & ~VriFormatSupport_LinearFilter : support;
        };
        device.core.GetFormatSupport = forceUnfilterable;
        device.core.CreateSampler    = [](VriDevice*, const VriSamplerDesc*, VriDescriptor** out)
        {
            *out = nullptr;
            return VriResult_Failure;
        };
        auto failed                  = (*blit)->setSourceExpected(1, *sources[1]);
        device.core.CreateSampler    = nativeCreateSampler;
        device.core.GetFormatSupport = nativeFormatSupport;
        if (failed)
        {
            return std::unexpected("Blit did not report a failed manual-filter sampler");
        }

        bool       uploaded = false;
        const auto render   = [&](bool hdr) -> std::expected<void, std::string>
        {
            // Dyadic coordinates avoid native filtering's implementation-dependent subtexel quantization.
            // Keep an odd viewport for the FP32 software filter and verify the untouched border as well.
            std::array<VriRect, 2> rectangles {VriRect {0, 0, 8, 8}, VriRect {9, 0, 8, 8}};
            if (hdr)
            {
                rectangles[1] = {9, 0, 9, 7};
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (!uploaded)
            {
                for (size_t i = 0; i < sources.size(); ++i)
                {
                    uploads[i]->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                    sources[i]->transition(
                        *commands,
                        {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
                    VriBufferTextureCopyDesc copy {};
                    copy.bufferRowLength = uint32_t(sourceRow / (i == 0 ? 8 : 16));
                    copy.texture = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, sourceSize.width, sourceSize.height, 1};
                    device.core.CmdUploadBufferToTexture(*commands, sources[i]->handle, uploads[i]->handle, &copy);
                }
                uploaded = true;
            }
            (*target)->transition(
                *commands,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            auto view = (*target)->viewExpected();
            if (!view)
            {
                return std::unexpected(view.error());
            }
            const Pixel clear {};
            beginColorPass(device, *commands, *view, targetSize, clear.data());
            device.core.CmdEndRendering(*commands);
            for (uint32_t slot = 0; slot < 2; ++slot)
            {
                if (auto drawn = (*blit)->drawExpected(*commands, **target, rectangles[slot], slot); !drawn)
                {
                    return drawn;
                }
            }
            (*target)->transition(*commands,
                                  {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*readback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferTextureCopyDesc copy {};
            copy.bufferRowLength = uint32_t(row / 16);
            copy.texture         = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, targetSize.width, targetSize.height, 1};
            device.core.CmdReadbackTextureToBuffer(*commands, (*readback)->handle, (*target)->handle, &copy);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            const auto* bytes =
                static_cast<const std::byte*>(device.core.MapBuffer((*readback)->handle, 0, (*readback)->desc.size));
            if (!bytes)
            {
                return std::unexpected("Map filtered-blit output failed");
            }
            bool        valid = true;
            std::string difference;
            for (uint32_t y = 0; y < targetSize.height; ++y)
            {
                for (uint32_t x = 0; x < targetSize.width; ++x)
                {
                    const auto& rectangle = rectangles[x < 9 ? 0 : 1];
                    const bool  covered   = x < uint32_t(rectangle.x) + rectangle.width && y < rectangle.height;
                    const bool  floating  = hdr && x >= 9;
                    const auto& pixels    = floating ? floatPixels : halfPixels;
                    const float wx =
                        std::clamp((float(x - uint32_t(rectangle.x)) + .5f) / rectangle.width * 2 - .5f, 0.f, 1.f);
                    const float wy = std::clamp((float(y) + .5f) / rectangle.height * 2 - .5f, 0.f, 1.f);
                    Pixel       pixel {};
                    std::memcpy(pixel.data(), bytes + y * row + x * 16, 16);
                    for (size_t channel = 0; channel < 4; ++channel)
                    {
                        const float a         = std::lerp(pixels[0][channel], pixels[1][channel], wx);
                        const float b         = std::lerp(pixels[2][channel], pixels[3][channel], wx);
                        const float expected  = covered ? std::lerp(a, b, wy) : 0;
                        float       tolerance = .004f;
                        if (!covered)
                        {
                            tolerance = 0;
                        }
                        else if (floating)
                        {
                            tolerance = channel == 0 ? .25f : .00001f;
                        }
                        const bool matches =
                            std::isfinite(pixel[channel]) && std::abs(pixel[channel] - expected) <= tolerance;
                        valid &= matches;
                        if (!matches && difference.empty())
                        {
                            difference =
                                " hdr=" + std::to_string(hdr) + " x=" + std::to_string(x) + " y=" + std::to_string(y) +
                                " channel=" + std::to_string(channel) + " actual=" + std::to_string(pixel[channel]) +
                                " expected=" + std::to_string(expected) + " tolerance=" + std::to_string(tolerance);
                        }
                    }
                }
            }
            device.core.UnmapBuffer((*readback)->handle);
            if (!valid)
            {
                return std::unexpected(
                    "Filtered blit lost HDR/alpha, clamped incorrectly, or changed slots after failure" + difference);
            }
            return {};
        };
        if (auto preserved = render(false); !preserved)
        {
            return preserved;
        }
        device.core.GetFormatSupport = forceUnfilterable;
        auto bound                   = (*blit)->setSourceExpected(1, *sources[1]);
        device.core.GetFormatSupport = nativeFormatSupport;
        if (!bound)
        {
            return bound;
        }
        return render(true);
    }
} // namespace vultra::testing
