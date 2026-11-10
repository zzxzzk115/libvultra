#include <vultra/assets/scene_data.hpp>
#include <vultra/servers/rendering/builtin/visibility_graph.hpp>

#include <cstddef>

namespace vultra
{
    namespace
    {
        // Fragment PrimitiveId is valid with either SPIR-V Geometry or Tessellation capability.
        // Slang emits Geometry; MoltenVK exposes tessellation but not geometry shaders.
        std::expected<void, std::string> useTessellationForFragmentPrimitiveId(CompiledShader& shader)
        {
            constexpr uint32_t opEntryPoint = 15, opCapability = 17, opDecorate = 71, opMemberDecorate = 72;
            constexpr uint32_t fragment = 4, builtIn = 11, primitiveId = 7, frontFacing = 17, geometry = 2, tessellation = 3;
            if (shader.words.size() < 5 || shader.words[0] != 0x07230203)
                return std::unexpected("Invalid visibility fragment SPIR-V");
            size_t geometryWord = 0;
            bool hasTessellation = false, hasPrimitiveId = false, fragmentOnly = true, hasEntry = false, otherBuiltIn = false;
            for (size_t offset = 5; offset < shader.words.size();)
            {
                const auto instruction = shader.words[offset];
                const auto count = instruction >> 16;
                const auto op = instruction & 0xffffu;
                if (!count || count > shader.words.size() - offset)
                    return std::unexpected("Malformed visibility fragment SPIR-V instruction");
                if (op == opCapability && count == 2)
                {
                    if (shader.words[offset + 1] == geometry)
                    {
                        if (geometryWord) return std::unexpected("Duplicate geometry capability in visibility fragment");
                        geometryWord = offset;
                    }
                    hasTessellation |= shader.words[offset + 1] == tessellation;
                }
                if (op == opEntryPoint && count >= 3)
                {
                    hasEntry = true;
                    fragmentOnly &= shader.words[offset + 1] == fragment;
                }
                uint32_t value = UINT32_MAX;
                if (op == opDecorate && count >= 4 && shader.words[offset + 2] == builtIn)
                    value = shader.words[offset + 3];
                if (op == opMemberDecorate && count >= 5 && shader.words[offset + 3] == builtIn)
                    value = shader.words[offset + 4];
                if (value != UINT32_MAX)
                {
                    hasPrimitiveId |= value == primitiveId;
                    otherBuiltIn |= value != primitiveId && value != frontFacing;
                }
                if (op >= 218 && op <= 221)
                    return std::unexpected("Geometry instructions are not supported by the visibility fragment fallback");
                offset += count;
            }
            if (!geometryWord) return {};
            if (!hasEntry || !fragmentOnly || !hasPrimitiveId || otherBuiltIn)
                return std::unexpected("Geometry capability is not solely for fragment PrimitiveId");
            if (hasTessellation)
                shader.words.erase(shader.words.begin() + geometryWord, shader.words.begin() + geometryWord + 2);
            else
                shader.words[geometryWord + 1] = tessellation;
            return {};
        }
    }

