#include <vultra/drivers/rhi/resources.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace vultra;

    struct Format
    {
        const char*            name;
        VriFormat              format;
        VriClearColor          value;
        std::vector<std::byte> pixel;
    };

    template<class T, size_t N>
    Format format(const char* name, VriFormat native, VriClearColor value, const std::array<T, N>& pixel)
    {
        const auto bytes = std::as_bytes(std::span(pixel));
        return {name, native, value, {bytes.begin(), bytes.end()}};
    }

    std::vector<Format> formats()
    {
        const VriClearColor normalized {{1, 0, 0, 1}};
        const VriClearColor floating {{0.25f, -0.5f, 0.75f, 1}};
        const VriClearColor fractional {{0.25f, 0.5f, 0.75f, 1}};
        VriClearColor       unsignedValue {};
        VriClearColor       signedValue {};
        for (size_t i = 0; i < 4; ++i)
        {
            unsignedValue.u32[i] = std::array<uint32_t, 4> {17, 59, 101, 227}[i];
            signedValue.i32[i]   = std::array<int32_t, 4> {-17, 59, -101, 127}[i];
        }
        std::vector<Format> result {
            format("R8_UNORM", VriFormat_R8_UNORM, normalized, std::array<uint8_t, 1> {255}),
            format("R8_SNORM", VriFormat_R8_SNORM, normalized, std::array<int8_t, 1> {127}),
            format("RG8_UNORM", VriFormat_RG8_UNORM, normalized, std::array<uint8_t, 2> {255, 0}),
            format("RG8_SNORM", VriFormat_RG8_SNORM, normalized, std::array<int8_t, 2> {127, 0}),
            format("RGBA8_UNORM", VriFormat_RGBA8_UNORM, normalized, std::array<uint8_t, 4> {255, 0, 0, 255}),
            format("RGBA8_SRGB", VriFormat_RGBA8_SRGB, normalized, std::array<uint8_t, 4> {255, 0, 0, 255}),
            format("BGRA8_UNORM", VriFormat_BGRA8_UNORM, normalized, std::array<uint8_t, 4> {0, 0, 255, 255}),
            format("BGRA8_SRGB", VriFormat_BGRA8_SRGB, normalized, std::array<uint8_t, 4> {0, 0, 255, 255}),
            format("R8_UINT", VriFormat_R8_UINT, unsignedValue, std::array<uint8_t, 1> {17}),
            format("RG8_UINT", VriFormat_RG8_UINT, unsignedValue, std::array<uint8_t, 2> {17, 59}),
            format("RGBA8_UINT", VriFormat_RGBA8_UINT, unsignedValue, std::array<uint8_t, 4> {17, 59, 101, 227}),
            format("R8_SINT", VriFormat_R8_SINT, signedValue, std::array<int8_t, 1> {-17}),
            format("RG8_SINT", VriFormat_RG8_SINT, signedValue, std::array<int8_t, 2> {-17, 59}),
            format("RGBA8_SINT", VriFormat_RGBA8_SINT, signedValue, std::array<int8_t, 4> {-17, 59, -101, 127}),
            format("R16_UNORM", VriFormat_R16_UNORM, normalized, std::array<uint16_t, 1> {65535}),
            format("R16_SNORM", VriFormat_R16_SNORM, normalized, std::array<int16_t, 1> {32767}),
            format("RG16_UNORM", VriFormat_RG16_UNORM, normalized, std::array<uint16_t, 2> {65535, 0}),
            format("RG16_SNORM", VriFormat_RG16_SNORM, normalized, std::array<int16_t, 2> {32767, 0}),
            format("RGBA16_UNORM", VriFormat_RGBA16_UNORM, normalized, std::array<uint16_t, 4> {65535, 0, 0, 65535}),
            format("RGBA16_SNORM", VriFormat_RGBA16_SNORM, normalized, std::array<int16_t, 4> {32767, 0, 0, 32767}),
            format("R16_SFLOAT", VriFormat_R16_SFLOAT, floating, std::array<uint16_t, 1> {0x3400}),
            format("RG16_SFLOAT", VriFormat_RG16_SFLOAT, floating, std::array<uint16_t, 2> {0x3400, 0xb800}),
            format("RGBA16_SFLOAT",
                   VriFormat_RGBA16_SFLOAT,
                   floating,
                   std::array<uint16_t, 4> {0x3400, 0xb800, 0x3a00, 0x3c00}),
            format("R16_UINT", VriFormat_R16_UINT, unsignedValue, std::array<uint16_t, 1> {17}),
            format("RG16_UINT", VriFormat_RG16_UINT, unsignedValue, std::array<uint16_t, 2> {17, 59}),
            format("RGBA16_UINT", VriFormat_RGBA16_UINT, unsignedValue, std::array<uint16_t, 4> {17, 59, 101, 227}),
            format("R16_SINT", VriFormat_R16_SINT, signedValue, std::array<int16_t, 1> {-17}),
            format("RG16_SINT", VriFormat_RG16_SINT, signedValue, std::array<int16_t, 2> {-17, 59}),
            format("RGBA16_SINT", VriFormat_RGBA16_SINT, signedValue, std::array<int16_t, 4> {-17, 59, -101, 127}),
            format("R32_SFLOAT", VriFormat_R32_SFLOAT, floating, std::array<float, 1> {0.25f}),
            format("RG32_SFLOAT", VriFormat_RG32_SFLOAT, floating, std::array<float, 2> {0.25f, -0.5f}),
            format("RGBA32_SFLOAT", VriFormat_RGBA32_SFLOAT, floating, std::array<float, 4> {0.25f, -0.5f, 0.75f, 1}),
            format("RGB10A2_UNORM", VriFormat_RGB10A2_UNORM, normalized, std::array<uint32_t, 1> {0xc00003ff}),
            format("RG11B10_UFLOAT", VriFormat_RG11B10_UFLOAT, normalized, std::array<uint32_t, 1> {0x3c0}),
            format("RGBA8_UNORM_FRACTIONAL",
                   VriFormat_RGBA8_UNORM,
                   fractional,
                   std::array<uint8_t, 4> {64, 128, 191, 255}),
            format("RGBA8_SRGB_FRACTIONAL",
                   VriFormat_RGBA8_SRGB,
                   fractional,
                   std::array<uint8_t, 4> {137, 188, 225, 255}),
            format("BGRA8_SRGB_FRACTIONAL",
                   VriFormat_BGRA8_SRGB,
                   fractional,
                   std::array<uint8_t, 4> {225, 188, 137, 255}),
            format("RGBA16_SNORM_FRACTIONAL",
                   VriFormat_RGBA16_SNORM,
                   floating,
                   std::array<int16_t, 4> {8192, -16384, 24575, 32767}),
        };
        const std::array<uint32_t, 4> wideUnsigned {0x7fffffffu, 0x80000001u, 0x9abcdef0u, 0xfffffffeu};
        const std::array<int32_t, 4>  wideSigned {std::numeric_limits<int32_t>::min() + 7,
                                                 std::numeric_limits<int32_t>::max() - 4,
                                                 -12345,
                                                 23456};
        std::memcpy(unsignedValue.u32, wideUnsigned.data(), sizeof(wideUnsigned));
        std::memcpy(signedValue.i32, wideSigned.data(), sizeof(wideSigned));
        result.push_back(format("R32_UINT", VriFormat_R32_UINT, unsignedValue, std::array {wideUnsigned[0]}));
        result.push_back(
            format("RG32_UINT", VriFormat_RG32_UINT, unsignedValue, std::array {wideUnsigned[0], wideUnsigned[1]}));
        result.push_back(format("RGBA32_UINT", VriFormat_RGBA32_UINT, unsignedValue, wideUnsigned));
        result.push_back(format("R32_SINT", VriFormat_R32_SINT, signedValue, std::array {wideSigned[0]}));
        result.push_back(
            format("RG32_SINT", VriFormat_RG32_SINT, signedValue, std::array {wideSigned[0], wideSigned[1]}));
        result.push_back(format("RGBA32_SINT", VriFormat_RGBA32_SINT, signedValue, wideSigned));
        return result;
    }

    struct Shape
    {
        const char*    name;
        VriTextureType type;
        uint32_t       width;
        uint32_t       height;
        uint32_t       depth;
        uint32_t       layers;
        uint32_t       mips;
    };

    constexpr std::array shapes {
        Shape {"1D", VriTextureType_1D, 17, 1, 1, 1, 1},
        Shape {"1DArray", VriTextureType_1DArray, 17, 1, 1, 3, 1},
        Shape {"2D", VriTextureType_2D, 17, 9, 1, 1, 3},
        Shape {"2DArray", VriTextureType_2DArray, 17, 9, 1, 3, 3},
        Shape {"3D", VriTextureType_3D, 17, 9, 3, 1, 3},
        Shape {"Cube", VriTextureType_Cube, 8, 8, 1, 6, 3},
        Shape {"CubeArray", VriTextureType_CubeArray, 8, 8, 1, 12, 3},
    };

    struct Region
    {
        VriBufferTextureCopyDesc copy;
        uint32_t                 rowBytes;
    };

    std::expected<void, std::string> clear(Device& device, const Format& format, const Shape& shape)
    {
        VriTextureDesc desc {};
        desc.type           = shape.type;
        desc.format         = format.format;
        desc.width          = shape.width;
        desc.height         = shape.height;
        desc.depth          = shape.depth;
        desc.layerNum       = shape.layers;
        desc.mipNum         = shape.mips;
        desc.sampleNum      = 1;
        desc.memoryLocation = VriMemoryLocation_Device;
        desc.usage = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc;
        auto texture = Texture::create(device, desc);
        auto frame   = Frame::create(device);
        if (!texture || !frame)
        {
            return std::unexpected(!texture ? texture.error() : frame.error());
        }
        const auto          alignment = device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment;
        const auto          texelSize = uint32_t(format.pixel.size());
        std::vector<Region> regions;
        uint64_t            bytes = 0;
        for (uint32_t layer = 0; layer < shape.layers; ++layer)
        {
            for (uint32_t mip = 0; mip < shape.mips; ++mip)
            {
                Region region {};
                auto&  image    = region.copy.texture;
                image.mip       = mip;
                image.baseLayer = layer;
                image.layerNum  = 1;
                image.aspect    = VriImageAspect_Color;
                image.width     = std::max(shape.width >> mip, 1u);
                image.height    = std::max(shape.height >> mip, 1u);
                image.depth     = std::max(shape.depth >> mip, 1u);
                // A texel-count row length must also meet the backend's byte alignment.
                const auto rowAlignment  = std::max(256u, alignment);
                region.rowBytes          = ((image.width * texelSize + rowAlignment - 1) / rowAlignment) * rowAlignment;
                region.copy.bufferOffset = bytes;
                region.copy.bufferRowLength   = region.rowBytes / texelSize;
                region.copy.bufferImageHeight = image.height;
                bytes += uint64_t(region.rowBytes) * image.height * image.depth;
                regions.push_back(region);
            }
        }
        auto upload   = Buffer::create(device, {bytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto readback = Buffer::create(device, {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!upload || !readback)
        {
            return std::unexpected(!upload ? upload.error() : readback.error());
        }
        auto* mapped = device.core.MapBuffer((*upload)->handle, 0, bytes);
        if (!mapped)
        {
            return std::unexpected("Map storage clear initializer failed");
        }
        std::memset(mapped, 0x5a, bytes);
        device.core.UnmapBuffer((*upload)->handle);
        // Track authored contents independently for every cell: sentinel, typed value or zero.
        std::vector<uint32_t> contents(regions.size(), 0);
        for (uint32_t cycle = 0; cycle < 5; ++cycle)
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (cycle < 2)
            {
                (*texture)->transition(
                    *commands,
                    {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            }
            if (cycle == 0)
            {
                (*upload)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                for (const auto& region : regions)
                {
                    device.core.CmdUploadBufferToTexture(*commands,
                                                         (*texture)->handle,
                                                         (*upload)->handle,
                                                         &region.copy);
                }
                (*texture)->transition(
                    *commands,
                    {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            }
            const VriClearColor zero {};
            VriTextureSubresourceRange selected {0, 1, 0, shape.layers};
            if (cycle < 2)
            {
                device.core.CmdClearStorageTexture(*commands, (*texture)->handle, cycle == 0 ? &format.value : &zero);
            }
            else
            {
                selected = cycle == 4 ? VriTextureSubresourceRange {} :
                                        VriTextureSubresourceRange {shape.mips > 1 ? 1u : 0u,
                                                                    cycle == 2 ? 1u : 0u,
                                                                    shape.layers / 2,
                                                                    cycle == 2 ? 1u : 0u};
                if (cycle == 2)
                {
                    const auto       prior = (*texture)->captureState();
                    const std::array invalid {VriTextureSubresourceRange {shape.mips, 1, 0, 1},
                                              VriTextureSubresourceRange {0, UINT32_MAX, 0, 1},
                                              VriTextureSubresourceRange {0, 1, shape.layers, 1},
                                              VriTextureSubresourceRange {0, 1, 0, UINT32_MAX}};
                    for (const auto range : invalid)
                    {
                        if ((*texture)->clearStorageExpected(*commands, format.value, range) ||
                            (*texture)->captureState() != prior)
                        {
                            return std::unexpected("Invalid storage clear range changed tracked state");
                        }
                    }
                    const auto native                       = device.core.CmdClearStorageTextureRange;
                    device.core.CmdClearStorageTextureRange = nullptr;
                    auto unavailable = (*texture)->clearStorageExpected(*commands, format.value, selected);
                    device.core.CmdClearStorageTextureRange = native;
                    if (unavailable || (*texture)->captureState() != prior)
                    {
                        return std::unexpected("Unavailable storage clear changed tracked state");
                    }
                    device.core.CmdClearStorageTextureRange =
                        [](VriCommandBuffer*, VriTexture*, const VriClearColor*, const VriTextureSubresourceRange*)
                    {
                        return VriResult_Failure;
                    };
                    auto rejected = (*texture)->clearStorageExpected(*commands, format.value, selected);
                    device.core.CmdClearStorageTextureRange = native;
                    if (rejected || (*texture)->captureState() != prior)
                    {
                        return std::unexpected("Rejected storage clear lost CPU state before discard");
                    }
                    if (auto discarded = (*frame)->discardExpected(); !discarded)
                    {
                        return discarded;
                    }
                    commands = (*frame)->beginExpected();
                    if (!commands)
                    {
                        return std::unexpected(commands.error());
                    }
                }
                if (auto cleared =
                        (*texture)->clearStorageExpected(*commands, cycle == 3 ? zero : format.value, selected);
                    !cleared)
                {
                    return cleared;
                }
            }
            const auto mipEnd   = selected.mipNum ? selected.baseMip + selected.mipNum : shape.mips;
            const auto layerEnd = selected.layerNum ? selected.baseLayer + selected.layerNum : shape.layers;
            for (size_t i = 0; i < regions.size(); ++i)
            {
                const auto& cell = regions[i].copy.texture;
                if (cell.mip >= selected.baseMip && cell.mip < mipEnd && cell.baseLayer >= selected.baseLayer &&
                    cell.baseLayer < layerEnd)
                {
                    contents[i] = cycle == 1 || cycle == 3 ? 2u : 1u;
                }
            }
            (*texture)->transition(*commands,
                                   {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*readback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            for (const auto& region : regions)
            {
                device.core.CmdReadbackTextureToBuffer(*commands,
                                                       (*readback)->handle,
                                                       (*texture)->handle,
                                                       &region.copy);
            }
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            const auto* result = static_cast<const std::byte*>(device.core.MapBuffer((*readback)->handle, 0, bytes));
            if (!result)
            {
                return std::unexpected("Map storage clear result failed");
            }
            std::string error;
            for (size_t i = 0; i < regions.size(); ++i)
            {
                const auto& region = regions[i];
                const auto& image = region.copy.texture;
                for (uint32_t z = 0; z < image.depth; ++z)
                {
                    for (uint32_t y = 0; y < image.height; ++y)
                    {
                        for (uint32_t x = 0; x < image.width; ++x)
                        {
                            const auto offset = region.copy.bufferOffset +
                                                (uint64_t(z) * image.height + y) * region.rowBytes + x * texelSize;
                            for (uint32_t channel = 0; channel < texelSize; ++channel)
                            {
                                std::byte expected {0x5a};
                                if (contents[i] != 0)
                                {
                                    expected = contents[i] == 1 ? format.pixel[channel] : std::byte {0};
                                }
                                if (result[offset + channel] != expected && error.empty())
                                {
                                    error = std::string("Storage clear mismatch: ") + format.name + "/" + shape.name +
                                            " cycle=" + std::to_string(cycle) + " mip=" + std::to_string(image.mip) +
                                            " layer=" + std::to_string(image.baseLayer) + " xyz=" + std::to_string(x) +
                                            "," + std::to_string(y) + "," + std::to_string(z) +
                                            " byte=" + std::to_string(channel) +
                                            " expected=" + std::to_string(std::to_integer<uint32_t>(expected)) +
                                            " actual=" +
                                            std::to_string(std::to_integer<uint32_t>(result[offset + channel]));
                                }
                            }
                        }
                    }
                }
            }
            device.core.UnmapBuffer((*readback)->handle);
            if (!error.empty())
            {
                return std::unexpected(error);
            }
        }
        std::printf("STORAGE_CLEAR format=%s shape=%s all_layers_depth_exact=1 selected_ranges_exact=1 "
                    "other_cells_preserved=1\n",
                    format.name,
                    shape.name);
        std::fflush(stdout);
        return {};
    }

    std::expected<void, std::string> probe(Device& device)
    {
        const auto* caps       = device.core.GetDeviceDesc(device.handle);
        const auto  compressed = device.core.GetFormatSupport(device.handle, VriFormat_ASTC_4X4_UNORM);
        std::printf("STORAGE_CLEAR_DEVICE clear=%d astc_support=%u\n", int(caps->hasClearStorageTexture), compressed);
        std::fflush(stdout);
        if (!caps->hasClearStorageTexture || !caps->hasClearStorageTextureRange)
        {
            return std::unexpected("Storage texture clear capability is required");
        }
        const std::array blockFormats {VriFormat_BC1_UNORM,
                                       VriFormat_BC2_UNORM,
                                       VriFormat_BC3_UNORM,
                                       VriFormat_BC4_UNORM,
                                       VriFormat_BC5_UNORM,
                                       VriFormat_BC6H_UFLOAT,
                                       VriFormat_BC7_UNORM,
                                       VriFormat_BC1_SRGB,
                                       VriFormat_BC2_SRGB,
                                       VriFormat_BC3_SRGB,
                                       VriFormat_BC7_SRGB,
                                       VriFormat_ASTC_4X4_UNORM,
                                       VriFormat_ASTC_4X4_SRGB,
                                       VriFormat_ASTC_6X6_UNORM,
                                       VriFormat_ASTC_6X6_SRGB};
        for (const auto block : blockFormats)
        {
            const auto support = device.core.GetFormatSupport(device.handle, block);
            if (support & (VriFormatSupport_ColorAttachment | VriFormatSupport_StorageTexture | VriFormatSupport_Blend))
            {
                return std::unexpected("Compressed format advertises writable attachment/storage capability");
            }
        }
        if (device.core.GetFormatSupport(device.handle, VriFormat_RGBA32_UINT) & VriFormatSupport_Blend)
        {
            return std::unexpected("Integer color format advertises blending support");
        }
        if (caps->graphicsAPI == VriGraphicsAPI_Metal)
        {
            auto invalid  = colorTexture({8, 8}, VriFormat_ASTC_4X4_UNORM);
            invalid.usage = VriTextureUsage_ShaderResourceStorage;
            if (Texture::create(device, invalid))
            {
                return std::unexpected("Metal accepted compressed storage usage");
            }
            if (!(device.core.GetFormatSupport(device.handle, VriFormat_D24_UNORM_S8_UINT) & VriFormatSupport_Texture))
            {
                auto depth   = depthTexture({8, 8});
                depth.format = VriFormat_D24_UNORM_S8_UINT;
                if (Texture::create(device, depth))
                {
                    return std::unexpected("Metal substituted another depth format for unsupported D24S8");
                }
            }
        }
        uint32_t checked = 0;
        for (const auto& format : formats())
        {
            const bool typed = format.format == VriFormat_RGBA32_SFLOAT || format.format == VriFormat_RGBA32_UINT ||
                               format.format == VriFormat_RGBA32_SINT;
            const auto support = device.core.GetFormatSupport(device.handle, format.format);
            if (!(support & VriFormatSupport_StorageTexture))
            {
                if (typed)
                {
                    return std::unexpected(std::string("Required typed storage format unavailable: ") + format.name);
                }
                std::printf("STORAGE_CLEAR_UNSUPPORTED format=%s\n", format.name);
                continue;
            }
            for (const auto& shape : shapes)
            {
                if (!typed && shape.type != VriTextureType_2D)
                {
                    continue;
                }
                if (auto cleared = clear(device, format, shape); !cleared)
                {
                    return cleared;
                }
                ++checked;
            }
        }
        if (checked < 21)
        {
            return std::unexpected("Required typed texture clear shape coverage is missing");
        }
        std::printf("Storage texture clear checks passed: %u cases\n", checked);
        return {};
    }
} // namespace

int main()
{
    auto device = vultra::Device::create(true);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto result = probe(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
