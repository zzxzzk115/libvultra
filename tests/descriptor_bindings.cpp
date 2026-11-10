#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <expected>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace vultra;
    constexpr VriAccessStage read {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader};
    constexpr VriAccessStage write {VriAccess_ShaderResourceRead | VriAccess_ShaderResourceStorageWrite,
                                    VriPipelineStage_ComputeShader};
    constexpr uint32_t       sentinel    = 0xa5a5a5a5;
    constexpr uint32_t       outputBytes = 128;

    struct Bindings
    {
        Device&                         device;
        uint64_t                        viewAlignment = 16;
        VriPipelineLayout*              layout        = nullptr;
        VriDescriptorPool*              pool          = nullptr;
        VriDescriptorSet*               set           = nullptr;
        std::vector<VriDescriptor*>     views;
        std::unique_ptr<ShaderPipeline> pipeline;

        ~Bindings()
        {
            device.waitIdleForDestruction();
            pipeline.reset();
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            for (auto* view : views)
            {
                device.core.DestroyDescriptor(view);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }
    };

    std::expected<VriDescriptor*, std::string>
    createView(Bindings& bindings, Buffer& buffer, VriDescriptorType type, uint64_t offset, uint64_t size)
    {
        const VriBufferViewDesc desc {buffer.handle, type, VriFormat_Unknown, offset, size};
        VriDescriptor*          result = nullptr;
        const auto              status = bindings.device.core.CreateBufferView(bindings.device.handle, &desc, &result);
        if (result)
        {
            bindings.views.push_back(result);
        }
        if (status != VriResult_Success || !result)
        {
            return std::unexpected("Create descriptor-binding view: " + std::to_string(status));
        }
        return result;
    }

    std::expected<void, std::string> createBindings(Bindings&               bindings,
                                                    const ShaderProgram&    program,
                                                    Buffer&                 inputA,
                                                    Buffer&                 inputB,
                                                    Buffer&                 output,
                                                    VriDescriptorRangeFlags flags)
    {
        auto& device = bindings.device;
        bindings.viewAlignment =
            std::lcm(uint64_t(16),
                     uint64_t(std::max(device.core.GetDeviceDesc(device.handle)->storageBufferOffsetAlignment, 1u)));
        const std::array ranges {
            VriDescriptorRangeDesc {0, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute},
            VriDescriptorRangeDesc {1, 4, VriDescriptorType_StructuredBuffer, VriShaderStage_Compute, flags}};
        const VriDescriptorSetDesc  set {0, ranges.data(), uint32_t(ranges.size())};
        const VriPushConstantDesc   push {0, 16, VriShaderStage_Compute};
        const VriPipelineLayoutDesc layout {&set, 1, &push, 1, VriShaderStage_Compute};
        auto                        status = device.core.CreatePipelineLayout(device.handle, &layout, &bindings.layout);
        if (status != VriResult_Success || !bindings.layout)
        {
            return std::unexpected("Create descriptor-binding layout: " + std::to_string(status));
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum    = 1;
        pool.structuredBufferMaxNum = 4;
        pool.storageBufferMaxNum    = 1;
        status                      = device.core.CreateDescriptorPool(device.handle, &pool, &bindings.pool);
        if (status != VriResult_Success || !bindings.pool)
        {
            return std::unexpected("Create descriptor-binding pool: " + std::to_string(status));
        }
        status = device.core.AllocateDescriptorSets(bindings.pool, bindings.layout, 0, &bindings.set, 1);
        if (status != VriResult_Success || !bindings.set)
        {
            return std::unexpected("Allocate descriptor-binding set: " + std::to_string(status));
        }
        auto destination = createView(bindings, output, VriDescriptorType_StorageBuffer, 0, outputBytes);
        if (!destination)
        {
            return std::unexpected(destination.error());
        }
        const VriDescriptor*               outputView = *destination;
        const VriDescriptorRangeUpdateDesc outputUpdate {&outputView, 1};
        device.core.UpdateDescriptorRanges(bindings.set, 0, 1, &outputUpdate);
        const bool sparse = (flags & VriDescriptorRange_PartiallyBound) != 0;
        for (uint32_t index = 0; index < 4; ++index)
        {
            if (sparse && index != 0 && index != 3)
            {
                continue;
            }
            auto input = createView(bindings,
                                    index % 2 ? inputB : inputA,
                                    VriDescriptorType_StructuredBuffer,
                                    (index == 0 ? 1u : index) * bindings.viewAlignment,
                                    32);
            if (!input)
            {
                return std::unexpected(input.error());
            }
            const VriDescriptor*               inputView = *input;
            const VriDescriptorRangeUpdateDesc update {&inputView, 1, index};
            device.core.UpdateDescriptorRanges(bindings.set, 1, 1, &update);
        }
        auto pipeline = ShaderPipeline::create(
            device,
            program,
            {{"main", VriShaderStage_Compute}},
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                VriPipeline*                 result = nullptr;
                const VriComputePipelineDesc desc {bindings.layout, shaders[0], device.pipelineCache};
                const auto                   created = device.core.CreateComputePipeline(device.handle, &desc, &result);
                if (created != VriResult_Success || !result)
                {
                    if (result)
                    {
                        device.core.DestroyPipeline(result);
                    }
                    return std::unexpected("Create descriptor-binding pipeline: " + std::to_string(created));
                }
                return result;
            });
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        bindings.pipeline = std::move(*pipeline);
        return {};
    }

    std::expected<void, std::string> checkReflection(const ShaderProgram& program)
    {
        auto reflected = program.resourceBindingsExpected();
        if (!reflected)
        {
            return std::unexpected(reflected.error());
        }
        const auto matches = [&](uint32_t binding, VriDescriptorType type, uint32_t count)
        {
            return std::ranges::count_if(*reflected,
                                         [&](const auto& resource)
                                         {
                                             return !resource.pushConstant && resource.set == 0 &&
                                                    resource.binding == binding && resource.type == type &&
                                                    resource.count == count;
                                         }) == 1;
        };
        if (!matches(0, VriDescriptorType_StorageBuffer, 1) || !matches(1, VriDescriptorType_StructuredBuffer, 4))
        {
            return std::unexpected("Descriptor-binding shader reflection differs from the native layout");
        }
        return {};
    }

    std::expected<void, std::string> checkPartialLayouts(Device& device)
    {
        constexpr VriDescriptorRangeFlags flags = VriDescriptorRange_Array | VriDescriptorRange_PartiallyBound;
        const std::array                  types {VriDescriptorType_Sampler,
                                VriDescriptorType_Texture,
                                VriDescriptorType_StorageTexture,
                                VriDescriptorType_ConstantBuffer,
                                VriDescriptorType_StructuredBuffer,
                                VriDescriptorType_StorageBuffer};
        for (const auto type : types)
        {
            const VriDescriptorRangeDesc range {0, 4, type, VriShaderStage_Compute, flags};
            const VriDescriptorSetDesc   set {0, &range, 1};
            const VriPipelineLayoutDesc  desc {&set, 1, nullptr, 0, VriShaderStage_Compute};
            VriPipelineLayout*           layout  = nullptr;
            const auto                   status  = device.core.CreatePipelineLayout(device.handle, &desc, &layout);
            const bool                   created = status == VriResult_Success && layout;
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
            if (!created)
            {
                return std::unexpected("Create partial layout for descriptor type " + std::to_string(type) + ": " +
                                       std::to_string(status));
            }
        }
        std::puts("DESCRIPTOR_BINDINGS partial_layout_types=6");
        return {};
    }

    std::expected<void, std::string> checkDisabledBindless(Device& active)
    {
        if (active.core.GetDeviceDesc(active.handle)->graphicsAPI != VriGraphicsAPI_Vulkan)
        {
            return {};
        }
        auto device = Device::create(true, nullptr, 0);
        if (!device)
        {
            return std::unexpected(device.error());
        }
        VriDescriptorRangeDesc      range {0,
                                      4,
                                      VriDescriptorType_StructuredBuffer,
                                      VriShaderStage_Compute,
                                      VriDescriptorRange_Array};
        const VriDescriptorSetDesc  set {0, &range, 1};
        const VriPipelineLayoutDesc desc {&set, 1, nullptr, 0, VriShaderStage_Compute};
        VriPipelineLayout*          layout = nullptr;
        auto                        status = (*device)->core.CreatePipelineLayout((*device)->handle, &desc, &layout);
        const bool                  fixedCreated = status == VriResult_Success && layout;
        if (layout)
        {
            (*device)->core.DestroyPipelineLayout(layout);
            layout = nullptr;
        }
        if (!fixedCreated)
        {
            return std::unexpected("Fixed array incorrectly requires bindless features");
        }
        range.flags |= VriDescriptorRange_PartiallyBound;
        std::puts("DESCRIPTOR_BINDINGS_EXPECTED_FEATURE_BEGIN");
        std::fflush(stdout);
        status = (*device)->core.CreatePipelineLayout((*device)->handle, &desc, &layout);
        std::puts("DESCRIPTOR_BINDINGS_EXPECTED_FEATURE_END");
        std::fflush(stdout);
        const bool rejected = status == VriResult_Unsupported && !layout;
        if (layout)
        {
            (*device)->core.DestroyPipelineLayout(layout);
        }
        if (!rejected)
        {
            return std::unexpected("Partial array accepted without enabled bindless features");
        }
        std::puts("DESCRIPTOR_BINDINGS disabled_feature_rejected=passed fixed_without_bindless=passed");
        return {};
    }

    std::expected<void, std::string> run(Device& device, const ShaderProgram& program)
    {
        if (auto reflected = checkReflection(program); !reflected)
        {
            return reflected;
        }
        if (auto layouts = checkPartialLayouts(device); !layouts)
        {
            return layouts;
        }
        std::array<uint32_t, 512> dataA {};
        std::array<uint32_t, 512> dataB {};
        for (uint32_t index = 0; index < dataA.size(); ++index)
        {
            dataA[index] = index * 17 + 3;
            dataB[index] = index * 31 + 5;
        }
        auto inputA =
            uploadBufferExpected(device, std::as_bytes(std::span(dataA)), VriBufferUsage_StorageBuffer, read, 4);
        auto inputB =
            uploadBufferExpected(device, std::as_bytes(std::span(dataB)), VriBufferUsage_StorageBuffer, read, 4);
        auto frame = Frame::create(device);
        if (!inputA)
        {
            return std::unexpected(inputA.error());
        }
        if (!inputB)
        {
            return std::unexpected(inputB.error());
        }
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const std::array<VriDescriptorRangeFlags, 3> variants {
            VriDescriptorRange_Array,
            VriDescriptorRange_Array | VriDescriptorRange_PartiallyBound,
            VriDescriptorRange_Array | VriDescriptorRange_PartiallyBound | VriDescriptorRange_VariableSized};
        const std::array names {"fixed", "partial", "variable"};
        for (size_t model = 0; model < variants.size(); ++model)
        {
            std::array<uint32_t, outputBytes / 4> oracle;
            oracle.fill(sentinel);
            auto output = uploadBufferExpected(device,
                                               std::as_bytes(std::span(oracle)),
                                               VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc,
                                               write,
                                               16);
            auto readback =
                Buffer::create(device, {outputBytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            if (!output)
            {
                return std::unexpected(output.error());
            }
            if (!readback)
            {
                return std::unexpected(readback.error());
            }
            Bindings bindings {device};
            if (auto created = createBindings(bindings, program, **inputA, **inputB, **output, variants[model]);
                !created)
            {
                return created;
            }
            for (uint32_t iteration = 0; iteration < 2; ++iteration)
            {
                if (iteration == 1)
                {
                    auto view = createView(bindings,
                                           **inputA,
                                           VriDescriptorType_StructuredBuffer,
                                           6 * bindings.viewAlignment,
                                           32);
                    if (!view)
                    {
                        return std::unexpected(view.error());
                    }
                    const VriDescriptor*               descriptor = *view;
                    const VriDescriptorRangeUpdateDesc update {&descriptor, 1, 3};
                    device.core.UpdateDescriptorRanges(bindings.set, 1, 1, &update);
                }
                auto begun = (*frame)->beginExpected();
                if (!begun)
                {
                    return std::unexpected(begun.error());
                }
                auto* command = *begun;
                (*inputA)->transition(command, read);
                (*inputB)->transition(command, read);
                (*output)->transition(command, write);
                device.core.CmdSetPipelineLayout(command, bindings.layout);
                device.core.CmdSetPipeline(command, bindings.pipeline->handle());
                device.core.CmdSetDescriptorSet(command, 0, bindings.set);
                for (uint32_t choice = 0; choice < 2; ++choice)
                {
                    const uint32_t                selected    = choice == 0 ? 0 : 3;
                    const uint32_t                destination = iteration * 2 + choice;
                    const std::array<uint32_t, 4> parameters {selected, destination, iteration * 19, 0};
                    device.core.CmdSetConstants(command, 0, parameters.data(), sizeof(parameters));
                    const VriDispatchDesc dispatch {1, 1, 1};
                    device.core.CmdDispatch(command, &dispatch);
                    const bool useB    = selected == 3 && iteration == 0;
                    uint32_t   element = uint32_t(bindings.viewAlignment / 4);
                    if (selected == 3)
                    {
                        element = uint32_t((iteration == 0 ? 3 : 6) * bindings.viewAlignment / 4);
                    }
                    const auto&      data = useB ? dataB : dataA;
                    const std::array expected {data[element], data[element + 3], selected, iteration * 19};
                    std::copy(expected.begin(), expected.end(), oracle.begin() + destination * 4);
                }
                (*output)->transition(command, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                (*readback)->transition(command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
                const VriBufferCopyDesc copy {0, 0, outputBytes};
                device.core.CmdCopyBuffer(command, (*readback)->handle, (*output)->handle, &copy);
                if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
                {
                    return submitted;
                }
                const auto* values =
                    static_cast<const uint32_t*>(device.core.MapBuffer((*readback)->handle, 0, outputBytes));
                if (!values)
                {
                    return std::unexpected("Map descriptor-binding result failed");
                }
                const auto     mismatch = std::mismatch(oracle.begin(), oracle.end(), values);
                const bool     equal    = mismatch.first == oracle.end();
                const auto     channel  = size_t(mismatch.first - oracle.begin());
                const uint32_t actual   = equal ? 0 : values[channel];
                device.core.UnmapBuffer((*readback)->handle);
                if (!equal)
                {
                    return std::unexpected(std::string(names[model]) + " descriptor-binding channel " +
                                           std::to_string(channel) + ": " + std::to_string(actual) + " versus " +
                                           std::to_string(oracle[channel]));
                }
            }
            std::printf(
                "DESCRIPTOR_BINDINGS model=%s frames=2 populated=%u post_completion_update=passed channels=64\n",
                names[model],
                model == 0 ? 4 : 2);
        }
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fputs("Usage: test-descriptor-bindings descriptor_bindings.vshaderc\n", stderr);
        return 2;
    }
    auto program = vultra::ShaderProgram::loadExpected(argv[1]);
    if (!program)
    {
        std::fprintf(stderr, "%s\n", program.error().c_str());
        return 1;
    }
    auto device = vultra::Device::create(true, nullptr, VriFeature_Bindless);
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
    if (auto disabled = checkDisabledBindless(**device); !disabled)
    {
        std::fprintf(stderr, "%s\n", disabled.error().c_str());
        return 1;
    }
    return 0;
}
