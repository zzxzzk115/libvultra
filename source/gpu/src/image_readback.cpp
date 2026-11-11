#include <vultra/gpu/image_readback.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace vultra
{
    namespace
    {
        float half(uint16_t h)
        {
            const auto exponent = (h >> 10) & 31;
            const auto mantissa = h & 1023;
            float      value;
            if (exponent == 0)
            {
                value = std::ldexp(float(mantissa), -24);
            }
            else if (exponent == 31)
            {
                value = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            }
            else
            {
                value = std::ldexp(float(1024 + mantissa), int(exponent) - 25);
            }

            return (h & 0x8000) ? -value : value;
        }
    } // namespace

    namespace
    {
        std::expected<uint32_t, Error> texelSize(VriFormat format)
        {
            switch (format)
            {
                case VriFormat_RGBA8_UNORM:
                case VriFormat_RGBA8_SRGB:
                case VriFormat_BGRA8_UNORM:
                case VriFormat_BGRA8_SRGB:
                case VriFormat_D32_SFLOAT:
                    return 4;
                case VriFormat_RGBA16_SFLOAT:
                    return 8;
                case VriFormat_RGBA32_SFLOAT:
                    return 16;
                default:
                    return makeError("Unsupported capture format");
            }
        }
    } // namespace

    namespace
    {
        std::expected<VriBufferDesc, Error> readbackBuffer(Device& device, Texture& texture, uint32_t mip)
        {
            if (!(texture.desc.usage & VriTextureUsage_TransferSrc) || texture.desc.sampleNum != 1 ||
                texture.desc.layerNum != 1 || mip >= texture.desc.mipNum || texture.state.layout == VriLayout_Undefined)
            {
                return makeError("Readback requires an initialized single-sample, single-layer TransferSrc mip");
            }
            const auto texel = texelSize(texture.desc.format);
            if (!texel)
            {
                return std::unexpected(texel.error());
            }
            const auto  texelBytes  = *texel;
            const auto* description = device.core.GetDeviceDesc(device.handle);
            // VRI describes buffer/texture rows in texels; the backend supplies their byte alignment.
            const auto alignment =
                std::lcm(uint64_t(texelBytes), uint64_t(std::max(description->uploadBufferTextureRowAlignment, 1u)));
            const auto tight    = uint64_t(std::max(texture.desc.width >> mip, 1u)) * texelBytes;
            const auto rowBytes = (tight + alignment - 1) / alignment * alignment;
            if (rowBytes / texelBytes > UINT32_MAX)
            {
                return makeError("Readback row length exceeds the VRI copy layout");
            }
            return VriBufferDesc {rowBytes * std::max(texture.desc.height >> mip, 1u),
                                  0,
                                  VriBufferUsage_TransferDst,
                                  VriMemoryLocation_HostReadback};
        }
    } // namespace

    ImageReadback::ImageReadback(Device&   device,
                                 Buffer    staging,
                                 Extent    size,
                                 VriFormat format,
                                 uint32_t  mip,
                                 uint32_t  texelBytes) :
        m_Device(device),
        m_Staging(std::move(staging)),
        m_Size(size),
        m_Format(format),
        m_Mip(mip),
        m_TexelBytes(texelBytes),
        m_RowBytes(m_Staging.desc.size / m_Size.height)
    {
    }

    std::expected<ImageReadback, Error> ImageReadback::create(Device& device, Texture& texture, uint32_t mip)
    {
        const auto description = readbackBuffer(device, texture, mip);
        if (!description)
        {
            return std::unexpected(description.error());
        }
        auto staging = Buffer::create(device, *description);
        if (!staging)
        {
            return std::unexpected(staging.error());
        }
        // readbackBuffer() accepted the format, so its texel size is known.
        return ImageReadback(device,
                             std::move(*staging),
                             {std::max(texture.desc.width >> mip, 1u), std::max(texture.desc.height >> mip, 1u)},
                             texture.desc.format,
                             mip,
                             *texelSize(texture.desc.format));
    }

    std::expected<void, Error> ImageReadback::record(VriCommandBuffer* cmd, Texture& texture)
    {
        if (auto valid = readbackBuffer(m_Device, texture, m_Mip); !valid)
        {
            return std::unexpected(valid.error());
        }
        if (texture.desc.format != m_Format || m_Mip >= texture.desc.mipNum ||
            m_Size != Extent {std::max(texture.desc.width >> m_Mip, 1u), std::max(texture.desc.height >> m_Mip, 1u)} ||
            texture.state.layout == VriLayout_Undefined || m_Recorded)
        {
            return makeError("Readback source changed, uninitialized, or already recorded");
        }
        auto&      device   = m_Device;
        const auto previous = texture.state;
        texture.transition(cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        VriBufferTextureCopyDesc region {};
        region.bufferRowLength  = uint32_t(m_RowBytes / m_TexelBytes);
        region.texture.layerNum = 1;
        region.texture.aspect   = m_Format == VriFormat_D32_SFLOAT ? VriImageAspect_Depth : VriImageAspect_Color;
        region.texture.mip      = m_Mip;
        region.texture.width    = m_Size.width;
        region.texture.height   = m_Size.height;
        region.texture.depth    = 1;
        device.core.CmdReadbackTextureToBuffer(cmd, m_Staging.handle, texture.handle, &region);
        texture.transition(cmd, previous);
        m_Recorded = true;
        return {};
    }

    std::expected<Image, Error> ImageReadback::consume()
    {
        if (!m_Recorded)
        {
            return makeError("Readback has not been recorded");
        }
        const auto size   = m_Size;
        const auto bytes  = m_Staging.desc.size;
        const bool depth  = m_Format == VriFormat_D32_SFLOAT;
        const bool bgra   = m_Format == VriFormat_BGRA8_UNORM || m_Format == VriFormat_BGRA8_SRGB;
        auto&      device = m_Device;
        Image      image {size, std::vector<float>(size_t(size.width) * size.height * 4)};
        auto*      data = static_cast<const unsigned char*>(device.core.MapBuffer(m_Staging.handle, 0, bytes));
        if (!data)
        {
            return makeError("Map capture buffer failed");
        }
        for (uint32_t row = 0; row < size.height; ++row)
        {
            const auto* sourceRow   = data + size_t(row * m_RowBytes);
            auto*       destination = image.rgba.data() + size_t(row) * size.width * 4;
            if (m_TexelBytes == 16)
            {
                std::memcpy(destination, sourceRow, size_t(size.width) * m_TexelBytes);
                continue;
            }
            for (size_t i = 0; i < size_t(size.width) * 4; ++i)
            {
                if (depth)
                {
                    float value;
                    std::memcpy(&value, sourceRow + (i / 4) * 4, 4);
                    destination[i] = i % 4 == 3 ? 1 : value;
                }
                else if (m_TexelBytes == 4)
                {
                    const size_t channel = i % 4;
                    const size_t source  = bgra && (channel == 0 || channel == 2) ? i - channel + (2 - channel) : i;
                    destination[i]       = float(sourceRow[source]) / 255;
                }
                else if (m_TexelBytes == 8)
                {
                    uint16_t value;
                    std::memcpy(&value, sourceRow + i * 2, 2);
                    destination[i] = half(value);
                }
                else
                {
                    std::memcpy(&destination[i], sourceRow + i * 4, 4);
                }
            }
        }
        device.core.UnmapBuffer(m_Staging.handle);
        m_Recorded = false;
        return image;
    }

    std::expected<Image, Error> readback(Device& device, Texture& texture, uint32_t mip)
    {
        auto capture = ImageReadback::create(device, texture, mip);
        if (!capture)
        {
            return std::unexpected(capture.error());
        }
        const auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto cmd = (*frame)->begin();
        if (!cmd)
        {
            return std::unexpected(cmd.error());
        }
        if (auto recorded = capture->record(*cmd, texture); !recorded)
        {
            return std::unexpected(recorded.error());
        }
        if (auto submitted = (*frame)->submitAndWait(); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        return capture->consume();
    }
} // namespace vultra
