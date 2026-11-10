#include <vultra/servers/rendering/builtin/clustered_lights.hpp>
#include <vultra/servers/rendering/builtin/deferred_lighting.hpp>
#include <vultra/servers/rendering/builtin/frame_data.hpp>
#include <vultra/servers/rendering/builtin/gbuffer_pipeline.hpp>

#include <algorithm>

namespace vultra
{
    std::expected<std::unique_ptr<DeferredLightingPipeline>, std::string>
    DeferredLightingPipeline::create(Device&                 device,
                                     VriPipelineLayout*      layout,
                                     const ShaderProgram&    sky,
                                     const ShaderProgram&    lighting,
                                     bool                    clustered,
                                     GBufferPositionEncoding positionEncoding)
    {
        if (!layout)
            return std::unexpected("Deferred lighting requires a layout");
        auto result = std::unique_ptr<DeferredLightingPipeline>(
            new DeferredLightingPipeline(device, layout, clustered, positionEncoding));
        const auto create =
            [&](const ShaderProgram& program) -> std::expected<std::unique_ptr<ShaderPipeline>, std::string>
        {
            return ShaderPipeline::create(
                device,
                program,
                {{"screenVertex", VriShaderStage_Vertex}, {"fragmentMain", VriShaderStage_Fragment}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    VriColorAttachmentDesc color {};
                    color.format         = VriFormat_RGBA16_SFLOAT;
                    color.colorWriteMask = VriColorWrite_RGBA;
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineCache           = device.pipelineCache;
                    desc.pipelineLayout          = layout;
                    desc.shaders                 = shaders.data();
                    desc.shaderNum               = uint32_t(shaders.size());
                    desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode  = VriCullMode_None;
                    desc.rasterization.lineWidth = 1;
                    desc.multisample.sampleNum   = 1;
                    desc.outputMerger.colors     = &color;
                    desc.outputMerger.colorNum   = 1;
                    VriPipeline* handle          = nullptr;
                    const auto   status          = device.core.CreateGraphicsPipeline(device.handle, &desc, &handle);
                    if (status != VriResult_Success || !handle)
                    {
                        if (handle)
                            device.core.DestroyPipeline(handle);
                        return std::unexpected("Create deferred fullscreen pipeline failed: " + std::to_string(status));
                    }
                    return handle;
                });
        };
        auto skyPipeline = create(sky);
        if (!skyPipeline)
            return std::unexpected(skyPipeline.error());
        auto lightingPipeline = create(lighting);
        if (!lightingPipeline)
            return std::unexpected(lightingPipeline.error());
        result->m_Sky      = std::move(*skyPipeline);
        result->m_Lighting = std::move(*lightingPipeline);
        return result;
    }

    std::expected<void, std::string> prepareDeferredLightingFrame(RenderGraph&                                graph,
                                                                  const std::array<RenderGraph::Resource, 7>& gbuffer,
                                                                  VriDescriptorSet*                           frameSet,
                                                                  GBufferPositionEncoding positionEncoding)
    {
        if (!frameSet)
            return std::unexpected("Deferred lighting requires a frame descriptor set");
        std::array<const VriDescriptor*, 7>         views {};
        std::array<VriDescriptorRangeUpdateDesc, 7> updates {};
        const auto                                  formats = gbufferFormats(positionEncoding);
        for (size_t i = 0; i < views.size(); ++i)
        {
            auto texture = graph.getTextureExpected(gbuffer[i]);
            if (!texture || (*texture)->desc.format != formats[i] ||
                !((*texture)->desc.usage & VriTextureUsage_ShaderResource))
                return std::unexpected("Deferred lighting G-buffer view is unavailable");
            auto view = (*texture)->viewExpected();
            if (!view)
                return std::unexpected(view.error());
            views[i]   = *view;
            updates[i] = {&views[i], 1};
        }
        graph.device().core.UpdateDescriptorRanges(frameSet, 12, uint32_t(updates.size()), updates.data());
        return {};
    }

