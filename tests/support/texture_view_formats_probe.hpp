#pragma once

#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace vultra::test::texture_view_formats_probe
{
    using Words = std::array<uint32_t, 4>;

    enum class Read
    {
        eFloatBits,
        eQuantized,
        eUint,
        eInt
    };

    struct FormatCase
    {
        VriFormat source;
        VriFormat view;
        Read      read;
        bool      required;
    };

    constexpr std::array entries {"readFloatBits", "readQuantized", "readUint", "readInt"};
    constexpr std::array cases {
        FormatCase {VriFormat_R8_UNORM, VriFormat_R8_UINT, Read::eUint, true},
        FormatCase {VriFormat_R16_UINT, VriFormat_R16_SFLOAT, Read::eFloatBits, true},
        FormatCase {VriFormat_R16_UINT, VriFormat_RG8_UINT, Read::eUint, false},
        FormatCase {VriFormat_RGBA8_UNORM, VriFormat_RGBA8_SRGB, Read::eQuantized, true},
        FormatCase {VriFormat_RGBA8_UNORM, VriFormat_RGBA8_UINT, Read::eUint, true},
        FormatCase {VriFormat_RGBA8_UNORM, VriFormat_RGBA8_SINT, Read::eInt, true},
        FormatCase {VriFormat_RGBA8_UNORM, VriFormat_R32_UINT, Read::eUint, false},
        FormatCase {VriFormat_RGBA8_UNORM, VriFormat_RG16_UINT, Read::eUint, false},
        FormatCase {VriFormat_BGRA8_UNORM, VriFormat_BGRA8_SRGB, Read::eQuantized, true},
        FormatCase {VriFormat_BGRA8_UNORM, VriFormat_RGBA8_UINT, Read::eUint, false},
        FormatCase {VriFormat_R32_UINT, VriFormat_R32_SFLOAT, Read::eFloatBits, true},
        FormatCase {VriFormat_R32_UINT, VriFormat_RGBA8_UINT, Read::eUint, false},
        FormatCase {VriFormat_RGBA16_UINT, VriFormat_RGBA16_SFLOAT, Read::eFloatBits, true},
        FormatCase {VriFormat_RGBA16_UINT, VriFormat_RG32_UINT, Read::eUint, false},
        FormatCase {VriFormat_RG32_UINT, VriFormat_RGBA16_UINT, Read::eUint, false},
        FormatCase {VriFormat_RGBA32_UINT, VriFormat_RGBA32_SFLOAT, Read::eFloatBits, true},
        FormatCase {VriFormat_RGBA32_UINT, VriFormat_RGBA32_SINT, Read::eInt, true},
        FormatCase {VriFormat_RGB32_UINT, VriFormat_RGB32_SFLOAT, Read::eFloatBits, false},
        FormatCase {VriFormat_BC1_UNORM, VriFormat_BC1_SRGB, Read::eQuantized, false},
        FormatCase {VriFormat_BC2_UNORM, VriFormat_BC2_SRGB, Read::eQuantized, false},
        FormatCase {VriFormat_BC3_UNORM, VriFormat_BC3_SRGB, Read::eQuantized, false},
        FormatCase {VriFormat_BC7_UNORM, VriFormat_BC7_SRGB, Read::eQuantized, false},
        FormatCase {VriFormat_ASTC_4X4_UNORM, VriFormat_ASTC_4X4_SRGB, Read::eQuantized, false},
        FormatCase {VriFormat_ASTC_6X6_UNORM, VriFormat_ASTC_6X6_SRGB, Read::eQuantized, false}};

    struct Pixel
    {
        std::array<std::byte, 16> bytes {};
        uint32_t                  size        = 0;
        uint32_t                  blockWidth  = 1;
        uint32_t                  blockHeight = 1;
        std::array<uint8_t, 4>    decoded {};

        void append(uint32_t value, uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                bytes[size++] = std::byte((value >> (8 * i)) & 255);
            }
        }
    };

    inline Pixel compressedPixel(VriFormat source, unsigned cell)
    {
        Pixel result;
        result.blockWidth  = 4;
        result.blockHeight = 4;
        // Endpoints avoid implementation-dependent low-bit expansion at the quantized sampling boundary.
        constexpr std::array<std::array<uint8_t, 3>, 6> colors {
            {{66, 65, 255}, {66, 255, 0}, {255, 65, 66}, {255, 0, 255}, {0, 65, 66}, {0, 255, 255}}};
        constexpr std::array<std::array<uint16_t, 3>, 6> endpoints {
            {{8, 16, 31}, {8, 63, 0}, {31, 16, 8}, {31, 0, 31}, {0, 16, 8}, {0, 63, 31}}};
        const auto& rgb = colors[cell];
        result.decoded  = {rgb[0], rgb[1], rgb[2], 255};
        if (source == VriFormat_BC1_UNORM || source == VriFormat_BC2_UNORM || source == VriFormat_BC3_UNORM)
        {
            if (source == VriFormat_BC2_UNORM)
            {
                result.decoded[3] = uint8_t((15 - cell) * 17);
                for (unsigned i = 0; i < 8; ++i)
                {
                    result.append(result.decoded[3], 1);
                }
            }
            else if (source == VriFormat_BC3_UNORM)
            {
                result.decoded[3] = uint8_t(240 + cell);
                result.append(result.decoded[3], 1);
                for (unsigned i = 0; i < 7; ++i)
                {
                    result.append(0, 1);
                }
            }
            const auto& endpoint = endpoints[cell];
            result.append(uint32_t(endpoint[0] << 11) | uint32_t(endpoint[1] << 5) | endpoint[2], 2);
            result.append(0, 2);
            result.append(0, 4);
            return result;
        }
        constexpr std::array<uint8_t, 3> palette {64, 128, 192};
        result.decoded = {palette[cell % 3], palette[(cell + 1) % 3], palette[(cell + 2) % 3], uint8_t(240 + cell)};
        if (source == VriFormat_BC7_UNORM)
        {
            // Synthetic blocks from the project's BC7 encoder, independently decoded by DirectXTex.
            constexpr std::array<std::array<uint8_t, 16>, 6> blocks {
                {{32, 128, 112, 232, 15, 253, 195, 195, 175, 170, 170, 170, 0, 0, 0, 0},
                 {32, 161, 63, 244, 15, 8, 199, 199, 175, 170, 170, 170, 0, 0, 0, 0},
                 {32, 208, 63, 32, 28, 250, 203, 203, 175, 170, 170, 170, 0, 0, 0, 0},
                 {32, 128, 112, 232, 15, 253, 207, 207, 175, 170, 170, 170, 0, 0, 0, 0},
                 {32, 161, 63, 244, 15, 8, 211, 211, 175, 170, 170, 170, 0, 0, 0, 0},
                 {32, 208, 63, 32, 28, 250, 215, 215, 175, 170, 170, 170, 0, 0, 0, 0}}};
            for (const auto value : blocks[cell])
            {
                result.append(value, 1);
            }
            return result;
        }
        if (source == VriFormat_ASTC_6X6_UNORM)
        {
            result.blockWidth  = 6;
            result.blockHeight = 6;
        }
        // ASTC LDR void-extent constant block; layout checked against ARM's symbolic-to-physical encoder.
        constexpr std::array<uint8_t, 8> prefix {0xfc, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        for (const auto value : prefix)
        {
            result.append(value, 1);
        }
        for (const auto value : result.decoded)
        {
            result.append(uint32_t(value) * 257, 2);
        }
        return result;
    }

    inline Pixel pixel(VriFormat source, unsigned cell)
    {
        if (source == VriFormat_BC1_UNORM || source == VriFormat_BC2_UNORM || source == VriFormat_BC3_UNORM ||
            source == VriFormat_BC7_UNORM || source == VriFormat_ASTC_4X4_UNORM || source == VriFormat_ASTC_6X6_UNORM)
        {
            return compressedPixel(source, cell);
        }
        Pixel                            result;
        constexpr std::array<uint8_t, 3> palette {64, 128, 192};
        switch (source)
        {
            case VriFormat_R8_UNORM:
                result.append(64 + cell * 7, 1);
                break;
            case VriFormat_RGBA8_UNORM:
            case VriFormat_RGBA8_UINT:
            case VriFormat_BGRA8_UNORM:
                for (unsigned c = 0; c < 3; ++c)
                {
                    result.append(palette[(cell + c) % 3], 1);
                }
                result.append(240 + cell, 1);
                break;
            case VriFormat_R16_UINT:
                result.append(0x3800 + cell * 8, 2);
                break;
            case VriFormat_RGBA16_UINT:
                for (const uint32_t value : {0x3800u, 0x3400u, 0x3a00u})
                {
                    result.append(value + cell * 8, 2);
                }
                result.append(0x3c00, 2);
                break;
            default: {
                const std::array values {0.5f + float(cell) / 256,
                                         0.25f + float(cell) / 512,
                                         0.75f + float(cell) / 256,
                                         1.0f};
                unsigned         count = 4;
                if (source == VriFormat_R32_UINT)
                {
                    count = 1;
                }
                else if (source == VriFormat_RG32_UINT)
                {
                    count = 2;
                }
                else if (source == VriFormat_RGB32_UINT)
                {
                    count = 3;
                }
                for (unsigned c = 0; c < count; ++c)
                {
                    result.append(std::bit_cast<uint32_t>(values[c]), 4);
                }
                break;
            }
        }
        return result;
    }

    struct Shape
    {
        uint32_t components;
        uint32_t bytes;
        bool     floating   = false;
        bool     normalized = false;
        bool     srgb       = false;
        bool     bgra       = false;
    };

    inline std::expected<Shape, std::string> shape(VriFormat format)
    {
        switch (format)
        {
            case VriFormat_R8_UNORM:
                return Shape {1, 1, false, true};
            case VriFormat_R8_UINT:
                return Shape {1, 1};
            case VriFormat_RG8_UINT:
                return Shape {2, 1};
            case VriFormat_RGBA8_UNORM:
            case VriFormat_BC1_UNORM:
            case VriFormat_BC2_UNORM:
            case VriFormat_BC3_UNORM:
            case VriFormat_BC7_UNORM:
            case VriFormat_ASTC_4X4_UNORM:
            case VriFormat_ASTC_6X6_UNORM:
                return Shape {4, 1, false, true};
            case VriFormat_RGBA8_SRGB:
            case VriFormat_BC1_SRGB:
            case VriFormat_BC2_SRGB:
            case VriFormat_BC3_SRGB:
            case VriFormat_BC7_SRGB:
            case VriFormat_ASTC_4X4_SRGB:
            case VriFormat_ASTC_6X6_SRGB:
                return Shape {4, 1, false, true, true};
            case VriFormat_BGRA8_UNORM:
                return Shape {4, 1, false, true, false, true};
            case VriFormat_BGRA8_SRGB:
                return Shape {4, 1, false, true, true, true};
            case VriFormat_RGBA8_UINT:
            case VriFormat_RGBA8_SINT:
                return Shape {4, 1};
            case VriFormat_R16_UINT:
                return Shape {1, 2};
            case VriFormat_RG16_UINT:
                return Shape {2, 2};
            case VriFormat_RGBA16_UINT:
                return Shape {4, 2};
            case VriFormat_R16_SFLOAT:
                return Shape {1, 2, true};
            case VriFormat_RGBA16_SFLOAT:
                return Shape {4, 2, true};
            case VriFormat_R32_UINT:
                return Shape {1, 4};
            case VriFormat_RG32_UINT:
                return Shape {2, 4};
            case VriFormat_RGB32_UINT:
                return Shape {3, 4};
            case VriFormat_RGBA32_UINT:
            case VriFormat_RGBA32_SINT:
                return Shape {4, 4};
            case VriFormat_R32_SFLOAT:
                return Shape {1, 4, true};
            case VriFormat_RGB32_SFLOAT:
                return Shape {3, 4, true};
            case VriFormat_RGBA32_SFLOAT:
                return Shape {4, 4, true};
            default:
                return std::unexpected("No independent pixel oracle for this probe view format");
        }
    }

    inline std::expected<Words, std::string> expected(const Pixel& source, VriFormat view, Read read)
    {
        auto layout = shape(view);
        if (!layout)
        {
            return std::unexpected(layout.error());
        }
        Words result {0, 0, 0, 1};
        if (read == Read::eFloatBits)
        {
            result[3] = std::bit_cast<uint32_t>(1.0f);
        }
        else if (read == Read::eQuantized)
        {
            result[3] = 255;
        }
        for (uint32_t c = 0; c < layout->components; ++c)
        {
            const uint32_t channel = layout->bgra && c < 3 ? 2 - c : c;
            uint32_t       value   = 0;
            if (source.blockWidth > 1)
            {
                value = source.decoded[channel];
            }
            else
            {
                for (uint32_t byte = 0; byte < layout->bytes; ++byte)
                {
                    value |= uint32_t(source.bytes[channel * layout->bytes + byte]) << (8 * byte);
                }
            }
            if (read == Read::eInt && layout->bytes < 4 && (value & (1u << (8 * layout->bytes - 1))))
            {
                value |= UINT32_MAX << (8 * layout->bytes);
            }
            if (read == Read::eUint || read == Read::eInt)
            {
                result[c] = value;
                continue;
            }
            float number = 0;
            if (layout->normalized)
            {
                number = float(value) / 255;
            }
            else if (layout->floating)
            {
                number = layout->bytes == 2 ? glm::unpackHalf1x16(uint16_t(value)) : std::bit_cast<float>(value);
            }
            else
            {
                return std::unexpected("Floating shader selected for an integer pixel oracle");
            }
            if (layout->srgb && c < 3)
            {
                const double encoded = double(value) / 255;
                number = float(encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4));
            }
            result[c] = read == Read::eFloatBits ? std::bit_cast<uint32_t>(number) :
                                                   uint32_t(std::lround(std::clamp(number, 0.0f, 1.0f) * 255));
        }
        return result;
    }

    struct Probe
    {
        Device&                                  device;
        std::unique_ptr<Buffer>                  output;
        std::unique_ptr<Buffer>                  readback;
        std::unique_ptr<Frame>                   frame;
        VriPipelineLayout*                       layout     = nullptr;
        VriDescriptorPool*                       pool       = nullptr;
        VriDescriptorSet*                        set        = nullptr;
        VriDescriptor*                           outputView = nullptr;
        std::array<VriPipeline*, entries.size()> pipelines {};

        explicit Probe(Device& gpu) :
            device(gpu)
        {
        }

        VriFence* ready      = nullptr;
        uint64_t  readyValue = 0;

        ~Probe()
        {
            frame.reset();
            for (auto* pipeline : pipelines)
            {
                if (pipeline)
                {
                    device.core.DestroyPipeline(pipeline);
                }
            }
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            if (outputView)
            {
                device.core.DestroyDescriptor(outputView);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
            if (ready)
            {
                device.core.DestroyFence(ready);
            }
        }

        // Explicit dependencies connect upload/read queues; the CPU wait protects temporary staging owners.
        std::expected<void, std::string> submitAndWait(Frame& submission)
        {
            const VriFenceSubmitDesc before {ready, readyValue, VriPipelineStage_AllCommands};
            const VriFenceSubmitDesc after {ready, readyValue + 1, VriPipelineStage_AllCommands};
            const auto waits = readyValue ? std::span(&before, 1) : std::span<const VriFenceSubmitDesc> {};
            if (auto submitted = submission.submitExpected(waits, std::span(&after, 1)); !submitted)
            {
                return submitted;
            }
            ++readyValue;
            auto completed = submission.waitExpected();
            if (!completed)
            {
                device.waitIdleForDestruction();
            }
            return completed;
        }

        std::expected<void, std::string> initialize(const ShaderProgram& program)
        {
            const auto fenceStatus = device.core.CreateFence(device.handle, 0, &ready);
            if (fenceStatus != VriResult_Success || !ready)
            {
                return std::unexpected("Create format-view queue dependency failed");
            }
            auto values = Buffer::create(
                device,
                {1024, 16, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
            auto copied =
                Buffer::create(device, {1024, 16, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            auto commands = Frame::create(device, VriQueueType_Compute);
            if (!values || !copied || !commands)
            {
                return std::unexpected("Create format-view compute buffers/frame failed");
            }
            output   = std::move(*values);
            readback = std::move(*copied);
            frame    = std::move(*commands);
            const std::array ranges {
                VriDescriptorRangeDesc {0, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
                VriDescriptorRangeDesc {1, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
                VriDescriptorRangeDesc {2, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
                VriDescriptorRangeDesc {3, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute}};
            const VriDescriptorSetDesc descriptors {0, ranges.data(), uint32_t(ranges.size())};
            VriPipelineLayoutDesc      description {};
            description.descriptorSets   = &descriptors;
            description.descriptorSetNum = 1;
            description.shaderStages     = VriShaderStage_Compute;
            auto status                  = device.core.CreatePipelineLayout(device.handle, &description, &layout);
            if (status != VriResult_Success || !layout)
            {
                return std::unexpected("Create format-view pipeline layout: " + std::to_string(status));
            }
            VriDescriptorPoolDesc poolDesc {};
            poolDesc.descriptorSetMaxNum = 1;
            poolDesc.textureMaxNum       = 3;
            poolDesc.storageBufferMaxNum = 1;
            status                       = device.core.CreateDescriptorPool(device.handle, &poolDesc, &pool);
            if (status != VriResult_Success || !pool ||
                device.core.AllocateDescriptorSets(pool, layout, 0, &set, 1) != VriResult_Success)
            {
                return std::unexpected("Create format-view descriptor set failed");
            }
            const VriBufferViewDesc view {output->handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 1024};
            status = device.core.CreateBufferView(device.handle, &view, &outputView);
            if (status != VriResult_Success || !outputView)
            {
                return std::unexpected("Create format-view output descriptor: " + std::to_string(status));
            }
            for (size_t i = 0; i < pipelines.size(); ++i)
            {
                const std::array selected {ShaderEntry {entries[i], VriShaderStage_Compute}};
                auto             shaders = program.descriptorsExpected(selected);
                if (!shaders)
                {
                    return std::unexpected(shaders.error());
                }
                const VriComputePipelineDesc pipeline {layout, shaders->front(), device.pipelineCache};
                status = device.core.CreateComputePipeline(device.handle, &pipeline, &pipelines[i]);
                if (status != VriResult_Success || !pipelines[i])
                {
                    return std::unexpected("Create format-view compute pipeline: " + std::to_string(status));
                }
            }
            return {};
        }

        std::expected<void, std::string>
        sample(Texture& image, VriTextureViewDesc selected, VriDescriptor* view, Read read, const Words& expectedValues)
        {
            const auto width    = image.desc.width >> selected.baseMip;
            const auto height   = image.desc.height >> selected.baseMip;
            const auto count    = size_t(width) * height;
            auto       commands = frame->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            auto transitioned = image.transitionExpected(
                *commands,
                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader},
                selected);
            if (!transitioned ||
                !output->transitionExpected(*commands,
                                            {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader}))
            {
                return std::unexpected("Transition format-view compute inputs/output failed");
            }
            const std::array<const VriDescriptor*, 4>   descriptors {view, view, view, outputView};
            std::array<VriDescriptorRangeUpdateDesc, 4> updates {};
            for (size_t i = 0; i < updates.size(); ++i)
            {
                updates[i] = {&descriptors[i], 1};
            }
            device.core.UpdateDescriptorRanges(set, 0, uint32_t(updates.size()), updates.data());
            device.core.CmdSetPipelineLayout(*commands, layout);
            device.core.CmdSetPipeline(*commands, pipelines[size_t(read)]);
            device.core.CmdSetDescriptorSet(*commands, 0, set);
            const VriDispatchDesc dispatch {1, 1, 1};
            device.core.CmdDispatch(*commands, &dispatch);
            if (!output->transitionExpected(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer}) ||
                !readback->transitionExpected(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer}))
            {
                return std::unexpected("Transition format-view readback failed");
            }
            VriBufferCopyDesc copy {};
            copy.size = count * sizeof(Words);
            device.core.CmdCopyBuffer(*commands, readback->handle, output->handle, &copy);
            if (auto submitted = submitAndWait(*frame); !submitted)
            {
                return submitted;
            }
            const auto* mapped = static_cast<const Words*>(device.core.MapBuffer(readback->handle, 0, copy.size));
            if (!mapped)
            {
                return std::unexpected("Map format-view GPU output failed");
            }
            bool matches = true;
            for (size_t i = 0; i < count; ++i)
            {
                if (mapped[i] != expectedValues)
                {
                    std::fprintf(stderr,
                                 "format=%u mip=%u layer=%u pixel=%zu actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
                                 unsigned(selected.format),
                                 selected.baseMip,
                                 selected.baseLayer,
                                 i,
                                 mapped[i][0],
                                 mapped[i][1],
                                 mapped[i][2],
                                 mapped[i][3],
                                 expectedValues[0],
                                 expectedValues[1],
                                 expectedValues[2],
                                 expectedValues[3]);
                    matches = false;
                    break;
                }
            }
            device.core.UnmapBuffer(readback->handle);
            if (!matches)
            {
                return std::unexpected("Native format-view GPU interpretation differs from the byte oracle");
            }
            return {};
        }
    };

    inline VriTextureViewDesc selection(uint32_t mip, uint32_t layer, VriFormat format)
    {
        VriTextureViewDesc result {};
        result.viewType  = VriTextureViewType_2D;
        result.format    = format;
        result.baseMip   = mip;
        result.mipNum    = 1;
        result.baseLayer = layer;
        result.layerNum  = 1;
        return result;
    }

    inline std::expected<void, std::string>
    upload(Probe& reader, Texture& image, uint32_t mip, uint32_t layer, const Pixel& value)
    {
        auto&          device = reader.device;
        const auto     width  = image.desc.width >> mip;
        const auto     height = image.desc.height >> mip;
        const auto*    limits = device.core.GetDeviceDesc(device.handle);
        const uint64_t alignment =
            std::lcm(uint64_t(value.size), uint64_t(std::max(limits->uploadBufferTextureRowAlignment, 1u)));
        const auto     columns = (width + value.blockWidth - 1) / value.blockWidth;
        const auto     rows    = (height + value.blockHeight - 1) / value.blockHeight;
        const uint64_t row     = ((uint64_t(columns) * value.size + alignment - 1) / alignment) * alignment;
        auto           staging =
            Buffer::create(device, {row * rows, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto frame = Frame::create(device);
        if (!staging || !frame)
        {
            return std::unexpected("Create format-view upload resources failed");
        }
        auto* mapped = static_cast<std::byte*>(device.core.MapBuffer((*staging)->handle, 0, (*staging)->desc.size));
        if (!mapped)
        {
            return std::unexpected("Map format-view upload failed");
        }
        std::fill_n(mapped, size_t((*staging)->desc.size), std::byte {});
        for (uint32_t y = 0; y < rows; ++y)
        {
            for (uint32_t x = 0; x < columns; ++x)
            {
                std::copy_n(value.bytes.data(), value.size, mapped + size_t(y * row + x * value.size));
            }
        }
        device.core.UnmapBuffer((*staging)->handle);
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (!(*staging)->transitionExpected(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer}) ||
            !image.transitionExpected(
                *commands,
                {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer},
                selection(mip, layer, image.desc.format)))
        {
            return std::unexpected("Transition format-view upload failed");
        }
        VriBufferTextureCopyDesc region {};
        region.bufferRowLength   = uint32_t(row / value.size) * value.blockWidth;
        region.bufferImageHeight = rows * value.blockHeight;
        region.texture.mip       = mip;
        region.texture.baseLayer = layer;
        region.texture.layerNum  = 1;
        region.texture.aspect    = VriImageAspect_Color;
        region.texture.width     = width;
        region.texture.height    = height;
        region.texture.depth     = 1;
        device.core.CmdUploadBufferToTexture(*commands, image.handle, (*staging)->handle, &region);
        return reader.submitAndWait(**frame);
    }

    inline std::expected<void, std::string> placedFormats(Probe& reader)
    {
        auto&                         device = reader.device;
        const std::array              formats {VriFormat_RGBA8_UNORM, VriFormat_RGBA8_UINT};
        std::array<VriTextureDesc, 2> descriptions {};
        std::array<VriMemoryDesc, 2>  requirements {};
        for (size_t i = 0; i < formats.size(); ++i)
        {
            auto& desc          = descriptions[i];
            desc                = colorTexture({4, 4}, formats[i]);
            desc.memoryLocation = VriMemoryLocation_Undefined;
            desc.usage = VriTextureUsage_ShaderResource | VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst |
                         VriTextureUsage_ConcurrentQueues | VriTextureUsage_MutableFormat;
            device.core.GetTextureMemoryDesc(device.handle, &desc, VriMemoryLocation_Device, &requirements[i]);
            const auto& required = requirements[i];
            if (!required.size || !std::has_single_bit(required.alignment) || required.requiresDedicatedAllocation)
            {
                return std::unexpected("Mutable texture has invalid or dedicated-only placement requirements");
            }
        }
        auto heapDesc           = requirements[0];
        heapDesc.size           = std::max(requirements[0].size, requirements[1].size);
        heapDesc.alignment      = std::max(requirements[0].alignment, requirements[1].alignment);
        const auto firstMask    = requirements[0].memoryTypeMask ? requirements[0].memoryTypeMask : UINT32_MAX;
        const auto secondMask   = requirements[1].memoryTypeMask ? requirements[1].memoryTypeMask : UINT32_MAX;
        heapDesc.memoryTypeMask = firstMask & secondMask;
        if (!heapDesc.memoryTypeMask)
        {
            return std::unexpected("Mutable texture aliases have no common native memory type");
        }
        VriMemory* allocated = nullptr;
        const auto status    = device.core.AllocateMemory(device.handle, &heapDesc, &allocated);
        // The heap outlives both textures and their cached native views.
        std::unique_ptr<VriMemory, decltype(VriCoreInterface::FreeMemory)> memory(allocated, device.core.FreeMemory);
        if (status != VriResult_Success || !memory)
        {
            return std::unexpected("Allocate mutable texture heap failed: " + std::to_string(status));
        }
        std::array<std::unique_ptr<Texture>, 2>      images;
        std::array<std::array<VriDescriptor*, 2>, 2> views {};
        for (size_t i = 0; i < images.size(); ++i)
        {
            auto image = Texture::create(device, descriptions[i]);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            images[i] = std::move(*image);
            if (device.core.BindTextureMemory(device.handle, images[i]->handle, memory.get(), 0) != VriResult_Success)
            {
                return std::unexpected("Bind distinct mutable textures to one heap offset failed");
            }
            for (size_t j = 0; j < formats.size(); ++j)
            {
                auto view = images[i]->viewExpected(selection(0, 0, formats[j]));
                if (!view)
                {
                    return std::unexpected(view.error());
                }
                views[i][j] = *view;
            }
        }
        if (images[0]->handle == images[1]->handle || views[0][0] == views[0][1] || views[1][0] == views[1][1])
        {
            return std::unexpected("Placed format views lost distinct resource/descriptor identity");
        }
        auto activation = Frame::create(device);
        if (!activation)
        {
            return std::unexpected(activation.error());
        }
        for (unsigned edit = 0; edit < 4; ++edit)
        {
            const auto index = edit % images.size();
            auto&      image = *images[index];
            if (edit)
            {
                auto commands = (*activation)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                if (!device.core.CmdAliasMemoryBarrier ||
                    device.core.CmdAliasMemoryBarrier(*commands) != VriResult_Success)
                {
                    return std::unexpected("Order mutable texture heap activation failed");
                }
                if (auto completed = reader.submitAndWait(**activation); !completed)
                {
                    return completed;
                }
                image.setState({});
            }
            const auto data = pixel(formats[index], edit);
            if (auto sent = upload(reader, image, 0, 0, data); !sent)
            {
                return sent;
            }
            for (size_t j = 0; j < formats.size(); ++j)
            {
                const auto read   = j ? Read::eUint : Read::eQuantized;
                auto       values = expected(data, formats[j], read);
                if (!values)
                {
                    return std::unexpected(values.error());
                }
                if (auto checked = reader.sample(image, selection(0, 0, formats[j]), views[index][j], read, *values);
                    !checked)
                {
                    return checked;
                }
            }
        }
        std::puts("TEXTURE_VIEW_FORMAT_HEAP resources=2 allocations=1 offset=0 activations=4 exact_words=512");
        return {};
    }

    inline std::expected<void, std::string> gpu(Device& device, const ShaderProgram& program)
    {
        if (!device.core.GetTextureViewFormatSupport)
        {
            return std::unexpected("Native texture-view format query is unavailable");
        }
        Probe reader(device);
        if (auto initialized = reader.initialize(program); !initialized)
        {
            return initialized;
        }
        if (auto checked = placedFormats(reader); !checked)
        {
            return checked;
        }
        uint64_t words    = 0;
        uint64_t rawBytes = 0;
        unsigned checked  = 0;
        for (const auto& item : cases)
        {
            auto desc     = colorTexture({8, 8}, item.source);
            desc.type     = VriTextureType_2DArray;
            desc.mipNum   = 3;
            desc.layerNum = 2;
            desc.usage    = VriTextureUsage_ShaderResource | VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst |
                         VriTextureUsage_ConcurrentQueues | VriTextureUsage_MutableFormat;
            if (!device.core.GetTextureViewFormatSupport(device.handle, item.source, item.view, desc.usage))
            {
                std::printf("TEXTURE_VIEW_FORMAT_UNSUPPORTED source=%u view=%u\n",
                            unsigned(item.source),
                            unsigned(item.view));
                if (item.required)
                {
                    return std::unexpected("A required native texture-view format pair is unavailable");
                }
                continue;
            }
            auto image = Texture::create(device, desc);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            for (uint32_t layer = 0; layer < 2; ++layer)
            {
                for (uint32_t mip = 0; mip < 3; ++mip)
                {
                    if (auto sent = upload(reader, **image, mip, layer, pixel(item.source, layer * 3 + mip)); !sent)
                    {
                        return sent;
                    }
                }
            }
            const auto sourceShape = shape(item.source);
            if (!sourceShape)
            {
                return std::unexpected(sourceShape.error());
            }
            const Read originalRead = sourceShape->normalized ? Read::eQuantized : Read::eUint;
            for (uint32_t layer = 0; layer < 2; ++layer)
            {
                for (uint32_t mip = 0; mip < 3; ++mip)
                {
                    const auto original  = selection(mip, layer, item.source);
                    const auto alternate = selection(mip, layer, item.view);
                    const auto before    = (*image)->captureState();
                    auto       baseView  = (*image)->viewExpected(original);
                    auto       otherView = (*image)->viewExpected(alternate);
                    if (!baseView || !otherView || *baseView == *otherView || (*image)->captureState() != before ||
                        (*image)->viewExpected(original) != *baseView ||
                        (*image)->viewExpected(alternate) != *otherView)
                    {
                        return std::unexpected("Distinct format views lost cache identity or changed tracked state");
                    }
                    for (unsigned edit = 0; edit < 2; ++edit)
                    {
                        const auto data = pixel(item.source, (layer * 3 + mip + edit) % 6);
                        if (edit)
                        {
                            if (auto sent = upload(reader, **image, mip, layer, data); !sent)
                            {
                                return sent;
                            }
                        }
                        auto first  = expected(data, item.source, originalRead);
                        auto second = expected(data, item.view, item.read);
                        if (!first || !second)
                        {
                            return std::unexpected("Create independent format-view byte oracle failed");
                        }
                        if (auto read = reader.sample(**image, original, *baseView, originalRead, *first); !read)
                        {
                            return read;
                        }
                        if (auto read = reader.sample(**image, alternate, *otherView, item.read, *second); !read)
                        {
                            return read;
                        }
                        const auto count = size_t(desc.width >> mip) * (desc.height >> mip);
                        words += count * 8;
                        auto copy = TextureReadback::start(device, **image, mip, layer);
                        if (!copy)
                        {
                            return std::unexpected(copy.error());
                        }
                        auto       raw     = (*copy)->dataExpected();
                        const auto columns = ((desc.width >> mip) + data.blockWidth - 1) / data.blockWidth;
                        const auto rows    = ((desc.height >> mip) + data.blockHeight - 1) / data.blockHeight;
                        if (!raw || raw->bytes.size() != uint64_t(columns) * rows * data.size)
                        {
                            return std::unexpected("Format-view raw storage footprint changed");
                        }
                        for (size_t i = 0; i < raw->bytes.size(); ++i)
                        {
                            if (raw->bytes[i] != data.bytes[i % data.size])
                            {
                                return std::unexpected("A format view changed the source's stored bytes");
                            }
                        }
                        rawBytes += raw->bytes.size();
                    }
                }
            }
            ++checked;
            std::printf("TEXTURE_VIEW_FORMAT_PAIR source=%u view=%u cells=6 edits=2 shared_storage=verified\n",
                        unsigned(item.source),
                        unsigned(item.view));
        }
        std::printf("TEXTURE_VIEW_FORMAT_GPU pairs=%u exact_words=%llu raw_bytes=%llu\n",
                    checked,
                    static_cast<unsigned long long>(words),
                    static_cast<unsigned long long>(rawBytes));
        return {};
    }

    struct NativeFault
    {
        Device&                    device;
        VriCoreInterface           original;
        unsigned                   textures       = 0;
        unsigned                   views          = 0;
        unsigned                   destroyedViews = 0;
        bool                       rejectQuery    = false;
        bool                       rejectView     = false;
        inline static NativeFault* s_Active       = nullptr;

        explicit NativeFault(Device& gpu) :
            device(gpu),
            original(gpu.core)
        {
            s_Active                                = this;
            device.core.GetTextureViewFormatSupport = query;
            device.core.CreateTexture               = texture;
            device.core.CreateTextureView           = view;
            device.core.DestroyDescriptor           = destroy;
        }

        ~NativeFault()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static VriBool VRI_CALL query(const VriDevice*     gpu,
                                      VriFormat            source,
                                      VriFormat            selected,
                                      VriTextureUsageFlags usage)
        {
            const auto& self = *s_Active;
            return !self.rejectQuery && self.original.GetTextureViewFormatSupport &&
                   self.original.GetTextureViewFormatSupport(gpu, source, selected, usage);
        }

        static VriResult VRI_CALL texture(VriDevice* gpu, const VriTextureDesc* desc, VriTexture** output)
        {
            auto& self = *s_Active;
            ++self.textures;
            return self.original.CreateTexture(gpu, desc, output);
        }

        static VriResult VRI_CALL view(VriDevice* gpu, const VriTextureViewDesc* desc, VriDescriptor** output)
        {
            auto& self = *s_Active;
            ++self.views;
            const auto result = self.original.CreateTextureView(gpu, desc, output);
            return self.rejectView && result == VriResult_Success ? VriResult_Failure : result;
        }

        static void VRI_CALL destroy(VriDescriptor* descriptor)
        {
            ++s_Active->destroyedViews;
            s_Active->original.DestroyDescriptor(descriptor);
        }
    };

    inline std::expected<void, std::string> boundaries(Device& device)
    {
        auto desc  = colorTexture({4, 4});
        desc.usage = VriTextureUsage_ShaderResource | VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
        auto plain = Texture::create(device, desc);
        if (!plain)
        {
            return std::unexpected(plain.error());
        }
        const auto  typed = selection(0, 0, VriFormat_RGBA8_UINT);
        NativeFault faults(device);
        if ((*plain)->viewExpected(typed) || faults.views)
        {
            return std::unexpected("An ordinary texture requested an unauthorized native format view");
        }
        desc.usage |= VriTextureUsage_MutableFormat;
        const auto query                        = device.core.GetTextureViewFormatSupport;
        device.core.GetTextureViewFormatSupport = nullptr;
        const bool missingAccepted              = bool(Texture::create(device, desc));
        device.core.GetTextureViewFormatSupport = query;
        faults.rejectQuery                      = true;
        const bool disabledAccepted             = bool(Texture::create(device, desc));
        faults.rejectQuery                      = false;
        if (missingAccepted || disabledAccepted || faults.textures)
        {
            return std::unexpected("Unsupported mutable creation allocated a native texture");
        }
        auto forged = Texture::create(device, desc, (*plain)->handle);
        if (!forged || (*forged)->viewExpected(typed))
        {
            return std::unexpected("A borrowed wrapper bypassed the native resource's creation permission");
        }
        auto image = Texture::create(device, desc);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        const auto state = (*image)->captureState();
        auto       base  = (*image)->viewExpected(selection(0, 0, desc.format));
        if (!base)
        {
            return std::unexpected(base.error());
        }
        const auto before          = faults.destroyedViews;
        faults.rejectView          = true;
        const bool partialAccepted = bool((*image)->viewExpected(typed));
        faults.rejectView          = false;
        if (partialAccepted || faults.destroyedViews != before + 1 || (*image)->captureState() != state)
        {
            return std::unexpected("Rejected native format view leaked a partial descriptor or changed state");
        }
        auto       recovered   = (*image)->viewExpected(typed);
        const auto viewsBefore = faults.views;
        if (!recovered || *recovered == *base || (*image)->viewExpected(typed) != *recovered ||
            faults.views != viewsBefore || (*image)->viewExpected(selection(0, 0, desc.format)) != *base ||
            (*image)->captureState() != state)
        {
            return std::unexpected("Native view retry/cache publication lost its accepted descriptor identity");
        }
        const auto supported = [&](VriFormat source, VriFormat selected, VriTextureUsageFlags usage)
        {
            return bool(device.core.GetTextureViewFormatSupport(device.handle, source, selected, usage));
        };
        if (supported(VriFormat_R8_UNORM, VriFormat_R32_UINT, desc.usage) ||
            supported(VriFormat_D32_SFLOAT, VriFormat_R32_SFLOAT, desc.usage) ||
            supported(VriFormat_BC1_UNORM, VriFormat_BC3_SRGB, desc.usage) ||
            supported(VriFormat_ASTC_4X4_UNORM, VriFormat_ASTC_6X6_SRGB, desc.usage) ||
            supported(VriFormat_Unknown, desc.format, desc.usage) ||
            supported(desc.format, VriFormat_Count, desc.usage) ||
            supported(desc.format, VriFormat_RGBA8_UINT, desc.usage & ~VriTextureUsage_MutableFormat) ||
            supported(desc.format, desc.format, UINT32_MAX))
        {
            return std::unexpected("Native format query accepted an incompatible class, codec, depth cast or usage");
        }
        const auto invalid      = selection(0, 0, VriFormat_R16_SFLOAT);
        auto       direct       = invalid;
        direct.texture          = (*image)->handle;
        VriDescriptor* rejected = nullptr;
        if (device.core.CreateTextureView(device.handle, &direct, &rejected) != VriResult_InvalidArgument || rejected)
        {
            return std::unexpected("Native view creation accepted an incompatible texel-bit class");
        }
        desc.memoryLocation = VriMemoryLocation_Undefined;
        auto unbound        = Texture::create(device, desc);
        if (!unbound || (*unbound)->viewExpected(typed))
        {
            return std::unexpected("A native format view accepted an unbound texture");
        }
        auto otherDevice = Device::create(true, nullptr, 0);
        if (!otherDevice)
        {
            return std::unexpected(otherDevice.error());
        }
        desc.memoryLocation = VriMemoryLocation_Device;
        auto other          = Texture::create(**otherDevice, desc);
        if (!other)
        {
            return std::unexpected(other.error());
        }
        direct         = typed;
        direct.texture = (*other)->handle;
        if (device.core.CreateTextureView(device.handle, &direct, &rejected) != VriResult_InvalidArgument || rejected)
        {
            return std::unexpected("Native format view creation accepted another device's resource");
        }
        std::puts("TEXTURE_VIEW_FORMAT_BOUNDARIES permission=checked capability=checked partial=released retry=cached "
                  "state=retained foreign=rejected");
        return {};
    }
} // namespace vultra::test::texture_view_formats_probe
