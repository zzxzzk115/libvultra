#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    struct DrawConstants
    {
        float    depth;
        uint32_t coverage;
    };

    const char* formatName(VriFormat format)
    {
        switch (format)
        {
            case VriFormat_D16_UNORM:
                return "D16";
            case VriFormat_D32_SFLOAT:
                return "D32";
            case VriFormat_S8_UINT:
                return "S8";
            case VriFormat_D24_UNORM_S8_UINT:
                return "D24S8";
            case VriFormat_D32_SFLOAT_S8_UINT:
                return "D32S8";
            default:
                return "Unknown";
        }
    }

    std::expected<void, std::string>
    probe(vultra::Device& device, const vultra::ShaderProgram& shader, VriFormat format)
    {
        using namespace vultra;
        const auto* caps     = device.core.GetDeviceDesc(device.handle);
        const bool  hasDepth = format != VriFormat_S8_UINT;
        const bool  stencil  = format != VriFormat_D16_UNORM && format != VriFormat_D32_SFLOAT;
        const bool  loadOnly = (!hasDepth || caps->hasLoadOnlyDepthAttachmentResolve) &&
                              (!stencil || caps->hasLoadOnlyStencilAttachmentResolve);
        const bool                array   = format == VriFormat_D32_SFLOAT || format == VriFormat_D32_SFLOAT_S8_UINT;
        const uint32_t            layer   = array ? 1 : 0;
        const VriImageAspectFlags aspects = (hasDepth ? VriImageAspect_Depth : VriImageAspect_None) |
                                            (stencil ? VriImageAspect_Stencil : VriImageAspect_None);
        float sampleZero = .375f;
        if (format == VriFormat_D16_UNORM)
        {
            sampleZero = 1.f;
        }
        if (format == VriFormat_D24_UNORM_S8_UINT)
        {
            sampleZero = 0.f;
        }
        constexpr Extent                        extent {4, 4};
        constexpr uint32_t                      samples = 4;
        constexpr std::array<uint32_t, samples> stencilValues {43, 61, 97, 149};
        constexpr std::array                    planes {VriImageAspect_Depth, VriImageAspect_Stencil};
        constexpr VriShaderStageFlags stages = VriShaderStage_Vertex | VriShaderStage_Fragment;
        const VriPushConstantDesc     push {0, sizeof(DrawConstants), VriShaderStage_Fragment};
        const VriPipelineLayoutDesc   layoutDesc {nullptr, 0, &push, 1, stages};
        VriPipelineLayout*            rawLayout = nullptr;
        const auto                    status = device.core.CreatePipelineLayout(device.handle, &layoutDesc, &rawLayout);
        std::unique_ptr<VriPipelineLayout, decltype(device.core.DestroyPipelineLayout)> layout(
            rawLayout,
            device.core.DestroyPipelineLayout);
        if (status != VriResult_Success || !layout)
        {
            return std::unexpected("Create sample coverage pipeline layout failed");
        }
        std::array<std::unique_ptr<ShaderPipeline>, samples> pipelines;
        const auto                                           pipelineCount = stencil ? samples : 1;
        for (uint32_t sample = 0; sample < pipelineCount; ++sample)
        {
            auto pipeline = ShaderPipeline::create(
                device,
                shader,
                {{"vertexMain", VriShaderStage_Vertex},
                 {hasDepth ? "fragmentMain" : "stencilMain", VriShaderStage_Fragment}},
                [&](std::span<const VriShaderDesc> stages) -> std::expected<VriPipeline*, std::string>
                {
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineLayout                  = layout.get();
                    desc.shaders                         = stages.data();
                    desc.shaderNum                       = uint32_t(stages.size());
                    desc.inputAssembly.topology          = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode          = VriCullMode_None;
                    desc.rasterization.lineWidth         = 1;
                    desc.multisample.sampleNum           = samples;
                    desc.outputMerger.depthStencilFormat = format;
                    desc.depthStencil.depthTest          = hasDepth ? VRI_TRUE : VRI_FALSE;
                    desc.depthStencil.depthWrite         = hasDepth ? VRI_TRUE : VRI_FALSE;
                    if (stencil)
                    {
                        desc.depthStencil.stencilTest = VRI_TRUE;
                        desc.depthStencil.front       = {VriStencilOp_Keep,
                                                         VriStencilOp_Replace,
                                                         VriStencilOp_Keep,
                                                         VriCompareOp_Always,
                                                         255,
                                                         255,
                                                         stencilValues[sample]};
                        desc.depthStencil.back        = desc.depthStencil.front;
                    }
                    desc.depthStencil.depthCompareOp = VriCompareOp_Always;
                    VriPipeline* result              = nullptr;
                    const auto   made = device.core.CreateGraphicsPipeline(device.handle, &desc, &result);
                    if (made != VriResult_Success || !result)
                    {
                        if (result)
                        {
                            device.core.DestroyPipeline(result);
                        }
                        return std::unexpected("Create depth coverage pipeline failed: " + std::to_string(made));
                    }
                    return result;
                });
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            pipelines[sample] = std::move(*pipeline);
        }

        RenderGraph graph(device);
        auto        multisample = depthTexture(extent);
        multisample.format      = format;
        multisample.sampleNum   = samples;
        multisample.type        = array ? VriTextureType_2DArray : VriTextureType_2D;
        multisample.layerNum    = array ? 2 : 1;
        auto resolved           = multisample;
        resolved.width *= 2;
        resolved.height *= 2;
        resolved.mipNum    = 2;
        resolved.sampleNum = 1;
        resolved.usage |= VriTextureUsage_TransferSrc;
        auto source = graph.createTextureExpected("samples", multisample);
        auto stored = graph.createTextureExpected("stored-resolve", resolved);
        auto loaded = graph.createTextureExpected("loaded-resolve", resolved);
        if (!source || !stored || !loaded)
        {
            return std::unexpected("Create depth resolve graph resources failed");
        }
        constexpr uint32_t                              mip = 1;
        const RenderGraph::TextureRange                                sampleRange {0, 1, layer, 1, aspects};
        const RenderGraph::TextureRange                                targetRange {mip, 1, layer, 1, aspects};
        std::array<std::array<std::unique_ptr<TextureReadback>, 2>, 2> readbacks;
        const auto                                                     attachmentFor = [&](Texture&            sourceTexture,
                                       Texture&            target,
                                       VriAttachmentLoadOp load,
                                       bool                recordsDraws,
                                       uint32_t            sourceLayer,
                                       uint32_t            targetMip,
                                       uint32_t            targetLayer)
        {
            if (stencil)
            {
                return sourceTexture.depthStencilResolveAttachmentExpected(target,
                                                                           load,
                                                                           recordsDraws,
                                                                           sourceLayer,
                                                                           targetMip,
                                                                           targetLayer);
            }
            return sourceTexture
                .depthResolveAttachmentExpected(target, load, recordsDraws, sourceLayer, targetMip, targetLayer);
        };
        bool                                            injectFailure = false;
        unsigned                                        callbacks     = 0;
        const auto addResolve = [&](const char* name, RenderGraph::Resource output, bool drawSamples)
        {
            const std::array uses {
                RenderGraph::Use {*source,
                                  drawSamples ? Usage::eDepthWriteResolve : Usage::eDepthReadWriteResolve,
                                  drawSamples,
                                  sampleRange},
                RenderGraph::Use {output, Usage::eDepthResolveDestination, true, targetRange}};
            return graph.addPassExpected(
                name,
                uses,
                [&, output, drawSamples](auto* commands, auto& current) -> std::expected<void, std::string>
                {
                    ++callbacks;
                    auto depth  = current.getTextureExpected(*source);
                    auto target = current.getTextureExpected(output);
                    if (!depth || !target)
                    {
                        return std::unexpected("Resolve pass resources disappeared");
                    }
                    const auto load       = drawSamples ? VriAttachmentLoadOp_Clear : VriAttachmentLoadOp_Load;
                    auto       attachment =
                        attachmentFor(**depth, **target, load, drawSamples || !loadOnly, layer, mip, layer);
                    if (!attachment)
                    {
                        return std::unexpected(attachment.error());
                    }
                    if (drawSamples)
                    {

                        attachment->clearValue.depthStencil.depth = .75f;
                        attachment->clearValue.depthStencil.stencil = 251;
                    }
                    else
                    {
                        attachment->storeOp = VriAttachmentStoreOp_DontCare;
                    }
                    VriAttachmentsDesc attachments {};
                    if (aspects & VriImageAspect_Depth)
                    {
                        attachments.depth = &*attachment;
                    }
                    if (stencil)
                    {
                        attachments.stencil = &*attachment;
                    }
                    attachments.renderArea = {0, 0, extent.width, extent.height};
                    attachments.layerNum   = 1;
                    device.core.CmdBeginRendering(commands, &attachments);
                    if (drawSamples || !loadOnly)
                    {
                        const VriViewport viewport {0, 0, float(extent.width), float(extent.height), 0, 1};
                        device.core.CmdSetViewports(commands, &viewport, 1);
                        device.core.CmdSetScissors(commands, &attachments.renderArea, 1);
                        device.core.CmdSetPipelineLayout(commands, layout.get());
                        const VriDrawDesc triangle {3, 1, 0, 0};
                        const std::array  depths {sampleZero, .125f, .5f, .875f};
                        for (uint32_t sample = 0; sample < samples; ++sample)
                        {
                            if (!drawSamples && sample != 1)
                            {
                                continue;
                            }
                            const auto& pipeline = pipelines[stencil ? sample : 0];
                            device.core.CmdSetPipeline(commands, pipeline->handle());
                            const DrawConstants constants {depths[sample], 1u << sample};
                            device.core.CmdSetConstants(commands, 0, &constants, sizeof(constants));
                            device.core.CmdDraw(commands, &triangle);
                        }
                    }
                    device.core.CmdEndRendering(commands);
                    if (injectFailure)
                    {
                        return std::unexpected("Injected failure after native depth resolve recording");
                    }
                    return {};
                });
        };
        if (auto added = addResolve("write-and-resolve", *stored, true); !added)
        {
            return added;
        }
        if (auto added = addResolve("load-and-resolve", *loaded, false); !added)
        {
            return added;
        }
        const std::array copyUses {RenderGraph::Use {*stored, Usage::eCopySource, false, targetRange},
                                   RenderGraph::Use {*loaded, Usage::eCopySource, false, targetRange}};
        if (auto added = graph.addPassExpected(
                "readback",
                copyUses,
                [&](auto* commands, auto& current) -> std::expected<void, std::string>
                {
                    const std::array outputs {*stored, *loaded};
                    for (size_t i = 0; i < outputs.size(); ++i)
                    {
                        auto target = current.getTextureExpected(outputs[i]);
                        if (!target)
                        {
                            return std::unexpected(target.error());
                        }
                        for (size_t plane = 0; plane < planes.size(); ++plane)
                        {
                            const auto aspect = planes[plane];
                            if (!(aspects & aspect))
                            {
                                continue;
                            }
                            auto& readback = readbacks[i][plane];
                            if (!readback)
                            {
                                auto created = TextureReadback::create(device, **target, mip, layer, aspect);
                                if (!created)
                                {
                                    return std::unexpected(created.error());
                                }
                                readback = std::move(*created);
                            }
                            if (auto recorded = readback->recordExpected(commands, **target); !recorded)
                            {
                                return recorded;
                            }
                        }
                    }
                    return {};
                },
                true);
            !added)
        {
            return added;
        }
        if (auto compiled = graph.compileExpected(false); !compiled)
        {
            return compiled;
        }
        auto depth        = graph.getTextureExpected(*source);
        auto firstOutput  = graph.getTextureExpected(*stored);
        auto secondOutput = graph.getTextureExpected(*loaded);
        if (!depth || !firstOutput || !secondOutput)
        {
            return std::unexpected("Compiled depth resolve resources disappeared");
        }
        if (attachmentFor(**depth, **depth, VriAttachmentLoadOp_Clear, true, layer, mip, layer) ||
            attachmentFor(**depth, **firstOutput, VriAttachmentLoadOp_Clear, true, layer, 0, layer) ||
            attachmentFor(**depth, **firstOutput, VriAttachmentLoadOp_Clear, true, multisample.layerNum, mip, layer) ||
            attachmentFor(**firstOutput, **secondOutput, VriAttachmentLoadOp_Clear, true, layer, mip, layer))
        {
            return std::unexpected("Invalid depth/stencil resolve pairing was accepted");
        }
        if (hasDepth && stencil)
        {
            RenderGraph partial(device);
            auto        partialTarget = partial.importResourceExpected("partial-resolve", **firstOutput);
            if (!partialTarget)
            {
                return std::unexpected(partialTarget.error());
            }
            const std::array uses {RenderGraph::Use {*partialTarget,
                                                     Usage::eDepthResolveDestination,
                                                     true,
                                                     {mip, 1, layer, 1, VriImageAspect_Stencil}}};
            if (auto added = partial.addPassExpected(
                    "partial",
                    uses,
                    [](auto*, auto&) -> std::expected<void, std::string>
                    {
                        return {};
                    },
                    true);
                !added)
            {
                return added;
            }
            const auto compiled = partial.compileExpected(false);
            if (compiled || compiled.error().find("invalid depth/stencil resolve aspect") == std::string::npos)
            {
                return std::unexpected("Partial combined attachment resolve was not rejected");
            }
        }
        const auto empty = attachmentFor(**depth, **firstOutput, VriAttachmentLoadOp_Load, false, layer, mip, layer);
        if (empty.has_value() != loadOnly)
        {
            return std::unexpected("Load-only depth resolve did not follow its native capability");
        }
        VriFence*  rawFence = nullptr;
        const auto made     = device.core.CreateFence(device.handle, 0, &rawFence);
        std::unique_ptr<VriFence, decltype(device.core.DestroyFence)> fence(rawFence, device.core.DestroyFence);
        if (made != VriResult_Success || !fence)
        {
            return std::unexpected("Create depth resolve route fence failed");
        }
        auto first  = Frame::create(device);
        auto second = Frame::create(device);
        auto transfer = Frame::create(device, VriQueueType_Transfer);
        if (!first || !second || !transfer)
        {
            return std::unexpected("Create depth resolve command buffers failed");
        }
        const auto initialDepth  = (*depth)->captureState();
        const auto initialTarget = (*firstOutput)->captureState();
        auto       rejectedCmd   = (*first)->beginExpected();
        if (!rejectedCmd)
        {
            return std::unexpected(rejectedCmd.error());
        }
        injectFailure = true;
        auto rejected = graph.executeExpected(*rejectedCmd);
        injectFailure = false;
        if (rejected || rejected.error().find("Injected failure") == std::string::npos ||
            !(*first)->discardExpected() || (*depth)->captureState() != initialDepth ||
            (*firstOutput)->captureState() != initialTarget)
        {
            return std::unexpected("Depth resolve recording failure did not restore the checkpoint");
        }

        for (bool routed : {false, true})
        {
            auto firstCmd = (*first)->beginExpected();
            if (!firstCmd)
            {
                return std::unexpected(firstCmd.error());
            }
            if (routed)
            {
                auto secondCmd = (*second)->beginExpected();
                if (!secondCmd)
                {
                    return std::unexpected(secondCmd.error());
                }
                auto transferCmd = (*transfer)->beginExpected();
                if (!transferCmd)
                {
                    return std::unexpected(transferCmd.error());
                }
                const std::array records {RenderGraph::PassRecording {"write-and-resolve", *firstCmd},
                                          RenderGraph::PassRecording {"load-and-resolve", *secondCmd},
                                          RenderGraph::PassRecording {"readback", *transferCmd}};
                const std::array order {RenderGraph::RecordingDependency {*firstCmd, *secondCmd},
                                        RenderGraph::RecordingDependency {*secondCmd, *transferCmd}};
                const auto       before = callbacks;
                for (size_t count = 0; count < order.size(); ++count)
                {
                    auto unordered = graph.executeOrderedExpected(*firstCmd, records, std::span(order).first(count));
                    if (unordered ||
                        unordered.error().find("misses a required submission dependency") == std::string::npos ||
                        callbacks != before)
                    {
                        return std::unexpected("Depth/stencil resolve route did not reject a missing dependency");
                    }
                }
                if (auto recorded = graph.executeOrderedExpected(*firstCmd, records, order); !recorded)
                {
                    return recorded;
                }
                const std::array storedReady {VriFenceSubmitDesc {fence.get(), 1, VriPipelineStage_AllCommands}};
                const std::array loadedReady {VriFenceSubmitDesc {fence.get(), 2, VriPipelineStage_AllCommands}};
                if (!(*first)->submitExpected({}, storedReady) ||
                    !(*second)->submitExpected(storedReady, loadedReady) || !(*transfer)->submitExpected(loadedReady) ||
                    !(*transfer)->waitExpected() || !(*second)->waitExpected() || !(*first)->waitExpected())
                {
                    return std::unexpected("Complete routed depth/stencil resolve failed");
                }
            }
            else
            {
                if (auto recorded = graph.executeExpected(*firstCmd); !recorded)
                {
                    return recorded;
                }
                if (auto complete = (*first)->submitAndWaitExpected(); !complete)
                {
                    return complete;
                }
            }
            for (const auto& output : readbacks)
            {
                if (const auto& readback = output[0])
                {
                    auto data = readback->dataExpected();
                    if (!data)
                    {
                        return std::unexpected(data.error());
                    }
                    bool exact = false;
                    if (format == VriFormat_D16_UNORM)
                    {
                        exact = data->bytes.size() == 32 && std::ranges::count(data->bytes, std::byte {255}) == 32;
                    }
                    else if (format == VriFormat_D24_UNORM_S8_UINT)
                    {
                        std::array<uint32_t, 16> values {};
                        if (data->bytes.size() == sizeof(values))
                        {
                            std::memcpy(values.data(), data->bytes.data(), sizeof(values));
                            exact = std::ranges::all_of(values,
                                                        [](uint32_t value)
                                                        {
                                                            return (value & 0x00ffffffu) == 0;
                                                        });
                        }
                    }
                    else
                    {
                        std::array<float, 16> values {};
                        if (data->bytes.size() == sizeof(values))
                        {
                            std::memcpy(values.data(), data->bytes.data(), sizeof(values));
                            exact = std::ranges::count(values, sampleZero) == values.size();
                        }
                    }
                    if (!exact)
                    {
                        return std::unexpected("Depth resolve did not select sample zero");
                    }
                }
                if (const auto& readback = output[1])
                {
                    auto data = readback->dataExpected();
                    if (!data)
                    {
                        return std::unexpected(data.error());
                    }
                    if (data->bytes.size() != 16 || std::ranges::count(data->bytes, std::byte(stencilValues[0])) != 16)
                    {
                        const auto firstValue = data->bytes.empty() ? 0u : std::to_integer<unsigned>(data->bytes[0]);
                        return std::unexpected("Stencil resolve did not select sample zero: " +
                                               std::to_string(firstValue));
                    }
                }
            }
        }
        std::printf("DEPTH_RESOLVE format=%s samples=4 exact_depth=%d exact_stencil=%d stored_and_loaded=1 "
                    "routed=1 transfer=1 discard_retry=1 load_only=%d\n",
                    formatName(format),
                    hasDepth ? 64 : 0,
                    stencil ? 64 : 0,
                    int(loadOnly));
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fputs("Usage: test-depth-resolve depth-resolve.vshaderc\n", stderr);
        return 2;
    }
    auto program = vultra::ShaderProgram::loadExpected(argv[1]);
    auto device  = vultra::Device::create(true, nullptr, 0);
    if (!program || !device)
    {
        std::fprintf(stderr, "%s\n", (program ? device.error() : program.error()).c_str());
        return 1;
    }
    bool failed = false;
    for (auto format : {VriFormat_D16_UNORM,
                        VriFormat_D32_SFLOAT,
                        VriFormat_S8_UINT,
                        VriFormat_D32_SFLOAT_S8_UINT,
                        VriFormat_D24_UNORM_S8_UINT})
    {
        if (format == VriFormat_D24_UNORM_S8_UINT &&
            !((*device)->core.GetFormatSupport((*device)->handle, format) & VriFormatSupport_DepthStencil))
        {
            std::printf("DEPTH_RESOLVE_UNSUPPORTED format=%s\n", formatName(format));
            continue;
        }
        if (auto result = probe(**device, *program, format); !result)
        {
            std::fprintf(stderr, "%s: %s\n", formatName(format), result.error().c_str());
            failed = true;
        }
    }
    return failed ? 1 : 0;
}
