#include <vultra/servers/rendering/builtin/visibility_graph.hpp>
#include <vultra/servers/rendering/builtin/visibility_requirements.hpp>
#include <vultra/servers/rendering/builtin/visibility_resolve_pipeline.hpp>
#include <vultra/servers/rendering/scene_vertex_pages.hpp>

#include <array>

namespace vultra
{
    std::expected<std::unique_ptr<VisibilityResolvePipeline>, std::string>
    VisibilityResolvePipeline::create(Device&                     device,
                                      const ShaderProgram&        program,
                                      size_t                      materialCount,
                                      uint64_t                    vertexBufferSize,
                                      VisibilityResolveMode       mode,
                                      bool                        clustered,
                                      const ShaderProgram*        uncoatedProgram,
                                      const ShaderProgram*        sceneLightsProgram,
                                      const VisibilityColorInput* colorInput)
    {
        const auto* caps = device.core.GetDeviceDesc(device.handle);
        if (!caps)
        {
            return std::unexpected("Device description is unavailable");
        }
        if (mode != VisibilityResolveMode::eAll && mode != VisibilityResolveMode::eHoles &&
            mode != VisibilityResolveMode::eDemand)
        {
            return std::unexpected("Invalid visibility resolve mode");
        }
        if (mode != VisibilityResolveMode::eAll &&
            (!caps->hasComputeShader ||
             !(device.core.GetFormatSupport(device.handle, VriFormat_RGBA16_SFLOAT) & VriFormatSupport_StorageTexture)))
        {
            return std::unexpected("Compute visibility shading requires compute and RGBA16F storage");
        }
        if (mode == VisibilityResolveMode::eDemand && !caps->hasShaderWaveOps)
        {
            return std::unexpected("Demand visibility shading requires shader wave operations");
        }
        if (colorInput && (mode != VisibilityResolveMode::eAll || !colorInput->belongsTo(device)))
        {
            return std::unexpected("Visibility color input requires a matching full resolve");
        }
        const auto pages = sceneVertexPages(*caps, vertexBufferSize, clustered ? 5 : 4);
        if (!pages)
        {
            return std::unexpected(pages.error());
        }
        const auto format  = device.core.GetFormatSupport(device.handle, VriFormat_RG32_UINT);
        const bool batched = !validateVisibilityRequirements(*caps, format, materialCount);
        if (batched && mode == VisibilityResolveMode::eDemand)
        {
            return std::unexpected("Demand visibility shading requires a full bindless material table");
        }
        if (batched)
        {
            if (auto requirements = validateVisibilityBatchedRequirements(*caps, format, materialCount); !requirements)
            {
                return std::unexpected(requirements.error());
            }
        }
        const uint32_t                         slots  = batched ? 7u : uint32_t(materialCount * 7);
        const auto                             stages = VriShaderStage_Fragment | VriShaderStage_Compute;
        std::array<VriDescriptorRangeDesc, 12> lighting {};
        for (uint32_t i = 0; i < lighting.size(); ++i)
        {
            lighting[i] = {i, 1, VriDescriptorType_Texture, stages};
        }
        lighting[0].descriptorType  = VriDescriptorType_ConstantBuffer;
        lighting[10].descriptorType = VriDescriptorType_Sampler;
        std::array<VriDescriptorRangeDesc, 10> geometry {};
        for (uint32_t i = 0; i < geometry.size(); ++i)
        {
            geometry[i] = {i, 1, VriDescriptorType_StructuredBuffer, stages};
        }
        geometry[5].descriptorType = colorInput ? VriDescriptorType_InputAttachment : VriDescriptorType_Texture;
        if (colorInput)
        {
            geometry[5].shaderStages = VriShaderStage_Fragment;
        }
        geometry[0].descriptorNum  = pages->pageCount;
        geometry[0].flags          = VriDescriptorRange_Array;
        geometry[6].descriptorType = VriDescriptorType_StorageTexture;
        geometry[7].descriptorType = VriDescriptorType_Texture;
        geometry[8].descriptorType = VriDescriptorType_Texture;
        const VriDescriptorRangeDesc textures {0,
                                               slots,
                                               VriDescriptorType_Texture,
                                               stages,
                                               VriDescriptorRange_VariableSized};
        const VriDescriptorRangeDesc samplers {0,
                                               slots,
                                               VriDescriptorType_Sampler,
                                               stages,
                                               VriDescriptorRange_VariableSized};
        const std::array             sets {VriDescriptorSetDesc {0, lighting.data(), uint32_t(lighting.size())},
                               VriDescriptorSetDesc {1, geometry.data(), clustered ? 10u : 9u},
                               VriDescriptorSetDesc {2, &textures, 1},
                               VriDescriptorSetDesc {3, &samplers, 1}};
        const VriPushConstantDesc    push {0, 16, stages};
        const VriPipelineLayoutDesc  layoutDesc {sets.data(),
                                                uint32_t(sets.size()),
                                                &push,
                                                1,
                                                VriShaderStage_Vertex | stages};
        auto result                = std::unique_ptr<VisibilityResolvePipeline>(new VisibilityResolvePipeline(device));
        result->m_MaterialCount    = materialCount;
        result->m_VertexBufferSize = vertexBufferSize;
        result->m_VertexPageSize   = pages->pageSize;
        result->m_VertexPageCount  = pages->pageCount;
        result->m_Batched          = batched;
        result->m_Clustered        = clustered;
        result->m_Mode             = mode;
        result->m_ColorInput       = colorInput;
        const auto status          = device.core.CreatePipelineLayout(device.handle, &layoutDesc, &result->m_Layout);
        if (status != VriResult_Success)
        {
            return std::unexpected("Create visibility resolve layout failed: " + std::to_string(status));
        }
        if (!result->m_Layout)
        {
            return std::unexpected("Create visibility resolve layout returned null");
        }
        std::vector<ShaderEntry> entries;
        if (mode == VisibilityResolveMode::eHoles)
        {
            entries.push_back({batched ? "maskedBatchedMain" : "maskedMain", VriShaderStage_Compute});
        }
        else if (mode == VisibilityResolveMode::eDemand)
        {
            entries.push_back({"foveatedMain", VriShaderStage_Compute});
        }
        else
        {
            entries = {{"screenVertex", VriShaderStage_Vertex},
                       {batched ? "fragmentBatchedMain" : "fragmentMain", VriShaderStage_Fragment}};
        }
        auto buildPipeline = [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
        {
            if (mode != VisibilityResolveMode::eAll)
            {
                if (shaders.size() != 1)
                {
                    return std::unexpected("Compute visibility shading requires one compute entry");
                }
                const VriComputePipelineDesc desc {result->m_Layout, shaders[0], device.pipelineCache};
                VriPipeline*                 handle  = nullptr;
                const auto                   created = device.core.CreateComputePipeline(device.handle, &desc, &handle);
                if (created != VriResult_Success || !handle)
                {
                    if (handle)
                    {
                        device.core.DestroyPipeline(handle);
                    }
                    return std::unexpected("Create compute visibility pipeline failed: " + std::to_string(created));
                }
                return handle;
            }
            VriColorAttachmentDesc color {};
            color.format         = VriFormat_RGBA16_SFLOAT;
            color.colorWriteMask = VriColorWrite_RGBA;
            VriGraphicsPipelineDesc desc {};
            desc.pipelineCache           = device.pipelineCache;
            desc.pipelineLayout          = result->m_Layout;
            desc.shaders                 = shaders.data();
            desc.shaderNum               = uint32_t(shaders.size());
            desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
            desc.rasterization.cullMode  = VriCullMode_None;
            desc.rasterization.lineWidth = 1;
            desc.multisample.sampleNum   = 1;
            desc.outputMerger.colors     = &color;
            desc.outputMerger.colorNum   = 1;
            VriPipeline* handle          = nullptr;
            const auto   created         = colorInput ? colorInput->commands().CreatePipeline(colorInput->pass(),
                                                                                    VriColorInputPhase_Resolve,
                                                                                    &desc,
                                                                                    &handle) :
                                                        device.core.CreateGraphicsPipeline(device.handle, &desc, &handle);
            if (created != VriResult_Success)
            {
                if (handle)
                {
                    device.core.DestroyPipeline(handle);
                }
                return std::unexpected("Create visibility resolve pipeline failed: " + std::to_string(created));
            }
            return handle;
        };
        auto pipeline = ShaderPipeline::create(device, program, entries, buildPipeline);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        result->m_Pipeline = std::move(*pipeline);
        if (uncoatedProgram)
        {
            auto uncoated = ShaderPipeline::create(device, *uncoatedProgram, entries, buildPipeline);
            if (!uncoated)
            {
                return std::unexpected("Create uncoated visibility pipeline: " + uncoated.error());
            }
            result->m_UncoatedPipeline = std::move(*uncoated);
        }
        if (sceneLightsProgram)
        {
            auto sceneLights = ShaderPipeline::create(device, *sceneLightsProgram, entries, buildPipeline);
            if (!sceneLights)
            {
                return std::unexpected("Create scene-light visibility pipeline: " + sceneLights.error());
            }
            result->m_SceneLightsPipeline = std::move(*sceneLights);
        }
        return result;
    }

    VisibilityResolvePipeline::~VisibilityResolvePipeline()
    {
        m_SceneLightsPipeline.reset();
        m_UncoatedPipeline.reset();
        m_Pipeline.reset();
        if (m_Layout)
        {
            m_Device.core.DestroyPipelineLayout(m_Layout);
        }
    }
} // namespace vultra
