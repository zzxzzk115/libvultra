#include "colored_mesh.hpp"
#include "upload.hpp"

#include <algorithm>
#include <cstddef>

namespace sample
{
    ColoredMesh::ColoredMesh(vultra::Device&                owner,
                             VriFormat                      format,
                             std::span<const ColoredVertex> vertexData,
                             std::span<const uint32_t>      indexData,
                             VriPrimitiveTopology           topology,
                             bool                           depthTest) :
        device(owner),
        indexCount(uint32_t(indexData.size()))
    {
        vertices = uploadBuffer(device,
                                std::as_bytes(vertexData),
                                VriBufferUsage_VertexBuffer,
                                {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
        indices  = uploadBuffer(device,
                               std::as_bytes(indexData),
                               VriBufferUsage_IndexBuffer,
                                {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
        VriPushConstantDesc   push {0, sizeof(Parameters), VriShaderStage_Vertex | VriShaderStage_Fragment};
        VriPipelineLayoutDesc layoutDesc {};
        layoutDesc.pushConstants   = &push;
        layoutDesc.pushConstantNum = 1;
        layoutDesc.shaderStages    = push.shaderStages;
        vultra::check(device.core.CreatePipelineLayout(device.handle, &layoutDesc, &layout),
                      "Create colored mesh layout");
        try
        {
            pipeline = std::make_unique<vultra::ShaderPipeline>(
                device,
                "examples/common/colored_mesh.slang",
                std::vector<vultra::ShaderEntry> {{"vertexMain", VriShaderStage_Vertex},
                                                  {"fragmentMain", VriShaderStage_Fragment}},
                [this, format, topology, depthTest](std::span<const VriShaderDesc> shaders)
                {
                    VriVertexStreamDesc    stream {sizeof(ColoredVertex), 0, VriVertexStepRate_PerVertex};
                    VriVertexAttributeDesc attributes[2] {};
                    attributes[0].format = VriFormat_RGB32_SFLOAT;
                    attributes[0].offset = offsetof(ColoredVertex, position);
                    attributes[1].format = VriFormat_RGB32_SFLOAT;
                    attributes[1].offset = offsetof(ColoredVertex, color);
                    VriColorAttachmentDesc color {};
                    color.format         = format;
                    color.colorWriteMask = VriColorWrite_RGBA;

                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineCache           = device.pipelineCache;
                    desc.pipelineLayout          = layout;
                    desc.shaders                 = shaders.data();
                    desc.shaderNum               = uint32_t(shaders.size());
                    desc.vertexInput             = {attributes, 2, &stream, 1};
                    desc.inputAssembly.topology  = topology;
                    desc.rasterization.cullMode  = VriCullMode_None;
                    desc.rasterization.lineWidth = 1;
                    desc.multisample.sampleNum   = 1;
                    desc.outputMerger.colors     = &color;
                    desc.outputMerger.colorNum   = 1;
                    if (depthTest)
                    {
                        desc.depthStencil.depthTest          = VRI_TRUE;
                        desc.depthStencil.depthWrite         = VRI_FALSE;
                        desc.depthStencil.depthCompareOp     = VriCompareOp_LessOrEqual;
                        desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
                    }
                    VriPipeline* result = nullptr;
                    vultra::check(device.core.CreateGraphicsPipeline(device.handle, &desc, &result),
                                  "Create colored mesh pipeline");
                    return result;
                });
        }
        catch (...)
        {
            device.core.DestroyPipelineLayout(layout);
            throw;
        }
    }

    ColoredMesh::~ColoredMesh()
    {
        pipeline.reset();
        device.core.DestroyPipelineLayout(layout);
    }

    void ColoredMesh::draw(VriCommandBuffer* cmd, vultra::Texture& target, const float* clear, vultra::Texture* depth)
    {
        VriAttachmentDesc color {};
        color.view    = target.view();
        color.loadOp  = clear ? VriAttachmentLoadOp_Clear : VriAttachmentLoadOp_Load;
        color.storeOp = VriAttachmentStoreOp_Store;
        if (clear)
        {
            std::copy_n(clear, 4, color.clearValue.color.f32);
        }
        VriAttachmentDesc depthAttachment {};
        if (depth)
        {
            depthAttachment.view    = depth->view();
            depthAttachment.loadOp  = VriAttachmentLoadOp_Load;
            depthAttachment.storeOp = VriAttachmentStoreOp_Store;
        }
        VriAttachmentsDesc attachments {};
        attachments.colors     = &color;
        attachments.colorNum   = 1;
        attachments.depth      = depth ? &depthAttachment : nullptr;
        attachments.renderArea = {0, 0, target.desc.width, target.desc.height};
        attachments.layerNum   = 1;
        device.core.CmdBeginRendering(cmd, &attachments);
        VriViewport viewport {0, 0, float(target.desc.width), float(target.desc.height), 0, 1};
        VriRect     scissor {0, 0, target.desc.width, target.desc.height};
        device.core.CmdSetViewports(cmd, &viewport, 1);
        device.core.CmdSetScissors(cmd, &scissor, 1);
        device.core.CmdSetPipelineLayout(cmd, layout);
        device.core.CmdSetPipeline(cmd, pipeline->handle());
        device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
        VriVertexBufferBinding binding {vertices->handle, 0};
        device.core.CmdSetVertexBuffers(cmd, 0, &binding, 1);
        device.core.CmdSetIndexBuffer(cmd, indices->handle, 0, VriIndexType_UInt32);
        VriDrawIndexedDesc draw {indexCount, 1, 0, 0, 0};
        device.core.CmdDrawIndexed(cmd, &draw);
        device.core.CmdEndRendering(cmd);
    }
} // namespace sample
