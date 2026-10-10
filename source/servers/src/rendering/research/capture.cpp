#include <vultra/servers/rendering/research/capture.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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
        uint32_t texelSize(VriFormat format)
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
                    throw std::invalid_argument("Unsupported capture format");
            }
        }
    } // namespace

    namespace
    {
        VriBufferDesc readbackBuffer(Texture& texture, uint32_t mip)
        {
            if (!(texture.desc.usage & VriTextureUsage_TransferSrc) || texture.desc.sampleNum != 1 ||
                texture.desc.layerNum != 1 || mip >= texture.desc.mipNum || texture.state.layout == VriLayout_Undefined)
            {
                throw std::invalid_argument(
                    "Readback requires an initialized single-sample, single-layer TransferSrc mip");
            }
            const uint64_t bytes = uint64_t(std::max(texture.desc.width >> mip, 1u)) *
                                   std::max(texture.desc.height >> mip, 1u) * texelSize(texture.desc.format);
            return {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback};
        }
    } // namespace

    ImageReadback::ImageReadback(Device& device, Texture& texture, uint32_t mip) :
        m_Device(device),
        m_Staging(device, readbackBuffer(texture, mip)),
        m_Size {std::max(texture.desc.width >> mip, 1u), std::max(texture.desc.height >> mip, 1u)},
        m_Format(texture.desc.format),
        m_Mip(mip),
        m_TexelBytes(texelSize(texture.desc.format))
    {
    }

    void ImageReadback::record(VriCommandBuffer* cmd, Texture& texture)
    {
        readbackBuffer(texture, m_Mip);
        if (texture.desc.format != m_Format || m_Mip >= texture.desc.mipNum ||
            m_Size != Extent {std::max(texture.desc.width >> m_Mip, 1u), std::max(texture.desc.height >> m_Mip, 1u)} ||
            texture.state.layout == VriLayout_Undefined || m_Recorded)
        {
            throw std::invalid_argument("Readback source changed, uninitialized, or already recorded");
        }
        auto&      device   = m_Device;
        const auto previous = texture.state;
        texture.transition(cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        VriBufferTextureCopyDesc region {};
        region.texture.layerNum = 1;
        region.texture.aspect   = m_Format == VriFormat_D32_SFLOAT ? VriImageAspect_Depth : VriImageAspect_Color;
        region.texture.mip      = m_Mip;
        region.texture.width    = m_Size.width;
        region.texture.height   = m_Size.height;
        region.texture.depth    = 1;
        device.core.CmdReadbackTextureToBuffer(cmd, m_Staging.handle, texture.handle, &region);
        texture.transition(cmd, previous);
        m_Recorded = true;
    }

    Image ImageReadback::consume()
    {
        if (!m_Recorded)
        {
            throw std::logic_error("Readback has not been recorded");
        }
        const auto size   = m_Size;
        const auto bytes  = uint64_t(size.width) * size.height * m_TexelBytes;
        const bool depth  = m_Format == VriFormat_D32_SFLOAT;
        const bool bgra   = m_Format == VriFormat_BGRA8_UNORM || m_Format == VriFormat_BGRA8_SRGB;
        auto&      device = m_Device;
        Image      image {size, std::vector<float>(size_t(size.width) * size.height * 4)};
        auto*      data = static_cast<const unsigned char*>(device.core.MapBuffer(m_Staging.handle, 0, bytes));
        if (!data)
        {
            throw std::runtime_error("Map capture buffer failed");
        }
        if (m_TexelBytes == 16)
        {
            std::memcpy(image.rgba.data(), data, bytes);
        }
        else
        {
            for (size_t i = 0; i < image.rgba.size(); ++i)
            {
                if (depth)
                {
                    float value;
                    std::memcpy(&value, data + (i / 4) * 4, 4);
                    image.rgba[i] = i % 4 == 3 ? 1 : value;
                }
                else if (m_TexelBytes == 4)
                {
                    const size_t channel = i % 4;
                    const size_t source  = bgra && (channel == 0 || channel == 2) ? i - channel + (2 - channel) : i;
                    image.rgba[i]        = float(data[source]) / 255;
                }
                else if (m_TexelBytes == 8)
                {
                    uint16_t value;
                    std::memcpy(&value, data + i * 2, 2);
                    image.rgba[i] = half(value);
                }
                else
                {
                    std::memcpy(&image.rgba[i], data + i * 4, 4);
                }
            }
        }
        device.core.UnmapBuffer(m_Staging.handle);
        m_Recorded = false;
        return image;
    }

    Image readback(Device& device, Texture& texture, uint32_t mip)
    {
        ImageReadback capture(device, texture, mip);
        Frame         frame(device);
        capture.record(frame.begin(), texture);
        frame.submitAndWait();
        return capture.consume();
    }
} // namespace vultra
