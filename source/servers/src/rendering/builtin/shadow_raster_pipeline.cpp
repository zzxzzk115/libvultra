#include <vultra/assets/scene_data.hpp>
#include <vultra/servers/rendering/builtin/shadow_raster_pipeline.hpp>

#include <cstddef>

namespace vultra
{
    std::expected<std::unique_ptr<ShadowRasterPipeline>, std::string>
    ShadowRasterPipeline::create(Device& device, VriPipelineLayout* layout, const ShaderProgram& program)
    {
        if (!layout)
        {
            return std::unexpected("Shadow raster pipeline requires a layout");
        }
        auto result = std::unique_ptr<ShadowRasterPipeline>(new ShadowRasterPipeline(device, layout));
        for (uint32_t variant = 0; variant < result->m_Pipelines.size(); ++variant)
        {
            std::vector<ShaderEntry> entries {{"vertexMain", VriShaderStage_Vertex}};
            if (variant & 4)
            {
                entries.push_back({"fragmentMain", VriShaderStage_Fragment});
            }
            auto pipeline = ShaderPipeline::create(
                device,
                program,
                std::move(entries),
                [&, variant](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
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
                    desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
                    desc.depthStencil.depthTest          = VRI_TRUE;
                    desc.depthStencil.depthWrite         = VRI_TRUE;
                    desc.depthStencil.depthCompareOp     = VriCompareOp_Less;
                    VriPipeline* handle                  = nullptr;
                    const auto   status = device.core.CreateGraphicsPipeline(device.handle, &desc, &handle);
                    if (status != VriResult_Success || !handle)
                    {
                        if (handle)
                        {
                            device.core.DestroyPipeline(handle);
                        }
                        return std::unexpected("Create shadow raster pipeline failed: " + std::to_string(status));
                    }
                    return handle;
                });
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            result->m_Pipelines[variant] = std::move(*pipeline);
        }
        return result;
    }

    VriPipeline* ShadowRasterPipeline::handle(bool doubleSided, bool mirrored, bool alphaMasked) const
    {
        return m_Pipelines[uint32_t(doubleSided) | (uint32_t(mirrored) << 1) | (uint32_t(alphaMasked) << 2)]->handle();
    }

    std::expected<void, std::string> recordShadowRaster(Device&                               device,
                                                        const ShadowRasterPipeline&           pipeline,
                                                        VriCommandBuffer*                     commands,
                                                        uint32_t                              cascade,
                                                        Texture&                              depth,
                                                        Buffer&                               vertices,
                                                        Buffer&                               indices,
                                                        VriDescriptorSet*                     frameSet,
                                                        std::span<VriDescriptorSet* const>    materialSets,
                                                        std::span<const VisibilityRasterDraw> draws)
    {
        static_assert(sizeof(VisibilityRasterMaterial) == 128);
        if (!pipeline.belongsTo(device) || !commands || !frameSet || cascade >= 4 || !depth.belongsTo(device) ||
            !vertices.belongsTo(device) || !indices.belongsTo(device) || !vertices.handle || !indices.handle ||
            depth.desc.format != VriFormat_D32_SFLOAT || depth.desc.type != VriTextureType_2D || !depth.desc.width ||
            !depth.desc.height || depth.desc.depth != 1 || depth.desc.layerNum != 1 || depth.desc.mipNum != 1 ||
            depth.desc.sampleNum != 1 || !(depth.desc.usage & VriTextureUsage_DepthStencilAttachment) ||
            !(vertices.desc.usage & VriBufferUsage_VertexBuffer) || !(indices.desc.usage & VriBufferUsage_IndexBuffer))
        {
            return std::unexpected("Invalid shadow raster resources");
        }
        for (const auto& draw : draws)
        {
            if (draw.materialIndex >= materialSets.size() || !materialSets[draw.materialIndex] ||
                (!draw.alphaMasked && draw.material.flags.x > 0) || draw.material.instance.x == UINT32_MAX ||
                draw.indexCount % 3 != 0 ||
                uint64_t(draw.firstIndex) + draw.indexCount > indices.desc.size / sizeof(uint32_t))
            {
                return std::unexpected("Invalid shadow indexed draw");
            }
        }
        auto depthView = depth.viewExpected();
        if (!depthView)
        {
            return std::unexpected(depthView.error());
        }
        vertices.transition(commands, {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
        indices.transition(commands, {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
        depth.transition(commands,
                         {VriAccess_DepthStencilAttachmentWrite,
                          VriLayout_DepthStencilAttachment,
                          VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
        VriAttachmentDesc z {};
        z.view                          = *depthView;
        z.loadOp                        = VriAttachmentLoadOp_Clear;
        z.storeOp                       = VriAttachmentStoreOp_Store;
        z.clearValue.depthStencil.depth = 1;
        VriAttachmentsDesc attachments {};
        attachments.depth      = &z;
        attachments.renderArea = {0, 0, depth.desc.width, depth.desc.height};
        attachments.layerNum   = 1;
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
            auto* selected = pipeline.handle(draw.doubleSided, draw.mirrored, draw.alphaMasked);
            if (selected != active)
            {
                device.core.CmdSetPipeline(commands, selected);
                device.core.CmdSetDescriptorSet(commands, 0, frameSet);
                active = selected;
            }
            auto material    = draw.material;
            material.flags.z = float(cascade);
            device.core.CmdSetDescriptorSet(commands, 1, materialSets[draw.materialIndex]);
            device.core.CmdSetConstants(commands, 0, &material, sizeof(material));
            const VriDrawIndexedDesc indexed {draw.indexCount, 1, draw.firstIndex, 0, 0};
            device.core.CmdDrawIndexed(commands, &indexed);
        }
        device.core.CmdEndRendering(commands);
        return {};
    }
} // namespace vultra
