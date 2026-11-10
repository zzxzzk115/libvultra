#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    using Pixel = std::array<float, 4>;
    static_assert(sizeof(Pixel) == 16);

    float halfValue(uint32_t bits)
    {
        const int  exponent = int(bits >> 10);
        const auto fraction = bits & 1023u;
        if (exponent == 0)
        {
            return std::ldexp(float(fraction), -24);
        }
        return std::ldexp(float(1024u + fraction), exponent - 25);
    }

    struct NativeObjects
    {
        vultra::Device&                         device;
        VriPipelineLayout*                      layout = nullptr;
        VriDescriptorPool*                      pool   = nullptr;
        std::array<VriDescriptor*, 2>           bufferViews {};
        std::unique_ptr<vultra::ShaderPipeline> pipeline;

        ~NativeObjects()
        {
            device.waitIdleForDestruction();
            pipeline.reset();
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            for (auto* view : bufferViews)
            {
                if (view)
                {
                    device.core.DestroyDescriptor(view);
                }
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }
    };

    bool sameValue(float actual, float expected)
    {
        return std::isnan(expected) ? std::isnan(actual) :
                                      std::bit_cast<uint32_t>(actual) == std::bit_cast<uint32_t>(expected);
    }

    std::expected<void, std::string> run(vultra::Device& device, const vultra::ShaderProgram& program)
    {
        using namespace vultra;
        std::vector<Pixel> inputs;
        std::vector<Pixel> expected;
        const auto         infinity = std::numeric_limits<float>::infinity();
        // Every adjacent finite positive binary16 pair has an exact binary32 midpoint.
        // The oracle chooses the even endpoint directly; it does not implement float-to-half conversion.
        for (uint32_t lowerBits = 0; lowerBits < 0x7bffu; ++lowerBits)
        {
            const float lower    = halfValue(lowerBits);
            const float upper    = halfValue(lowerBits + 1);
            const float midpoint = (lower + upper) * .5f;
            const float below    = std::nextafter(midpoint, -infinity);
            const float above    = std::nextafter(midpoint, infinity);
            const float even     = (lowerBits & 1u) == 0 ? lower : upper;
            inputs.push_back({below, midpoint, above, lower});
            expected.push_back({lower, even, upper, lower});
            inputs.push_back({-below, -midpoint, -above, -lower});
            expected.push_back({-lower, -even, -upper, -lower});
        }
        const auto tiny          = std::numeric_limits<float>::denorm_min();
        const auto nan           = std::numeric_limits<float>::quiet_NaN();
        const auto overflowBelow = std::nextafter(65520.0f, 0.0f);
        const auto overflowAbove = std::nextafter(65520.0f, infinity);
        inputs.push_back({0.0f, -0.0f, tiny, -tiny});
        expected.push_back({0.0f, -0.0f, 0.0f, -0.0f});
        inputs.push_back({overflowBelow, 65520, overflowAbove, infinity});
        expected.push_back({65504, infinity, infinity, infinity});
        inputs.push_back({-overflowBelow, -65520, -overflowAbove, -infinity});
        expected.push_back({-65504, -infinity, -infinity, -infinity});
        inputs.push_back({nan, -nan, std::ldexp(1.0f, -25), -std::ldexp(1.0f, -25)});
        expected.push_back({nan, nan, 0.0f, -0.0f});
        constexpr uint32_t width  = 256;
        const auto         height = uint32_t((inputs.size() + width - 1) / width);
        inputs.resize(size_t(width) * height, Pixel {});
        expected.resize(inputs.size(), Pixel {});
        const uint64_t bytes = inputs.size() * sizeof(Pixel);
        auto           input =
            Buffer::create(device, {bytes, sizeof(Pixel), VriBufferUsage_StorageBuffer, VriMemoryLocation_HostUpload});
        auto output  = Buffer::create(device,
                                      {bytes,
                                       sizeof(Pixel),
                                       VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc,
                                       VriMemoryLocation_Device});
        auto staging = Buffer::create(device, {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto desc    = colorTexture({width, height}, VriFormat_RGBA16_SFLOAT);
        desc.usage   = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferSrc;
        auto image   = Texture::create(device, desc);
        if (!input || !output || !staging || !image)
        {
            if (!input)
            {
                return std::unexpected(input.error());
            }
            if (!output)
            {
                return std::unexpected(output.error());
            }
            if (!staging)
            {
                return std::unexpected(staging.error());
            }
            return std::unexpected(image.error());
        }
        auto* mapped = device.core.MapBuffer((*input)->handle, 0, bytes);
        if (!mapped)
        {
            return std::unexpected("Map binary16 rounding input");
        }
        std::memcpy(mapped, inputs.data(), size_t(bytes));
        device.core.UnmapBuffer((*input)->handle);
        NativeObjects    objects {device};
        const std::array ranges {
            VriDescriptorRangeDesc {0, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Compute},
            VriDescriptorRangeDesc {1, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute},
            VriDescriptorRangeDesc {2, 1, VriDescriptorType_StorageTexture, VriShaderStage_Compute}};
        const VriDescriptorSetDesc  set {0, ranges.data(), uint32_t(ranges.size())};
        const VriPipelineLayoutDesc layout {&set, 1, nullptr, 0, VriShaderStage_Compute};
        if (auto status = device.core.CreatePipelineLayout(device.handle, &layout, &objects.layout);
            status != VriResult_Success)
        {
            return std::unexpected("Create binary16 rounding layout: " + std::to_string(status));
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum    = 1;
        pool.structuredBufferMaxNum = 1;
        pool.storageBufferMaxNum    = 1;
        pool.storageTextureMaxNum   = 1;
        if (auto status = device.core.CreateDescriptorPool(device.handle, &pool, &objects.pool);
            status != VriResult_Success)
        {
            return std::unexpected("Create binary16 rounding pool: " + std::to_string(status));
        }
        VriDescriptorSet* descriptors = nullptr;
        if (auto status = device.core.AllocateDescriptorSets(objects.pool, objects.layout, 0, &descriptors, 1);
            status != VriResult_Success)
        {
            return std::unexpected("Allocate binary16 rounding descriptors: " + std::to_string(status));
        }
        const std::array buffers {input->get(), output->get()};
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            const VriBufferViewDesc view {buffers[i]->handle, ranges[i].descriptorType, VriFormat_Unknown, 0, bytes};
            if (auto status = device.core.CreateBufferView(device.handle, &view, &objects.bufferViews[i]);
                status != VriResult_Success)
            {
                return std::unexpected("Create binary16 rounding buffer view: " + std::to_string(status));
            }
        }
        auto imageView = (*image)->viewExpected();
        if (!imageView)
        {
            return std::unexpected(imageView.error());
        }
        const std::array<const VriDescriptor*, 3>   views {objects.bufferViews[0], objects.bufferViews[1], *imageView};
        std::array<VriDescriptorRangeUpdateDesc, 3> updates {};
        for (size_t i = 0; i < updates.size(); ++i)
        {
            updates[i] = {&views[i], 1};
        }
        device.core.UpdateDescriptorRanges(descriptors, 0, uint32_t(updates.size()), updates.data());
        auto pipeline = ShaderPipeline::create(
            device,
            program,
            std::vector<ShaderEntry> {{"main", VriShaderStage_Compute}},
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                if (shaders.size() != 1)
                {
                    return std::unexpected("Binary16 rounding requires one compute entry");
                }
                const VriComputePipelineDesc desc {objects.layout, shaders.front(), device.pipelineCache};
                VriPipeline*                 result = nullptr;
                const auto                   status = device.core.CreateComputePipeline(device.handle, &desc, &result);
                if (status != VriResult_Success || !result)
                {
                    if (result)
                    {
                        device.core.DestroyPipeline(result);
                    }
                    return std::unexpected("Create binary16 rounding pipeline: " + std::to_string(status));
                }
                return result;
            });
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        objects.pipeline = std::move(*pipeline);
        auto frame       = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto changed =
                (*input)->transitionExpected(*commands, {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader});
            !changed)
        {
            return changed;
        }
        if (auto changed =
                (*output)->transitionExpected(*commands,
                                              {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            !changed)
        {
            return changed;
        }
        if (auto changed = (*image)->transitionExpected(
                *commands,
                {VriAccess_ShaderResourceStorageWrite, VriLayout_ShaderResourceStorage, VriPipelineStage_ComputeShader},
                {.viewType = VriTextureViewType_MaxEnum});
            !changed)
        {
            return changed;
        }
        device.core.CmdSetPipelineLayout(*commands, objects.layout);
        device.core.CmdSetPipeline(*commands, objects.pipeline->handle());
        device.core.CmdSetDescriptorSet(*commands, 0, descriptors);
        const VriDispatchDesc dispatch {(width + 7) / 8, (height + 7) / 8, 1};
        device.core.CmdDispatch(*commands, &dispatch);
        if (auto changed =
                (*output)->transitionExpected(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            !changed)
        {
            return changed;
        }
        if (auto changed =
                (*staging)->transitionExpected(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            !changed)
        {
            return changed;
        }
        const VriBufferCopyDesc copy {0, 0, bytes};
        device.core.CmdCopyBuffer(*commands, (*staging)->handle, (*output)->handle, &copy);
        if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
        {
            return completed;
        }
        std::vector<Pixel> floatResults(inputs.size());
        mapped = device.core.MapBuffer((*staging)->handle, 0, bytes);
        if (!mapped)
        {
            return std::unexpected("Map binary16 rounding results");
        }
        std::memcpy(floatResults.data(), mapped, size_t(bytes));
        device.core.UnmapBuffer((*staging)->handle);
        auto halfResults = readbackExpected(device, **image);
        if (!halfResults)
        {
            return std::unexpected(halfResults.error());
        }
        if (halfResults->rgba.size() != expected.size() * 4)
        {
            return std::unexpected("Binary16 rounding output dimensions changed");
        }
        for (size_t i = 0; i < expected.size(); ++i)
        {
            for (size_t channel = 0; channel < 4; ++channel)
            {
                if (!sameValue(floatResults[i][channel], expected[i][channel]) ||
                    !sameValue(halfResults->rgba[i * 4 + channel], expected[i][channel]))
                {
                    std::fprintf(stderr,
                                 "FLOAT16_ROUNDING_MISMATCH pixel=%zu channel=%zu input=0x%08x "
                                 "expected=0x%08x float=0x%08x half=0x%08x\n",
                                 i,
                                 channel,
                                 std::bit_cast<uint32_t>(inputs[i][channel]),
                                 std::bit_cast<uint32_t>(expected[i][channel]),
                                 std::bit_cast<uint32_t>(floatResults[i][channel]),
                                 std::bit_cast<uint32_t>(halfResults->rgba[i * 4 + channel]));
                    return std::unexpected("Binary16 nearest-even quantization disagrees with adjacent-value oracle");
                }
            }
        }
        std::printf("FLOAT16_ROUNDING midpoint_pairs=31743 pixels=%zu channels=%zu "
                    "fp32=passed fp16_store=passed subnormal=passed overflow=passed signed_zero=passed nan=passed\n",
                    expected.size(),
                    expected.size() * 4);
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: test-float16-rounding probe.vshaderc\n");
        return 2;
    }
    auto program = vultra::ShaderProgram::loadExpected(argv[1]);
    if (!program)
    {
        std::fprintf(stderr, "%s\n", program.error().c_str());
        return 1;
    }
    auto device = vultra::Device::create(true, nullptr);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto checked = run(**device, *program); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
