#pragma once

#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>

namespace vultra::test::depth_history_msaa_probe
{
    struct Sampling
    {
        Device&                         device;
        VriPipelineLayout*              layout     = nullptr;
        VriDescriptorPool*              pool       = nullptr;
        VriDescriptorSet*               set        = nullptr;
        VriDescriptor*                  outputView = nullptr;
        std::unique_ptr<ShaderPipeline> pipeline;

        explicit Sampling(Device& gpu) :
            device(gpu)
        {
        }

        ~Sampling()
        {
            device.waitIdleForDestruction();
            pipeline.reset();
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
        }

        std::expected<void, std::string> initialize(const ShaderProgram& program)
        {
            const std::array ranges {
                VriDescriptorRangeDesc {0, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
                VriDescriptorRangeDesc {1, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
                VriDescriptorRangeDesc {2, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute}};
            const VriDescriptorSetDesc descriptorSet {0, ranges.data(), uint32_t(ranges.size())};
            const VriPushConstantDesc  constants {0, 16, VriShaderStage_Compute};
            VriPipelineLayoutDesc      description {};
            description.descriptorSets   = &descriptorSet;
            description.descriptorSetNum = 1;
            description.pushConstants    = &constants;
            description.pushConstantNum  = 1;
            description.shaderStages     = VriShaderStage_Compute;
            if (device.core.CreatePipelineLayout(device.handle, &description, &layout) != VriResult_Success)
            {
                return std::unexpected("Create MSAA history sampling layout failed");
            }
            VriDescriptorPoolDesc poolDesc {};
            poolDesc.descriptorSetMaxNum = 1;
            poolDesc.textureMaxNum       = 2;
            poolDesc.storageBufferMaxNum = 1;
            if (device.core.CreateDescriptorPool(device.handle, &poolDesc, &pool) != VriResult_Success ||
                device.core.AllocateDescriptorSets(pool, layout, 0, &set, 1) != VriResult_Success)
            {
                return std::unexpected("Create MSAA history sampling descriptors failed");
            }
            auto created = ShaderPipeline::create(
                device,
                program,
                {{"sampleDepthStencilMs", VriShaderStage_Compute}},
                [this](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    const VriComputePipelineDesc desc {layout, shaders.front(), device.pipelineCache};
                    VriPipeline*                 handle = nullptr;
                    const auto status = device.core.CreateComputePipeline(device.handle, &desc, &handle);
                    if (status != VriResult_Success || !handle)
                    {
                        if (handle)
                        {
                            device.core.DestroyPipeline(handle);
                        }
                        return std::unexpected("Create MSAA depth/stencil sampling pipeline: " +
                                               std::to_string(status));
                    }
                    return handle;
                });
            if (!created)
            {
                return std::unexpected(created.error());
            }
            pipeline = std::move(*created);
            return {};
        }

        std::expected<void, std::string> bind(Texture& texture, Buffer& output)
        {
            VriTextureViewDesc view {};
            view.viewType                   = VriTextureViewType_MaxEnum;
            view.aspect                     = VriImageAspect_Depth;
            auto depth                      = texture.viewExpected(view);
            view.aspect                     = VriImageAspect_Stencil;
            auto                    stencil = texture.viewExpected(view);
            const VriBufferViewDesc bufferView {output.handle,
                                                VriDescriptorType_StorageBuffer,
                                                VriFormat_Unknown,
                                                0,
                                                output.desc.size};
            if (!depth || !stencil ||
                device.core.CreateBufferView(device.handle, &bufferView, &outputView) != VriResult_Success)
            {
                return std::unexpected("Create MSAA history aspect/output views failed");
            }
            const std::array<const VriDescriptor*, 3>   descriptors {*depth, *stencil, outputView};
            std::array<VriDescriptorRangeUpdateDesc, 3> updates {};
            for (size_t i = 0; i < updates.size(); ++i)
            {
                updates[i] = {&descriptors[i], 1};
            }
            device.core.UpdateDescriptorRanges(set, 0, uint32_t(updates.size()), updates.data());
            return {};
        }
    };

    inline std::expected<void, std::string> probe(Device& device, const ShaderProgram& program)
    {
        constexpr uint32_t width   = 8;
        constexpr uint32_t height  = 8;
        constexpr uint32_t layers  = 3;
        constexpr uint32_t samples = 4;
        constexpr uint64_t bytes   = uint64_t(width) * height * layers * samples * sizeof(uint32_t) * 2;
        RenderGraph        graph(device);
        Sampling           sampling(device);
        if (auto initialized = sampling.initialize(program); !initialized)
        {
            return initialized;
        }
        auto desc      = colorTexture({width, height}, VriFormat_D32_SFLOAT_S8_UINT);
        desc.type      = VriTextureType_2DArray;
        desc.layerNum  = layers;
        desc.sampleNum = samples;
        desc.usage =
            VriTextureUsage_DepthStencilAttachment | VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst;
        auto history =
            graph.createHistoryTextureExpected("msaa-history", desc, VriClearValue {.depthStencil = {.25f, 17}});
        auto output = graph.createBufferExpected(
            "sample-values",
            {bytes, 8, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
        auto readback =
            graph.createBufferExpected("readback",
                                       {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!history || !output || !readback)
        {
            return std::unexpected("Create multisample depth/stencil history resources failed");
        }
        unsigned         phase = 1;
        const std::array observeUses {RenderGraph::Use {*history, Usage::eSampled},
                                      RenderGraph::Use {*output, Usage::eStorageWrite, true}};
        if (!graph.addPassExpected(
                "observe-samples",
                observeUses,
                [&](auto* commands, auto&) -> std::expected<void, std::string>
                {
                    device.core.CmdSetPipelineLayout(commands, sampling.layout);
                    device.core.CmdSetPipeline(commands, sampling.pipeline->handle());
                    device.core.CmdSetDescriptorSet(commands, 0, sampling.set);
                    const std::array parameters {width, height, layers, samples};
                    device.core.CmdSetConstants(commands, 0, parameters.data(), uint32_t(sizeof(parameters)));
                    const VriDispatchDesc dispatch {1, 1, layers * samples};
                    device.core.CmdDispatch(commands, &dispatch);
                    return {};
                }))
        {
            return std::unexpected("Add multisample observation failed");
        }
        const std::array copyUses {RenderGraph::Use {*output, Usage::eCopySource},
                                   RenderGraph::Use {*readback, Usage::eCopyDestination, true}};
        if (!graph.addPassExpected("read-samples",
                                   copyUses,
                                   [&](auto* commands, auto& current) -> std::expected<void, std::string>
                                   {
                                       auto from = current.getBufferExpected(*output);
                                       auto to   = current.getBufferExpected(*readback);
                                       if (!from || !to)
                                       {
                                           return std::unexpected("Get multisample observation buffers failed");
                                       }
                                       VriBufferCopyDesc copy {};
                                       copy.size = bytes;
                                       device.core.CmdCopyBuffer(commands, (*to)->handle, (*from)->handle, &copy);
                                       return {};
                                   }) ||
            !graph.addPassExpected(
                "publish-samples",
                std::array {RenderGraph::Use {*history, Usage::eCopyDestination, true}},
                [&](auto* commands, auto& current) -> std::expected<void, std::string>
                {
                    auto texture = current.getTextureExpected(*history);
                    if (!texture)
                    {
                        return std::unexpected(texture.error());
                    }
                    return (*texture)->clearDepthStencilExpected(commands, {.5f + float(phase) / 32, 31 + phase});
                }) ||
            !graph.exportResourceExpected(*readback))
        {
            return std::unexpected("Add multisample readback/publishing failed");
        }
        if (auto compiled = graph.compileExpected(); !compiled)
        {
            return std::unexpected("Compile multisample history: " + compiled.error());
        }
        auto texture      = graph.getTextureExpected(*history);
        auto buffer       = graph.getBufferExpected(*output);
        auto resultBuffer = graph.getBufferExpected(*readback);
        auto frame        = Frame::create(device);
        if (!texture || !buffer || !resultBuffer || !frame)
        {
            return std::unexpected("Prepare multisample history recording failed");
        }
        if (auto bound = sampling.bind(**texture, **buffer); !bound)
        {
            return bound;
        }
        std::array<unsigned, layers> expectedDepth {};
        std::array<unsigned, layers> expectedStencil {};
        for (unsigned cycle = 0; cycle < 6; ++cycle)
        {
            phase = cycle + 1;
            if (cycle == 2 || cycle == 3 || cycle == 4)
            {
                const uint32_t      layer   = cycle - 2;
                VriImageAspectFlags aspects = VriImageAspect_Depth | VriImageAspect_Stencil;
                if (cycle == 2)
                {
                    aspects = VriImageAspect_Depth;
                }
                if (cycle == 3)
                {
                    aspects = VriImageAspect_Stencil;
                }
                const std::array selected {RenderGraph::HistorySelection {*history, {0, 1, layer, 1, aspects}}};
                if (auto reset = graph.resetHistoryExpected(selected); !reset)
                {
                    return reset;
                }
                if (aspects & VriImageAspect_Depth)
                {
                    expectedDepth[layer] = 0;
                }
                if (aspects & VriImageAspect_Stencil)
                {
                    expectedStencil[layer] = 0;
                }
            }
            if (cycle == 5)
            {
                if (auto reset = graph.resetHistoryExpected(); !reset)
                {
                    return reset;
                }
                expectedDepth.fill(0);
                expectedStencil.fill(0);
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return recorded;
            }
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            const auto* values = static_cast<const uint32_t*>(device.core.MapBuffer((*resultBuffer)->handle, 0, bytes));
            if (!values)
            {
                return std::unexpected("Map multisample depth/stencil history readback failed");
            }
            bool matches = true;
            for (size_t i = 0; i < bytes / (sizeof(uint32_t) * 2); ++i)
            {
                const auto layer        = i / (width * height * samples);
                const auto depthPhase   = expectedDepth[layer];
                const auto stencilPhase = expectedStencil[layer];
                const auto depth        = std::bit_cast<uint32_t>(depthPhase ? .5f + float(depthPhase) / 32 : .25f);
                const auto stencil      = stencilPhase ? 31 + stencilPhase : 17;
                if ((values[i * 2] != depth || values[i * 2 + 1] != stencil) && matches)
                {
                    std::fprintf(
                        stderr,
                        "MSAA_HISTORY_MISMATCH cycle=%u layer=%zu texel_sample=%zu actual=%u,%u expected=%u,%u\n",
                        cycle,
                        layer,
                        i,
                        values[i * 2],
                        values[i * 2 + 1],
                        depth,
                        stencil);
                }
                matches &= values[i * 2] == depth && values[i * 2 + 1] == stencil;
            }
            device.core.UnmapBuffer((*resultBuffer)->handle);
            if (!matches)
            {
                return std::unexpected(
                    "Multisample depth/stencil history did not preserve every selected sample/aspect");
            }
            expectedDepth.fill(phase);
            expectedStencil.fill(phase);
        }
        std::puts("DEPTH_HISTORY_MSAA samples=4 layers=3 frames=6 exact_channels=9216 selected_aspects=preserved");
        return {};
    }
} // namespace vultra::test::depth_history_msaa_probe
