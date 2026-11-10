#include <vultra/assets/scene_data.hpp>
#include <vultra/servers/rendering/builtin/gbuffer_pipeline.hpp>

#include <cstddef>

namespace vultra
{
    std::expected<std::unique_ptr<GBufferPipeline>, std::string>
    GBufferPipeline::create(Device&                 device,
                            VriPipelineLayout*      layout,
                            const ShaderProgram&    program,
                            bool                    singlePass,
                            GBufferPositionEncoding positionEncoding)
    {
        if (!layout)
            return std::unexpected("G-buffer pipeline requires a layout");
        const auto* caps = device.core.GetDeviceDesc(device.handle);
        if (singlePass && (!caps || caps->attachmentColorMaxNum < kGBufferFormats.size()))
            return std::unexpected("Single-pass G-buffer requires seven color attachments");
        const auto     formats  = gbufferFormats(positionEncoding);
        constexpr auto required = VriFormatSupport_Texture | VriFormatSupport_ColorAttachment;
        if ((device.core.GetFormatSupport(device.handle, formats[0]) & required) != required)
            return std::unexpected("G-buffer position format is not renderable and sampleable");
        auto result =
            std::unique_ptr<GBufferPipeline>(new GBufferPipeline(device, layout, singlePass, positionEncoding));
        for (uint32_t stage = singlePass ? 2 : 0; stage < (singlePass ? 3u : 2u); ++stage)
        {
            for (uint32_t variant = 0; variant < 4; ++variant)
            {
                auto pipeline = ShaderPipeline::create(
                    device,
                    program,
                    {{"vertexMain", VriShaderStage_Vertex},
                     {stage == 0 ? "fragmentBase" :
                      stage == 1 ? "fragmentMaterial" :
                                   "fragmentCombined",
                      VriShaderStage_Fragment}},
                    [&, stage, variant](
                        std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                    {
                        const VriVertexStreamDesc stream {sizeof(SceneVertex), 0, VriVertexStepRate_PerVertex};
                        VriVertexAttributeDesc    attributes[5] {};
                        attributes[0].format = VriFormat_RGB32_SFLOAT;
                        attributes[0].offset = offsetof(SceneVertex, position);
                        attributes[1].format = VriFormat_RGB32_SFLOAT;
                        attributes[1].offset = offsetof(SceneVertex, normal);
                        attributes[2].format = VriFormat_RG32_SFLOAT;
                        attributes[2].offset = offsetof(SceneVertex, uv);
                        attributes[3].format = VriFormat_RGBA32_SFLOAT;
                        attributes[3].offset = offsetof(SceneVertex, color);
                        attributes[4].format = VriFormat_RGBA32_SFLOAT;
                        attributes[4].offset = offsetof(SceneVertex, tangent);
                        std::array<VriColorAttachmentDesc, 7> colors {};
                        const uint32_t                        count = stage == 0 ? 4 : stage == 1 ? 3 : 7;
                        for (uint32_t i = 0; i < count; ++i)
                        {
                            colors[i].format         = formats[i + (stage == 1 ? 4 : 0)];
                            colors[i].colorWriteMask = VriColorWrite_RGBA;
                        }
                        VriGraphicsPipelineDesc desc {};
                        desc.pipelineCache          = device.pipelineCache;
                        desc.pipelineLayout         = layout;
                        desc.shaders                = shaders.data();
                        desc.shaderNum              = uint32_t(shaders.size());
                        desc.vertexInput            = {attributes, 5, &stream, 1};
                        desc.inputAssembly.topology = VriPrimitiveTopology_TriangleList;
                        desc.rasterization.cullMode = (variant & 1) ? VriCullMode_None : VriCullMode_Back;
                        desc.rasterization.frontFace =
                            (variant & 2) ? VriFrontFace_Clockwise : VriFrontFace_CounterClockwise;
                        desc.rasterization.lineWidth         = 1;
                        desc.multisample.sampleNum           = 1;
                        desc.outputMerger.colors             = colors.data();
                        desc.outputMerger.colorNum           = count;
                        desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
                        desc.depthStencil.depthTest          = VRI_TRUE;
                        desc.depthStencil.depthWrite         = stage != 1 ? VRI_TRUE : VRI_FALSE;
                        desc.depthStencil.depthCompareOp     = stage != 1 ? VriCompareOp_Less : VriCompareOp_Equal;
                        VriPipeline* handle                  = nullptr;
                        const auto   status = device.core.CreateGraphicsPipeline(device.handle, &desc, &handle);
                        if (status != VriResult_Success || !handle)
                        {
                            if (handle)
                                device.core.DestroyPipeline(handle);
                            return std::unexpected("Create G-buffer pipeline failed: " + std::to_string(status));
                        }
                        return handle;
                    });
                if (!pipeline)
                    return std::unexpected(pipeline.error());
                result->m_Pipelines[stage * 4 + variant] = std::move(*pipeline);
            }
        }
        return result;
    }

    VriPipeline* GBufferPipeline::handle(uint32_t stage, bool doubleSided, bool mirrored) const
    {
        if (stage >= 3 || (m_SinglePass ? stage != 2 : stage == 2))
            return nullptr;
        return m_Pipelines[stage * 4 + uint32_t(doubleSided) + 2 * uint32_t(mirrored)]->handle();
    }

    std::expected<void, std::string> recordGBufferGeometry(Device&                               device,
                                                           const GBufferPipeline&                pipeline,
                                                           VriCommandBuffer*                     commands,
                                                           uint32_t                              stage,
                                                           const std::array<Texture*, 7>&        targets,
                                                           Texture&                              depth,
                                                           Buffer&                               vertices,
                                                           Buffer&                               indices,
                                                           VriDescriptorSet*                     frameSet,
                                                           std::span<VriDescriptorSet* const>    materialSets,
                                                           std::span<const VisibilityRasterDraw> draws)
    {
        if (!commands || !pipeline.belongsTo(device) || !pipeline.handle(stage, false, false) || !frameSet ||
            !depth.belongsTo(device) || !vertices.belongsTo(device) || !indices.belongsTo(device) ||
            depth.desc.format != VriFormat_D32_SFLOAT || depth.desc.type != VriTextureType_2D ||
            depth.desc.sampleNum != 1 || depth.desc.layerNum != 1 || !depth.desc.width || !depth.desc.height ||
            !(depth.desc.usage & VriTextureUsage_DepthStencilAttachment) ||
            !(vertices.desc.usage & VriBufferUsage_VertexBuffer) || !(indices.desc.usage & VriBufferUsage_IndexBuffer))
            return std::unexpected("Invalid G-buffer geometry resources");
        const uint32_t count   = stage == 0 ? 4 : stage == 1 ? 3 : 7;
        const uint32_t first   = stage == 1 ? 4 : 0;
        const auto     formats = gbufferFormats(pipeline.positionEncoding());
        for (uint32_t i = first; i < first + count; ++i)
        {
            const auto* texture = targets[i];
            if (!texture || !texture->belongsTo(device) || texture->desc.format != formats[i] ||
                texture->desc.type != VriTextureType_2D || texture->desc.width != depth.desc.width ||
                texture->desc.height != depth.desc.height || texture->desc.layerNum != 1 ||
                texture->desc.sampleNum != 1 || !(texture->desc.usage & VriTextureUsage_ColorAttachment))
                return std::unexpected("Invalid G-buffer color attachment");
            for (uint32_t j = first; j < i; ++j)
                if (texture == targets[j])
                    return std::unexpected("G-buffer color attachments must be distinct");
        }
        for (const auto& draw : draws)
            if (draw.materialIndex >= materialSets.size() || !materialSets[draw.materialIndex] ||
                draw.material.instance.x == UINT32_MAX || draw.indexCount % 3 ||
                uint64_t(draw.firstIndex) + draw.indexCount > indices.desc.size / sizeof(uint32_t))
                return std::unexpected("Invalid G-buffer draw");
        std::array<VriAttachmentDesc, 7> colors {};
        for (uint32_t i = 0; i < count; ++i)
        {
            auto view = targets[first + i]->viewExpected();
            if (!view)
                return std::unexpected(view.error());
            colors[i].view    = *view;
            colors[i].loadOp  = VriAttachmentLoadOp_Clear;
            colors[i].storeOp = VriAttachmentStoreOp_Store;
        }
        if (stage != 1)
            colors[0].clearValue.color.f32[pipeline.positionEncoding() == GBufferPositionEncoding::eDepth32 ? 1 : 3] =
                -1;
        auto depthView = depth.viewExpected();
        if (!depthView)
            return std::unexpected(depthView.error());
        VriAttachmentDesc z {};
        z.view                          = *depthView;
        z.loadOp                        = stage != 1 ? VriAttachmentLoadOp_Clear : VriAttachmentLoadOp_Load;
        z.storeOp                       = VriAttachmentStoreOp_Store;
        z.clearValue.depthStencil.depth = 1;
        VriAttachmentsDesc attachments {};
        attachments.colors     = colors.data();
        attachments.colorNum   = count;
        attachments.depth      = &z;
        attachments.renderArea = {0, 0, depth.desc.width, depth.desc.height};
        attachments.layerNum   = 1;
        vertices.transition(commands, {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
        indices.transition(commands, {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
        device.core.CmdBeginRendering(commands, &attachments);
        const VriViewport viewport {0, 0, float(depth.desc.width), float(depth.desc.height), 0, 1};
        const VriRect     scissor {0, 0, depth.desc.width, depth.desc.height};
        device.core.CmdSetViewports(commands, &viewport, 1);
        device.core.CmdSetScissors(commands, &scissor, 1);
        device.core.CmdSetPipelineLayout(commands, pipeline.layout());
        const VriVertexBufferBinding vertex {vertices.handle, 0};
        device.core.CmdSetVertexBuffers(commands, 0, &vertex, 1);
        device.core.CmdSetIndexBuffer(commands, indices.handle, 0, VriIndexType_UInt32);
        VriPipeline* active = nullptr;
        for (const auto& draw : draws)
        {
            auto* selected = pipeline.handle(stage, draw.doubleSided, draw.mirrored);
            if (selected != active)
            {
                device.core.CmdSetPipeline(commands, selected);
                device.core.CmdSetDescriptorSet(commands, 0, frameSet);
                active = selected;
            }
            device.core.CmdSetDescriptorSet(commands, 1, materialSets[draw.materialIndex]);
            device.core.CmdSetConstants(commands, 0, &draw.material, sizeof(draw.material));
            const VriDrawIndexedDesc indexed {draw.indexCount, 1, draw.firstIndex, 0, 0};
            device.core.CmdDrawIndexed(commands, &indexed);
        }
        device.core.CmdEndRendering(commands);
        return {};
    }
} // namespace vultra