    std::expected<void, std::string> addDeferredLightingGraphPasses(RenderGraph&                    graph,
                                                                    const DeferredLightingPipeline& pipeline,
                                                                    std::string                     name,
                                                                    DeferredLightingGraphInputs     inputs)
    {
        auto& device = graph.device();
        auto  frame  = graph.getBufferExpected(inputs.frame);
        auto  hdr    = graph.getTextureExpected(inputs.hdr);
        if (!pipeline.belongsTo(device) || !inputs.frameSet || !frame || !hdr ||
            !((*frame)->desc.usage & VriBufferUsage_ConstantBuffer) || (*frame)->desc.size < sizeof(BuiltinFrameData) ||
            (*hdr)->desc.format != VriFormat_RGBA16_SFLOAT || (*hdr)->desc.type != VriTextureType_2D ||
            !(*hdr)->desc.width || !(*hdr)->desc.height || (*hdr)->desc.layerNum != 1 || (*hdr)->desc.sampleNum != 1 ||
            !((*hdr)->desc.usage & VriTextureUsage_ColorAttachment) || !inputs.lighting[0] || !inputs.lighting[10])
            return std::unexpected("Invalid deferred lighting graph inputs");
        const auto width  = (*hdr)->desc.width;
        const auto height = (*hdr)->desc.height;
        if (pipeline.clustered())
        {
            auto           clusters = graph.getBufferExpected(inputs.clusters);
            const uint64_t columns  = (uint64_t(width) + ClusteredLights::kTileSize - 1) / ClusteredLights::kTileSize;
            const uint64_t rows     = (uint64_t(height) + ClusteredLights::kTileSize - 1) / ClusteredLights::kTileSize;
            if (!clusters || *clusters != inputs.clusterBuffer ||
                !((*clusters)->desc.usage & VriBufferUsage_StorageBuffer) ||
                columns * rows * ClusteredLights::kDepthSlices * 8 > (*clusters)->desc.size)
                return std::unexpected("Deferred lighting requires its matching clustered light buffer");
        }
        else if (inputs.clusters.graph || inputs.clusterBuffer)
        {
            return std::unexpected("Unclustered deferred lighting cannot use clustered light resources");
        }
        const auto formats = gbufferFormats(pipeline.positionEncoding());
        for (size_t i = 0; i < inputs.gbuffer.size(); ++i)
        {
            if (!graph.ownsResource(inputs.gbuffer[i]))
                return std::unexpected("Deferred lighting G-buffer does not belong to the graph");
            const auto info = graph.resourceInfo(inputs.gbuffer[i]);
            if (!info.isTexture || info.textureDesc.format != formats[i] || info.textureDesc.width != width ||
                info.textureDesc.height != height || info.textureDesc.type != VriTextureType_2D ||
                info.textureDesc.layerNum != 1 || info.textureDesc.sampleNum != 1 ||
                !(info.textureDesc.usage & VriTextureUsage_ShaderResource))
                return std::unexpected("Deferred lighting G-buffer attachment mismatch");
            for (size_t j = 0; j < i; ++j)
                if (inputs.gbuffer[i].index == inputs.gbuffer[j].index)
                    return std::unexpected("Deferred lighting G-buffer attachments must be distinct");
        }
        constexpr std::array<size_t, 10> slots {1, 2, 3, 4, 5, 6, 7, 8, 9, 11};
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (inputs.lightingTextures[i].graph == inputs.hdr.graph &&
                inputs.lightingTextures[i].index == inputs.hdr.index)
                return std::unexpected("Deferred lighting cannot sample its output attachment");
            auto texture = graph.getTextureExpected(inputs.lightingTextures[i]);
            if (!texture || !((*texture)->desc.usage & VriTextureUsage_ShaderResource))
                return std::unexpected("Deferred lighting requires sampled lighting textures");
            auto view = (*texture)->viewExpected();
            if (!view || *view != inputs.lighting[slots[i]])
                return std::unexpected("Deferred lighting descriptors do not match graph resources");
        }
        const auto appendLighting = [&inputs](std::vector<RenderGraph::Use>& uses)
        {
            for (auto resource : inputs.lightingTextures)
                if (std::ranges::none_of(uses,
                                         [resource](const auto& use)
                                         {
                                             return use.resource.graph == resource.graph &&
                                                    use.resource.index == resource.index;
                                         }))
                    uses.push_back({resource, Usage::eSampled});
        };
        const auto draw = [&device, &pipeline, inputs, width, height](VriCommandBuffer* commands,
                                                                      RenderGraph&      active,
                                                                      bool sky) -> std::expected<void, std::string>
        {
            auto target = active.getTextureExpected(inputs.hdr);
            if (!target)
                return std::unexpected(target.error());
            auto view = (*target)->viewExpected();
            if (!view)
                return std::unexpected(view.error());
            VriAttachmentDesc color {};
            color.view                    = *view;
            color.loadOp                  = sky ? VriAttachmentLoadOp_Clear : VriAttachmentLoadOp_Load;
            color.storeOp                 = VriAttachmentStoreOp_Store;
            color.clearValue.color.f32[3] = 1;
            VriAttachmentsDesc attachments {};
            attachments.colors     = &color;
            attachments.colorNum   = 1;
            attachments.renderArea = {0, 0, width, height};
            attachments.layerNum   = 1;
            device.core.CmdBeginRendering(commands, &attachments);
            const VriViewport viewport {0, 0, float(width), float(height), 0, 1};
            const VriRect     scissor {0, 0, width, height};
            device.core.CmdSetViewports(commands, &viewport, 1);
            device.core.CmdSetScissors(commands, &scissor, 1);
            device.core.CmdSetPipelineLayout(commands, pipeline.layout());
            device.core.CmdSetPipeline(commands, sky ? pipeline.sky() : pipeline.lighting());
            device.core.CmdSetDescriptorSet(commands, 0, inputs.frameSet);
            const VriDrawDesc triangle {3, 1, 0, 0};
            device.core.CmdDraw(commands, &triangle);
            device.core.CmdEndRendering(commands);
            return {};
        };
        std::vector<RenderGraph::Use> skyUses {{inputs.frame, Usage::eConstantRead},
                                               {inputs.hdr, Usage::eColorWrite, true}};
        appendLighting(skyUses);
        auto skyPass = graph.addPassExpected(name + ".sky",
                                             skyUses,
                                             [draw](auto* commands, auto& active)
                                             {
                                                 return draw(commands, active, true);
                                             });
        if (!skyPass)
            return skyPass;
        std::vector<RenderGraph::Use> lightingUses {{inputs.frame, Usage::eConstantRead},
                                                    {inputs.hdr, Usage::eColorReadWrite}};
        for (auto resource : inputs.gbuffer)
            lightingUses.push_back({resource, Usage::eSampled});
        if (pipeline.clustered())
            lightingUses.push_back({inputs.clusters, Usage::eStorageRead});
        appendLighting(lightingUses);
        return graph.addPassExpected(name + ".shade",
                                     lightingUses,
                                     [draw](auto* commands, auto& active)
                                     {
                                         return draw(commands, active, false);
                                     });
    }
} // namespace vultra
