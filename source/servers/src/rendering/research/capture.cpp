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

    Image readback(Device& device, Texture& texture, uint32_t mip)
    {
        if (!(texture.desc.usage & VriTextureUsage_TransferSrc) || texture.desc.sampleNum != 1 ||
            texture.desc.layerNum != 1 || mip >= texture.desc.mipNum || texture.state.layout == VriLayout_Undefined)
        {
            throw std::invalid_argument(
                "Readback requires a valid mip of an initialized single-sample, single-layer TransferSrc texture");
        }
        uint32_t texelBytes = 0;
        bool     bgra       = false;
        bool     depth      = false;
        switch (texture.desc.format)
        {
            case VriFormat_BGRA8_UNORM:
            case VriFormat_BGRA8_SRGB:
                bgra = true;
                [[fallthrough]];
            case VriFormat_RGBA8_UNORM:
            case VriFormat_RGBA8_SRGB:
                texelBytes = 4;
                break;
            case VriFormat_RGBA16_SFLOAT:
                texelBytes = 8;
                break;
            case VriFormat_RGBA32_SFLOAT:
                texelBytes = 16;
                break;
            case VriFormat_D32_SFLOAT:
                texelBytes = 4;
                depth      = true;
                break;
            default:
                throw std::invalid_argument("Unsupported capture format");
        }
        const Extent   size {std::max(texture.desc.width >> mip, 1u), std::max(texture.desc.height >> mip, 1u)};
        const uint64_t bytes = uint64_t(size.width) * size.height * texelBytes;
        Buffer         staging(device, {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        Frame          frame(device);
        auto*          cmd      = frame.begin();
        const auto     previous = texture.state;
        texture.transition(cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        VriBufferTextureCopyDesc region {};
        region.texture.layerNum = 1;
        region.texture.aspect   = depth ? VriImageAspect_Depth : VriImageAspect_Color;
        region.texture.mip      = mip;
        region.texture.width    = size.width;
        region.texture.height   = size.height;
        region.texture.depth    = 1;
        device.core.CmdReadbackTextureToBuffer(cmd, staging.handle, texture.handle, &region);
        texture.transition(cmd, previous);
        frame.submitAndWait();
        Image image {size, std::vector<float>(size_t(size.width) * size.height * 4)};
        auto* data = static_cast<const unsigned char*>(device.core.MapBuffer(staging.handle, 0, bytes));
        if (!data)
        {
            throw std::runtime_error("Map capture buffer failed");
        }
        for (size_t i = 0; i < image.rgba.size(); ++i)
        {
            if (depth)
            {
                float value;
                std::memcpy(&value, data + (i / 4) * 4, 4);
                image.rgba[i] = i % 4 == 3 ? 1 : value;
            }
            else if (texelBytes == 4)
            {
                const size_t channel = i % 4;
                const size_t source  = bgra && (channel == 0 || channel == 2) ? i - channel + (2 - channel) : i;
                image.rgba[i]        = float(data[source]) / 255;
            }
            else if (texelBytes == 8)
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
        device.core.UnmapBuffer(staging.handle);
        return image;
    }
} // namespace vultra
