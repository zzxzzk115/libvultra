#include <vultra/drivers/rhi/image_readback.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace vultra
{
    namespace
    {
        struct Block
        {
            uint32_t width  = 1;
            uint32_t height = 1;
            uint32_t bytes;
        };

        std::expected<Block, std::string> formatBlockExpected(VriFormat format, VriImageAspectFlags aspect)
        {
            switch (format)
            {
                case VriFormat_R8_UNORM:
                case VriFormat_R8_SNORM:
                case VriFormat_R8_UINT:
                case VriFormat_R8_SINT:
                case VriFormat_S8_UINT:
                    return Block {1, 1, 1};
                case VriFormat_RG8_UNORM:
                case VriFormat_RG8_SNORM:
                case VriFormat_RG8_UINT:
                case VriFormat_RG8_SINT:
                case VriFormat_R16_UNORM:
                case VriFormat_R16_SNORM:
                case VriFormat_R16_UINT:
                case VriFormat_R16_SINT:
                case VriFormat_R16_SFLOAT:
                case VriFormat_D16_UNORM:
                    return Block {1, 1, 2};
                case VriFormat_RGBA8_UNORM:
                case VriFormat_RGBA8_SRGB:
                case VriFormat_RGBA8_UINT:
                case VriFormat_RGBA8_SINT:
                case VriFormat_BGRA8_UNORM:
                case VriFormat_BGRA8_SRGB:
                case VriFormat_RG16_UNORM:
                case VriFormat_RG16_SNORM:
                case VriFormat_RG16_UINT:
                case VriFormat_RG16_SINT:
                case VriFormat_RG16_SFLOAT:
                case VriFormat_R32_UINT:
                case VriFormat_R32_SINT:
                case VriFormat_R32_SFLOAT:
                case VriFormat_RGB10A2_UNORM:
                case VriFormat_RG11B10_UFLOAT:
                case VriFormat_D32_SFLOAT:
                    return Block {1, 1, 4};
                case VriFormat_RGBA16_UNORM:
                case VriFormat_RGBA16_SNORM:
                case VriFormat_RGBA16_UINT:
                case VriFormat_RGBA16_SINT:
                case VriFormat_RGBA16_SFLOAT:
                case VriFormat_RG32_UINT:
                case VriFormat_RG32_SINT:
                case VriFormat_RG32_SFLOAT:
                    return Block {1, 1, 8};
                case VriFormat_RGB32_UINT:
                case VriFormat_RGB32_SINT:
                case VriFormat_RGB32_SFLOAT:
                    return Block {1, 1, 12};
                case VriFormat_RGBA32_UINT:
                case VriFormat_RGBA32_SINT:
                case VriFormat_RGBA32_SFLOAT:
                    return Block {1, 1, 16};
                case VriFormat_D24_UNORM_S8_UINT:
                case VriFormat_D32_SFLOAT_S8_UINT:
                    return Block {1, 1, aspect == VriImageAspect_Stencil ? 1u : 4u};
                case VriFormat_BC1_UNORM:
                case VriFormat_BC1_SRGB:
                case VriFormat_BC4_UNORM:
                    return Block {4, 4, 8};
                case VriFormat_BC2_UNORM:
                case VriFormat_BC2_SRGB:
                case VriFormat_BC3_UNORM:
                case VriFormat_BC3_SRGB:
                case VriFormat_BC5_UNORM:
                case VriFormat_BC6H_UFLOAT:
                case VriFormat_BC7_UNORM:
                case VriFormat_BC7_SRGB:
                case VriFormat_ASTC_4X4_UNORM:
                case VriFormat_ASTC_4X4_SRGB:
                    return Block {4, 4, 16};
                case VriFormat_ASTC_6X6_UNORM:
                case VriFormat_ASTC_6X6_SRGB:
                    return Block {6, 6, 16};
                default:
                    return std::unexpected("Unsupported texture readback format");
            }
        }

        VriImageAspectFlags aspects(VriFormat format)
        {
            switch (format)
            {
                case VriFormat_D16_UNORM:
                case VriFormat_D32_SFLOAT:
                    return VriImageAspect_Depth;
                case VriFormat_S8_UINT:
                    return VriImageAspect_Stencil;
                case VriFormat_D24_UNORM_S8_UINT:
                case VriFormat_D32_SFLOAT_S8_UINT:
                    return VriImageAspect_Depth | VriImageAspect_Stencil;
                default:
                    return VriImageAspect_Color;
            }
        }

        struct Layout
        {
            VriBufferTextureCopyDesc copy {};
            uint32_t                 rows;
            uint64_t                 rowBytes;
            uint64_t                 packedRowBytes;
            uint64_t                 bytes;
        };

        std::expected<Layout, std::string> layoutExpected(Device&             device,
                                                          Texture&            texture,
                                                          uint32_t            mip,
                                                          uint32_t            layer,
                                                          VriImageAspectFlags aspect,
                                                          bool                sliceZero)
        {
            const auto& desc = texture.desc;
            if (!texture.belongsTo(device) || !texture.handle || !(desc.usage & VriTextureUsage_TransferSrc) ||
                desc.sampleNum != 1 || mip >= desc.mipNum || mip >= 32 || layer >= desc.layerNum)
            {
                return std::unexpected(
                    "Readback requires its device's initialized, single-sample TransferSrc subresource");
            }
            const auto available = aspects(desc.format);
            if (!aspect)
            {
                aspect = available;
            }
            if ((aspect != VriImageAspect_Color && aspect != VriImageAspect_Depth &&
                 aspect != VriImageAspect_Stencil) ||
                (aspect & available) != aspect)
            {
                return std::unexpected("Readback requires one valid color, depth or stencil plane");
            }
            auto state = texture.stateExpected(mip, layer, aspect);
            if (!state || state->layout == VriLayout_Undefined)
            {
                return std::unexpected("Readback subresource is not initialized");
            }
            auto block = formatBlockExpected(desc.format, aspect);
            if (!block)
            {
                return std::unexpected(block.error());
            }
            const auto* caps = device.core.GetDeviceDesc(device.handle);
            if (!caps)
            {
                return std::unexpected("Readback device description is unavailable");
            }
            Layout result;
            auto&  region          = result.copy.texture;
            region.mip             = mip;
            region.baseLayer       = layer;
            region.layerNum        = 1;
            region.aspect          = aspect;
            region.width           = std::max(desc.width >> mip, 1u);
            region.height          = std::max(desc.height >> mip, 1u);
            region.depth           = sliceZero ? 1u : std::max(desc.depth >> mip, 1u);
            const uint64_t columns = (uint64_t(region.width) + block->width - 1) / block->width;
            result.rows            = uint32_t((uint64_t(region.height) + block->height - 1) / block->height);
            result.packedRowBytes  = columns * block->bytes;
            const auto alignment =
                std::lcm(uint64_t(block->bytes), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
            if (result.packedRowBytes > UINT64_MAX - alignment + 1)
            {
                return std::unexpected("Readback row alignment overflows");
            }
            result.rowBytes          = (result.packedRowBytes + alignment - 1) / alignment * alignment;
            const uint64_t rowTexels = result.rowBytes / block->bytes * block->width;
            const uint64_t imageRows = uint64_t(result.rows) * block->height;
            if (rowTexels > UINT32_MAX || imageRows > UINT32_MAX ||
                result.rowBytes > UINT64_MAX / result.rows / region.depth ||
                result.rowBytes > SIZE_MAX / result.rows / region.depth ||
                result.packedRowBytes > std::vector<std::byte>().max_size() / result.rows / region.depth)
            {
                return std::unexpected("Readback footprint exceeds native or host size limits");
            }
            result.copy.bufferRowLength   = uint32_t(rowTexels);
            result.copy.bufferImageHeight = uint32_t(imageRows);
            result.bytes                  = result.rowBytes * result.rows * region.depth;
            return result;
        }

        std::expected<uint32_t, std::string> imageTexelBytesExpected(VriFormat format)
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
                    return std::unexpected("Unsupported float-image capture format; use native texture readback data");
            }
        }

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

        std::expected<Image, std::string> imageExpected(const TextureReadbackData& data)
        {
            auto texelBytes = imageTexelBytesExpected(data.format);
            if (!texelBytes)
            {
                return std::unexpected(texelBytes.error());
            }
            const auto&    region = data.region;
            const uint64_t pixels = uint64_t(region.width) * region.height;
            if (region.layerNum != 1 || pixels > std::vector<float>().max_size() / 4 ||
                pixels > data.bytes.size() / *texelBytes)
            {
                return std::unexpected("Float-image capture has an invalid data footprint");
            }
            Image image {{region.width, region.height}, std::vector<float>(size_t(pixels) * 4)};
            if (data.format == VriFormat_RGBA32_SFLOAT)
            {
                std::memcpy(image.rgba.data(), data.bytes.data(), size_t(pixels) * 16);
                return image;
            }
            const auto* source = reinterpret_cast<const unsigned char*>(data.bytes.data());
            const bool  depth  = data.format == VriFormat_D32_SFLOAT;
            const bool  bgra   = data.format == VriFormat_BGRA8_UNORM || data.format == VriFormat_BGRA8_SRGB;
            for (size_t i = 0; i < image.rgba.size(); ++i)
            {
                if (depth)
                {
                    float value;
                    std::memcpy(&value, source + (i / 4) * 4, 4);
                    image.rgba[i] = i % 4 == 3 ? 1 : value;
                }
                else if (*texelBytes == 8)
                {
                    uint16_t value;
                    std::memcpy(&value, source + i * 2, 2);
                    image.rgba[i] = half(value);
                }
                else
                {
                    const auto channel = i % 4;
                    const auto index   = bgra && (channel == 0 || channel == 2) ? i - channel + (2 - channel) : i;
                    image.rgba[i]      = float(source[index]) / 255;
                }
            }
            return image;
        }
    } // namespace

    TextureReadback::TextureReadback(Device&                  device,
                                     VriFormat                format,
                                     VriBufferTextureCopyDesc copy,
                                     uint32_t                 rows,
                                     uint64_t                 rowBytes,
                                     uint64_t                 packedRowBytes,
                                     bool                     sliceZero,
                                     std::unique_ptr<Buffer>  staging) :
        m_Device(device),
        m_Format(format),
        m_Copy(copy),
        m_Rows(rows),
        m_RowBytes(rowBytes),
        m_PackedRowBytes(packedRowBytes),
        m_SliceZero(sliceZero),
        m_Staging(std::move(staging))
    {
    }

    TextureReadback::~TextureReadback() = default;

    std::expected<std::unique_ptr<TextureReadback>, std::string>
    TextureReadback::create(Device& device, Texture& texture, uint32_t mip, uint32_t layer, VriImageAspectFlags aspect)
    {
        return createSelectionExpected(device, texture, mip, layer, aspect, false);
    }

    std::expected<std::unique_ptr<TextureReadback>, std::string>
    TextureReadback::createSelectionExpected(Device&             device,
                                             Texture&            texture,
                                             uint32_t            mip,
                                             uint32_t            layer,
                                             VriImageAspectFlags aspect,
                                             bool                sliceZero)
    {
        auto layout = layoutExpected(device, texture, mip, layer, aspect, sliceZero);
        if (!layout)
        {
            return std::unexpected(layout.error());
        }
        auto staging =
            Buffer::create(device, {layout->bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!staging)
        {
            return std::unexpected(staging.error());
        }
        return std::unique_ptr<TextureReadback>(new TextureReadback(device,
                                                                    texture.desc.format,
                                                                    layout->copy,
                                                                    layout->rows,
                                                                    layout->rowBytes,
                                                                    layout->packedRowBytes,
                                                                    sliceZero,
                                                                    std::move(*staging)));
    }

    std::expected<std::unique_ptr<TextureReadback>, std::string>
    TextureReadback::start(Device&                             device,
                           Texture&                            texture,
                           uint32_t                            mip,
                           uint32_t                            layer,
                           VriImageAspectFlags                 aspect,
                           std::span<const VriFenceSubmitDesc> waits,
                           std::span<const VriFenceSubmitDesc> signals)
    {
        auto result = create(device, texture, mip, layer, aspect);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        (*result)->m_Frame = std::move(*frame);
        auto commands      = (*result)->m_Frame->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = (*result)->recordIntoExpected(*commands, texture); !recorded)
        {
            return std::unexpected(recorded.error());
        }
        if (auto submitted = (*result)->m_Frame->submitExpected(waits, signals); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        return result;
    }

    std::expected<void, std::string> TextureReadback::recordExpected(VriCommandBuffer* commands, Texture& texture)
    {
        if (m_Frame)
        {
            return std::unexpected("An asynchronous readback owns its recording and cannot be re-recorded");
        }
        return recordIntoExpected(commands, texture);
    }

    std::expected<void, std::string> TextureReadback::recordIntoExpected(VriCommandBuffer* commands, Texture& texture)
    {
        auto layout = layoutExpected(m_Device,
                                     texture,
                                     m_Copy.texture.mip,
                                     m_Copy.texture.baseLayer,
                                     m_Copy.texture.aspect,
                                     m_SliceZero);
        if (!layout)
        {
            return std::unexpected(layout.error());
        }
        if (!commands || texture.desc.format != m_Format || layout->rowBytes != m_RowBytes ||
            layout->packedRowBytes != m_PackedRowBytes || layout->rows != m_Rows ||
            layout->copy.texture.width != m_Copy.texture.width ||
            layout->copy.texture.height != m_Copy.texture.height || layout->copy.texture.depth != m_Copy.texture.depth)
        {
            return std::unexpected("Readback command buffer is null or source format/extent changed");
        }
        const auto previous =
            texture.stateExpected(m_Copy.texture.mip, m_Copy.texture.baseLayer, m_Copy.texture.aspect);
        if (!previous)
        {
            return std::unexpected(previous.error());
        }
        VriTextureViewDesc selected {};
        selected.viewType =
            (texture.desc.type == VriTextureType_Cube || texture.desc.type == VriTextureType_CubeArray) ?
                VriTextureViewType_2DArray :
                VriTextureViewType_MaxEnum;
        selected.baseMip   = m_Copy.texture.mip;
        selected.mipNum    = 1;
        selected.baseLayer = m_Copy.texture.baseLayer;
        selected.layerNum  = 1;
        selected.aspect    = m_Copy.texture.aspect;
        auto state         = texture.captureState();
        if (auto transitioned =
                texture.transitionExpected(commands,
                                           {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer},
                                           selected);
            !transitioned)
        {
            return std::unexpected(transitioned.error());
        }
        m_Staging->transition(commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        m_Device.core.CmdReadbackTextureToBuffer(commands, m_Staging->handle, texture.handle, &m_Copy);
        if (auto restored = texture.transitionExpected(commands, *previous, selected); !restored)
        {
            auto reset = texture.restoreUnsubmittedExpected(std::move(state));
            return std::unexpected(restored.error() + (reset ? "" : "; restore tracker: " + reset.error()));
        }
        m_Recorded = true;
        return {};
    }

    std::expected<void, std::string> TextureReadback::waitExpected(uint64_t timeoutNs)
    {
        if (!m_Frame)
        {
            return std::unexpected("Externally recorded readback completion belongs to its caller");
        }
        return m_Frame->waitExpected(timeoutNs);
    }

    std::expected<TextureReadbackData, std::string> TextureReadback::dataExpected(uint64_t timeoutNs)
    {
        if (!m_Recorded)
        {
            return std::unexpected("Texture readback has not been recorded");
        }
        if (m_Frame)
        {
            if (auto completed = m_Frame->waitExpected(timeoutNs); !completed)
            {
                return std::unexpected(completed.error());
            }
        }
        TextureReadbackData result {m_Format,
                                    m_Copy.texture,
                                    std::vector<std::byte>(size_t(m_PackedRowBytes * m_Rows * m_Copy.texture.depth))};
        const auto*         source =
            static_cast<const std::byte*>(m_Device.core.MapBuffer(m_Staging->handle, 0, m_Staging->desc.size));
        if (!source)
        {
            return std::unexpected("Map texture readback buffer failed");
        }
        for (uint32_t z = 0; z < m_Copy.texture.depth; ++z)
        {
            for (uint32_t row = 0; row < m_Rows; ++row)
            {
                const auto index = uint64_t(z) * m_Rows + row;
                std::memcpy(result.bytes.data() + size_t(index * m_PackedRowBytes),
                            source + size_t(index * m_RowBytes),
                            size_t(m_PackedRowBytes));
            }
        }
        m_Device.core.UnmapBuffer(m_Staging->handle);
        return result;
    }

    ImageReadback::ImageReadback(std::unique_ptr<TextureReadback> readback) :
        m_Readback(std::move(readback))
    {
    }

    std::expected<std::unique_ptr<ImageReadback>, std::string>
    ImageReadback::create(Device& device, Texture& texture, uint32_t mip)
    {
        if (texture.desc.layerNum != 1)
        {
            return std::unexpected("Float-image capture requires one texture layer");
        }
        if (auto format = imageTexelBytesExpected(texture.desc.format); !format)
        {
            return std::unexpected(format.error());
        }
        auto readback = TextureReadback::createSelectionExpected(device, texture, mip, 0, VriImageAspect_None, true);
        if (!readback)
        {
            return std::unexpected(readback.error());
        }
        return std::unique_ptr<ImageReadback>(new ImageReadback(std::move(*readback)));
    }

    ImageReadback::ImageReadback(Device& device, Texture& texture, uint32_t mip)
    {
        auto created = create(device, texture, mip);
        if (!created)
        {
            throw std::invalid_argument(created.error()); // Legacy boundary.
        }
        m_Readback = std::move((*created)->m_Readback);
    }

    std::expected<void, std::string> ImageReadback::recordExpected(VriCommandBuffer* commands, Texture& texture)
    {
        if (m_Recorded)
        {
            return std::unexpected("Consume or discard the previous image recording first");
        }
        if (auto recorded = m_Readback->recordExpected(commands, texture); !recorded)
        {
            return recorded;
        }
        m_Recorded = true;
        return {};
    }

    std::expected<Image, std::string> ImageReadback::consumeExpected()
    {
        if (!m_Recorded)
        {
            return std::unexpected("Image readback has not been recorded");
        }
        auto data = m_Readback->dataExpected();
        if (!data)
        {
            return std::unexpected(data.error());
        }
        auto image = imageExpected(*data);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        m_Recorded = false;
        return image;
    }

    void ImageReadback::discardUnsubmitted()
    {
        m_Recorded = false;
    }

    void ImageReadback::record(VriCommandBuffer* commands, Texture& texture)
    {
        if (auto recorded = recordExpected(commands, texture); !recorded)
        {
            throw std::invalid_argument(recorded.error()); // Legacy boundary.
        }
    }

    Image ImageReadback::consume()
    {
        auto image = consumeExpected();
        if (!image)
        {
            throw std::runtime_error(image.error()); // Legacy boundary.
        }
        return std::move(*image);
    }

    std::expected<Image, std::string> readbackExpected(Device& device, Texture& texture, uint32_t mip)
    {
        auto capture = ImageReadback::create(device, texture, mip);
        if (!capture)
        {
            return std::unexpected(capture.error());
        }
        // Destroy the frame first: any accepted copy must complete before releasing image staging.
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = (*capture)->recordExpected(*commands, texture); !recorded)
        {
            return std::unexpected(recorded.error());
        }
        if (auto submitted = (*frame)->submitExpected(); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        if (auto completed = (*frame)->waitExpected(); !completed)
        {
            return std::unexpected(completed.error());
        }
        return (*capture)->consumeExpected();
    }

    Image readback(Device& device, Texture& texture, uint32_t mip)
    {
        auto image = readbackExpected(device, texture, mip);
        if (!image)
        {
            throw std::runtime_error(image.error()); // Legacy boundary.
        }
        return std::move(*image);
    }
} // namespace vultra
