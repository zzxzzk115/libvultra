#include <vultra/assets/scene_data.hpp>
#include <vultra/servers/rendering/builtin/transparency_pipeline.hpp>

#include <algorithm>
#include <cstddef>

namespace vultra
{
    std::expected<std::unique_ptr<TransparencyPipeline>, std::string>
    TransparencyPipeline::create(Device&              device,
                                 VriPipelineLayout*   layout,
                                 const ShaderProgram& program,
                                 const ShaderProgram* uncoatedProgram,
                                 const ShaderProgram* sceneLightsProgram)
    {
        if (!layout)
        {
            return std::unexpected("Transparency pipeline requires a layout");
        }
        auto             result = std::unique_ptr<TransparencyPipeline>(new TransparencyPipeline(device, layout));
        const std::array programs {&program, uncoatedProgram, sceneLightsProgram};
        const std::array labels {"complete", "uncoated", "scene-light"};
        for (size_t group = 0; group < programs.size(); ++group)
        {
            if (!programs[group])
            {
                continue;
            }
            auto bindings = programs[group]->resourceBindingsExpected();
            if (!bindings)
            {
                return std::unexpected(bindings.error());
            }
            const auto depth = std::ranges::find(*bindings, "transparentOpaqueDepth", &ShaderResourceBinding::name);
            const bool sampledDepth = depth != bindings->end();
            if (sampledDepth && (depth->set != 0 || depth->binding != 9 || depth->count != 1 ||
                                 depth->type != VriDescriptorType_Texture || depth->viewType != VriTextureViewType_2D))
            {
                return std::unexpected("Transparency sampled depth must be a single 2D texture at set 0, binding 9");
            }
            if (group == 0)
            {
                result->m_UsesSampledDepth = sampledDepth;
            }
            else if (result->m_UsesSampledDepth != sampledDepth)
            {
                return std::unexpected("Transparency shader variants disagree on the depth source");
            }
        }
        for (uint32_t variant = 0; variant < result->m_Pipelines.size(); ++variant)
        {
            const auto group = variant / 8;
            if (!programs[group])
            {
                continue;
            }
            auto pipeline = ShaderPipeline::create(
                device,
                *programs[group],
                {{"vertexMain", VriShaderStage_Vertex}, {"fragmentMain", VriShaderStage_Fragment}},
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
                    VriColorAttachmentDesc color {};
                    color.format = (variant & 4) ? VriFormat_RGBA32_SFLOAT : VriFormat_RGBA16_SFLOAT;
                    color.colorWriteMask =
                        (variant & 4) ? VriColorWrite_R | VriColorWrite_G | VriColorWrite_B : VriColorWrite_RGBA;
                    color.blend = {VRI_TRUE,
                                   VriBlendFactor_SrcAlpha,
                                   VriBlendFactor_OneMinusSrcAlpha,
                                   VriBlendOp_Add,
                                   VriBlendFactor_One,
                                   VriBlendFactor_OneMinusSrcAlpha,
                                   VriBlendOp_Add};
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
                    desc.outputMerger.colors             = &color;
                    desc.outputMerger.colorNum           = 1;
                    desc.outputMerger.depthStencilFormat =
                        result->m_UsesSampledDepth ? VriFormat_Unknown : VriFormat_D32_SFLOAT;
                    desc.depthStencil.depthTest          = !result->m_UsesSampledDepth;
                    desc.depthStencil.depthWrite         = VRI_FALSE;
                    desc.depthStencil.depthCompareOp     = VriCompareOp_LessOrEqual;
                    VriPipeline* handle                  = nullptr;
                    const auto   status = device.core.CreateGraphicsPipeline(device.handle, &desc, &handle);
                    if (status != VriResult_Success || !handle)
                    {
                        if (handle)
                        {
                            device.core.DestroyPipeline(handle);
                        }
                        return std::unexpected("Create transparency pipeline failed: " + std::to_string(status));
                    }
                    return handle;
                });
            if (!pipeline)
            {
                return std::unexpected("Create " + std::string(labels[group]) +
                                       " transparency pipeline: " + pipeline.error());
            }
            result->m_Pipelines[variant] = std::move(*pipeline);
        }
        return result;
    }

    std::expected<void, std::string> TransparencyPipeline::record(VriCommandBuffer*                     commands,
                                                                  Texture&                              hdr,
                                                                  Texture&                              depth,
                                                                  Buffer&                               vertices,
                                                                  Buffer&                               indices,
                                                                  VriDescriptorSet*                     frameSet,
                                                                  std::span<VriDescriptorSet* const>    materialSets,
                                                                  std::span<const VisibilityRasterDraw> sortedDraws,
                                                                  bool sceneLightsOnly) const
    {
        if (!commands || !frameSet || !hdr.belongsTo(m_Device) || !depth.belongsTo(m_Device) ||
            !vertices.belongsTo(m_Device) || !indices.belongsTo(m_Device) ||
            (hdr.desc.format != VriFormat_RGBA16_SFLOAT && hdr.desc.format != VriFormat_RGBA32_SFLOAT) ||
            depth.desc.format != VriFormat_D32_SFLOAT || hdr.desc.width != depth.desc.width ||
            hdr.desc.height != depth.desc.height || !hdr.desc.width || !hdr.desc.height ||
            !(hdr.desc.usage & VriTextureUsage_ColorAttachment) ||
            !(depth.desc.usage &
              (m_UsesSampledDepth ? VriTextureUsage_ShaderResource : VriTextureUsage_DepthStencilAttachment)))
        {
            return std::unexpected("Invalid transparency attachments or descriptors");
        }
        for (const auto& draw : sortedDraws)
        {
            if (draw.materialIndex >= materialSets.size() || !materialSets[draw.materialIndex] || draw.indexCount % 3 ||
                uint64_t(draw.firstIndex) + draw.indexCount > indices.desc.size / sizeof(uint32_t))
            {
                return std::unexpected("Invalid transparent geometry or material");
            }
        }
        auto colorView = hdr.viewExpected();
        if (!colorView)
        {
            return std::unexpected(colorView.error());
        }
        auto depthView = depth.viewExpected();
        if (!depthView)
        {
            return std::unexpected(depthView.error());
        }
        vertices.transition(commands, {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
        indices.transition(commands, {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
        VriAttachmentDesc color {};
        color.view    = *colorView;
        color.loadOp  = VriAttachmentLoadOp_Load;
        color.storeOp = VriAttachmentStoreOp_Store;
        VriAttachmentDesc z {};
        if (m_UsesSampledDepth)
        {
            depth.transition(commands,
                             {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
            const VriDescriptor*               view = *depthView;
            const VriDescriptorRangeUpdateDesc update {&view, 1};
            m_Device.core.UpdateDescriptorRanges(frameSet, 9, 1, &update);
        }
        else
        {
            depth.transition(commands,
                             {VriAccess_DepthStencilAttachmentRead,
                              VriLayout_DepthStencilReadOnly,
                              VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
            z.view     = *depthView;
            z.loadOp   = VriAttachmentLoadOp_Load;
            z.storeOp  = VriAttachmentStoreOp_Store;
            z.readOnly = VRI_TRUE;
        }
        VriAttachmentsDesc attachments {};
        attachments.colors     = &color;
        attachments.colorNum   = 1;
        attachments.depth      = m_UsesSampledDepth ? nullptr : &z;
        attachments.renderArea = {0, 0, hdr.desc.width, hdr.desc.height};
        attachments.layerNum   = 1;
        m_Device.core.CmdBeginRendering(commands, &attachments);
        const VriViewport viewport {0, 0, float(hdr.desc.width), float(hdr.desc.height), 0, 1};
        const VriRect     scissor {0, 0, hdr.desc.width, hdr.desc.height};
        m_Device.core.CmdSetViewports(commands, &viewport, 1);
        m_Device.core.CmdSetScissors(commands, &scissor, 1);
        m_Device.core.CmdSetPipelineLayout(commands, m_Layout);
        const VriVertexBufferBinding vertex {vertices.handle, 0};
        m_Device.core.CmdSetVertexBuffers(commands, 0, &vertex, 1);
        m_Device.core.CmdSetIndexBuffer(commands, indices.handle, 0, VriIndexType_UInt32);
        VriPipeline*      active   = nullptr;
        VriDescriptorSet* material = nullptr;
        for (const auto& draw : sortedDraws)
        {
            uint32_t group = 0;
            if (draw.material.coat.x == 0)
            {
                if (sceneLightsOnly && m_Pipelines[16])
                {
                    group = 2;
                }
                else if (m_Pipelines[8])
                {
                    group = 1;
                }
            }
            const auto variant = uint32_t(draw.doubleSided) | (uint32_t(draw.mirrored) << 1) |
                                 (uint32_t(hdr.desc.format == VriFormat_RGBA32_SFLOAT) << 2) | (group * 8);
            auto* selected = m_Pipelines[variant]->handle();
            if (selected != active)
            {
                m_Device.core.CmdSetPipeline(commands, selected);
                m_Device.core.CmdSetDescriptorSet(commands, 0, frameSet);
                active   = selected;
                material = nullptr;
            }
            auto* selectedMaterial = materialSets[draw.materialIndex];
            if (selectedMaterial != material)
            {
                m_Device.core.CmdSetDescriptorSet(commands, 1, selectedMaterial);
                material = selectedMaterial;
            }
            m_Device.core.CmdSetConstants(commands, 0, &draw.material, sizeof(draw.material));
            const VriDrawIndexedDesc indexed {draw.indexCount, 1, draw.firstIndex, 0, 0};
            m_Device.core.CmdDrawIndexed(commands, &indexed);
        }
        m_Device.core.CmdEndRendering(commands);
        return {};
    }
} // namespace vultra