    std::expected<std::unique_ptr<VisibilityRasterPipeline>, std::string>
    VisibilityRasterPipeline::create(Device&                     device,
                                     VriPipelineLayout*          layout,
                                     const ShaderProgram&        program,
                                     const VisibilityColorInput* colorInput)
    {
        if (!layout || (colorInput && !colorInput->belongsTo(device)))
        {
            return std::unexpected("Visibility raster pipeline requires a layout");
        }
        const auto* caps = device.core.GetDeviceDesc(device.handle);
        if (!caps) return std::unexpected("Device description is unavailable");
        ShaderProgram compatible = program;
        if (caps->graphicsAPI == VriGraphicsAPI_Vulkan && program.profile.starts_with("spirv_") && !caps->hasGeometryShader)
        {
            if (!caps->hasTessellation)
                return std::unexpected("Visibility PrimitiveId requires Vulkan geometry or tessellation capability");
            for (auto& shader : compatible.shaders)
                if (shader.entry.stage == VriShaderStage_Fragment &&
                    (shader.entry.name == "fragmentMain" || shader.entry.name == "maskedFragmentMain"))
                    if (auto rewritten = useTessellationForFragmentPrimitiveId(shader); !rewritten)
                        return std::unexpected(rewritten.error());
        }
        auto result = std::unique_ptr<VisibilityRasterPipeline>(new VisibilityRasterPipeline(device, layout));
        result->m_ColorInput = colorInput;
        for (uint32_t variant = 0; variant < result->m_Pipelines.size(); ++variant)
        {
            const bool  alphaMasked   = (variant & 4) != 0;
            const char* vertexEntry   = alphaMasked ? "vertexMain" : "opaqueVertexMain";
            const char* fragmentEntry = alphaMasked ? "maskedFragmentMain" : "fragmentMain";
            auto        pipeline      = ShaderPipeline::create(
                device,
                compatible,
                {{vertexEntry, VriShaderStage_Vertex}, {fragmentEntry, VriShaderStage_Fragment}},
                [&, variant, alphaMasked](
                    std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    const VriVertexStreamDesc stream {sizeof(SceneVertex), 0, VriVertexStepRate_PerVertex};
                    VriVertexAttributeDesc    attributes[3] {};
                    attributes[0].format = VriFormat_RGB32_SFLOAT;
                    attributes[0].offset = offsetof(SceneVertex, position);
                    attributes[1].format = VriFormat_RG32_SFLOAT;
                    attributes[1].offset = offsetof(SceneVertex, uv);
                    attributes[2].format = VriFormat_RGBA32_SFLOAT;
                    attributes[2].offset = offsetof(SceneVertex, color);
                    VriColorAttachmentDesc color {};
                    color.format = VriFormat_RG32_UINT;
                    color.colorWriteMask = VriColorWrite_RGBA;
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineCache = device.pipelineCache;
                    desc.pipelineLayout = layout;
                    desc.shaders = shaders.data();
                    desc.shaderNum = uint32_t(shaders.size());
                    desc.vertexInput = {attributes, alphaMasked ? 3u : 1u, &stream, 1};
                    desc.inputAssembly.topology = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode = (variant & 1) ? VriCullMode_None : VriCullMode_Back;
                    desc.rasterization.frontFace = (variant & 2) ? VriFrontFace_Clockwise : VriFrontFace_CounterClockwise;
                    desc.rasterization.lineWidth = 1;
                    desc.multisample.sampleNum = 1;
                    desc.outputMerger.colors = &color;
                    desc.outputMerger.colorNum = 1;
                    desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
                    desc.depthStencil.depthTest = VRI_TRUE;
                    desc.depthStencil.depthWrite = VRI_TRUE;
                    desc.depthStencil.depthCompareOp = VriCompareOp_Less;
                    VriPipeline* handle = nullptr;
                    const auto   status = colorInput ? colorInput->commands().CreatePipeline(colorInput->pass(),
                                                                                           VriColorInputPhase_Raster,
                                                                                           &desc,
                                                                                           &handle) :
                                                       device.core.CreateGraphicsPipeline(device.handle, &desc, &handle);
                    if (status != VriResult_Success)
                    {
                        if (handle)
                            device.core.DestroyPipeline(handle);
                        return std::unexpected("Create visibility raster pipeline failed: " + std::to_string(status));
                    }
                    return handle;
                });
            if (!pipeline)
                return std::unexpected(pipeline.error());
            result->m_Pipelines[variant] = std::move(*pipeline);
        }
        return result;
    }

    VriPipeline* VisibilityRasterPipeline::handle(bool doubleSided, bool mirrored, bool alphaMasked) const
    {
        return m_Pipelines[uint32_t(doubleSided) | (uint32_t(mirrored) << 1) | (uint32_t(alphaMasked) << 2)]->handle();
    }

