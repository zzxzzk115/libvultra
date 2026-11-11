#include "colored_mesh.hpp"

#include <vultra/gpu/buffer_upload.hpp>

#include <algorithm>
#include <cstddef>

namespace sample
{
    ColoredMesh::ColoredMesh(vultra::Device&    owner,
                             vultra::Buffer     vertexBuffer,
                             vultra::Buffer     indexBuffer,
                             uint32_t           count,
                             VriPipelineLayout* pipelineLayout) :
        device(owner),
        vertices(std::move(vertexBuffer)),
        indices(std::move(indexBuffer)),
        indexCount(count),
        layout(pipelineLayout)
    {
    }

    std::expected<std::unique_ptr<ColoredMesh>, vultra::Error>
    ColoredMesh::create(vultra::Device&                device,
                        VriFormat                      format,
                        std::span<const ColoredVertex> vertexData,
                        std::span<const uint32_t>      indexData,
                        VriPrimitiveTopology           topology,
                        bool                           depthTest,
                        const vultra::ShaderProgram*   program)
    {
        auto vertexBuffer = vultra::uploadBuffer(device,
                                                 std::as_bytes(vertexData),
                                                 VriBufferUsage_VertexBuffer,
                                                 {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
        if (!vertexBuffer)
        {
            return std::unexpected(vertexBuffer.error());
        }
        auto indexBuffer = vultra::uploadBuffer(device,
                                                std::as_bytes(indexData),
                                                VriBufferUsage_IndexBuffer,
                                                {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
        if (!indexBuffer)
        {
            return std::unexpected(indexBuffer.error());
        }
        VriPushConstantDesc   push {0, sizeof(Parameters), VriShaderStage_Vertex | VriShaderStage_Fragment};
        VriPipelineLayoutDesc layoutDesc {};
        layoutDesc.pushConstants   = &push;
        layoutDesc.pushConstantNum = 1;
        layoutDesc.shaderStages    = push.shaderStages;
        VriPipelineLayout* layout  = nullptr;
        if (auto created = vultra::check(device.core.CreatePipelineLayout(device.handle, &layoutDesc, &layout),
                                         "Create colored mesh layout");
            !created)
        {
            return std::unexpected(created.error());
        }
        // From here the mesh owns the layout and releases it if the pipeline fails.
        std::unique_ptr<ColoredMesh> mesh(new ColoredMesh(device,
                                                          std::move(*vertexBuffer),
                                                          std::move(*indexBuffer),
                                                          uint32_t(indexData.size()),
                                                          layout));

        const std::vector<vultra::ShaderEntry> entries {{"vertexMain", VriShaderStage_Vertex},
                                                        {"fragmentMain", VriShaderStage_Fragment}};
        const auto build = [self = mesh.get(), format, topology, depthTest](
                               std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, vultra::Error>
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
            desc.pipelineCache           = self->device.pipelineCache;
            desc.pipelineLayout          = self->layout;
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
            if (auto created =
                    vultra::check(self->device.core.CreateGraphicsPipeline(self->device.handle, &desc, &result),
                                  "Create colored mesh pipeline");
                !created)
            {
                return std::unexpected(created.error());
            }
            return result;
        };
        auto pipeline =
            program ? vultra::ShaderPipeline::create(device, *program, entries, build) :
                      vultra::ShaderPipeline::create(device, "examples/common/colored_mesh.slang", entries, build);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        mesh->pipeline = std::move(*pipeline);
        return mesh;
    }

    ColoredMesh::~ColoredMesh()
    {
        pipeline.reset();
        device.core.DestroyPipelineLayout(layout);
    }

    std::expected<void, vultra::Error>
    ColoredMesh::draw(VriCommandBuffer* cmd, vultra::Texture& target, const float* clear, vultra::Texture* depth)
    {
        const auto targetView = target.view();
        if (!targetView)
        {
            return std::unexpected(targetView.error());
        }
        VriAttachmentDesc color {};
        color.view    = *targetView;
        color.loadOp  = clear ? VriAttachmentLoadOp_Clear : VriAttachmentLoadOp_Load;
        color.storeOp = VriAttachmentStoreOp_Store;
        if (clear)
        {
            std::copy_n(clear, 4, color.clearValue.color.f32);
        }
        VriAttachmentDesc depthAttachment {};
        if (depth)
        {
            const auto depthView = depth->view();
            if (!depthView)
            {
                return std::unexpected(depthView.error());
            }
            depthAttachment.view    = *depthView;
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
        VriVertexBufferBinding binding {vertices.handle, 0};
        device.core.CmdSetVertexBuffers(cmd, 0, &binding, 1);
        device.core.CmdSetIndexBuffer(cmd, indices.handle, 0, VriIndexType_UInt32);
        VriDrawIndexedDesc draw {indexCount, 1, 0, 0, 0};
        device.core.CmdDrawIndexed(cmd, &draw);
        device.core.CmdEndRendering(cmd);
        return {};
    }
} // namespace sample