    std::expected<void, std::string> recordVisibilityRaster(
        Device& device, const VisibilityRasterPipeline& pipeline, VriCommandBuffer* commands,
        Texture& ids, Texture& depth, Buffer& vertices, Buffer& indices, VriDescriptorSet* frameSet,
        std::span<VriDescriptorSet* const> materialSets, std::span<const VisibilityRasterDraw> draws)
    {
        static_assert(sizeof(VisibilityRasterMaterial) == 128);
        if (!pipeline.belongsTo(device) || !commands || !frameSet || !ids.belongsTo(device) || !depth.belongsTo(device) ||
            !vertices.belongsTo(device) || !indices.belongsTo(device) || !vertices.handle || !indices.handle ||
            ids.desc.format != VriFormat_RG32_UINT || depth.desc.format != VriFormat_D32_SFLOAT ||
            ids.desc.type != VriTextureType_2D || depth.desc.type != VriTextureType_2D ||
            !ids.desc.width || !ids.desc.height || ids.desc.width != depth.desc.width || ids.desc.height != depth.desc.height ||
            ids.desc.layerNum != 1 || depth.desc.layerNum != 1 || ids.desc.sampleNum != 1 || depth.desc.sampleNum != 1 ||
            !(ids.desc.usage & VriTextureUsage_ColorAttachment) || !(depth.desc.usage & VriTextureUsage_DepthStencilAttachment) ||
            !(vertices.desc.usage & VriBufferUsage_VertexBuffer) || !(indices.desc.usage & VriBufferUsage_IndexBuffer))
            return std::unexpected("Invalid visibility raster resources");
        for (const auto& draw : draws)
        {
            if (draw.materialIndex >= materialSets.size() || !materialSets[draw.materialIndex] ||
                draw.material.instance.x == UINT32_MAX ||
                draw.indexCount % 3 != 0 || uint64_t(draw.firstIndex) + draw.indexCount > indices.desc.size / sizeof(uint32_t))
                return std::unexpected("Invalid visibility indexed draw");
        }
        auto idView = ids.viewExpected();
        if (!idView) return std::unexpected(idView.error());
        auto depthView = depth.viewExpected();
        if (!depthView) return std::unexpected(depthView.error());
        const auto* local = pipeline.colorInput();
        if (local)
        {
            // Both subpasses access the same buffers. Publish the combined state before beginning rendering.
            constexpr auto stages = VriPipelineStage_VertexInput | VriPipelineStage_FragmentShader;
            if (auto ready =
                    vertices.transitionExpected(commands,
                                                {VriAccess_VertexBufferRead | VriAccess_ShaderResourceRead, stages});
                !ready)
            {
                return ready;
            }
            if (auto ready =
                    indices.transitionExpected(commands,
                                               {VriAccess_IndexBufferRead | VriAccess_ShaderResourceRead, stages});
                !ready)
            {
                return ready;
            }
        }
        else
        {
            vertices.transition(commands, {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
            indices.transition(commands, {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
        }
        ids.transition(commands, {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        depth.transition(commands, {VriAccess_DepthStencilAttachmentWrite, VriLayout_DepthStencilAttachment,
                                    VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
        VriAttachmentDesc color {};
        color.view = *idView;
        color.loadOp = VriAttachmentLoadOp_Clear;
        color.storeOp = VriAttachmentStoreOp_Store;
        VriAttachmentDesc z {};
        z.view = *depthView;
        z.loadOp = VriAttachmentLoadOp_Clear;
        z.storeOp = VriAttachmentStoreOp_Store;
        z.clearValue.depthStencil.depth = 1;
        VriAttachmentsDesc attachments {};
        attachments.colors = &color;
        attachments.colorNum = 1;
        attachments.depth = &z;
        attachments.renderArea = {0, 0, ids.desc.width, ids.desc.height};
        attachments.layerNum = 1;
        if (local)
        {
            const auto status = local->commands().CmdBegin(commands, local->target(), &color.clearValue, 1.f);
            if (status != VriResult_Success)
            {
                return std::unexpected("Begin visibility color input failed: " + std::to_string(status));
            }
        }
        else
        {
            device.core.CmdBeginRendering(commands, &attachments);
        }
        const VriViewport viewport {0, 0, float(ids.desc.width), float(ids.desc.height), 0, 1};
        const VriRect scissor {0, 0, ids.desc.width, ids.desc.height};
        device.core.CmdSetViewports(commands, &viewport, 1);
        device.core.CmdSetScissors(commands, &scissor, 1);
        device.core.CmdSetPipelineLayout(commands, pipeline.layout());
        const VriVertexBufferBinding vertex {vertices.handle, 0};
        device.core.CmdSetVertexBuffers(commands, 0, &vertex, 1);
        device.core.CmdSetIndexBuffer(commands, indices.handle, 0, VriIndexType_UInt32);
        VriPipeline*      active   = nullptr;
        VriDescriptorSet* material = nullptr;
        for (const auto& draw : draws)
        {
            auto* selected = pipeline.handle(draw.doubleSided, draw.mirrored, draw.alphaMasked);
            if (selected != active)
            {
                device.core.CmdSetPipeline(commands, selected);
                device.core.CmdSetDescriptorSet(commands, 0, frameSet);
                active   = selected;
                material = nullptr;
            }
            if (draw.alphaMasked)
            {
                auto* selectedMaterial = materialSets[draw.materialIndex];
                if (selectedMaterial != material)
                {
                    device.core.CmdSetDescriptorSet(commands, 1, selectedMaterial);
                    material = selectedMaterial;
                }
                device.core.CmdSetConstants(commands, 0, &draw.material, sizeof(draw.material));
            }
            const uint32_t           firstInstance = draw.alphaMasked ? 0 : draw.material.instance.x;
            const VriDrawIndexedDesc indexed {draw.indexCount, 1, draw.firstIndex, 0, firstInstance};
            device.core.CmdDrawIndexed(commands, &indexed);
        }
        if (!local)
        {
            device.core.CmdEndRendering(commands);
        }
        return {};
    }
}
