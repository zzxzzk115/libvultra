#include "vultra/function/renderer/builtin/passes/meshlet_depth_pre_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/depth_pre_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/function/renderer/shader_config/shader_config.hpp"

#include <shader_headers/depth_pre.frag.spv.h>
#include <shader_headers/meshlet.task.spv.h>
#include <shader_headers/meshlet_depth_pre.mesh.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "MeshletDepthPrePass";

        MeshletDepthPrePass::MeshletDepthPrePass(rhi::RenderDevice& rd) : rhi::RenderPass<MeshletDepthPrePass>(rd) {}

        void MeshletDepthPrePass::addPass(FrameGraph&            fg,
                                          FrameGraphBlackboard&  blackboard,
                                          const rhi::Extent2D&   resolution,
                                          const RenderableGroup& renderableGroup)
        {
            const auto& depthPreData = fg.addCallbackPass<DepthPreData>(
                PASS_NAME,
                [this, &fg, &blackboard, resolution](FrameGraph::Builder& builder, DepthPreData& data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());

                    data.depth = builder.create<framegraph::FrameGraphTexture>(
                        "DepthPre - Depth",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eDepth32F,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                          rhi::ImageUsage::eTransferSrc,
                        });
                    data.depth = builder.write(data.depth,
                                               framegraph::Attachment {
                                                   .imageAspect = rhi::ImageAspect::eDepth,
                                                   .clearValue  = framegraph::ClearValue::eOne,
                                               });
                },
                [this, &renderableGroup](const DepthPreData&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    gfx::BaseGeometryPassInfo passInfo {
                        .depthFormat  = rhi::getDepthFormat(*framebufferInfo),
                        .colorFormats = rhi::getColorFormats(*framebufferInfo),
                    };

                    cb.beginRendering(*framebufferInfo);

                    for (const auto& renderable : renderableGroup.renderables)
                    {
                        for (const auto& sm : renderable.mesh->renderMesh.subMeshes)
                        {
                            // Skip non-opaque meshes
                            if (sm.meshletCount == 0 || !sm.opaque)
                            {
                                continue;
                            }

                            struct GlobalMeshDataPushConstants
                            {
                                uint64_t vertexBufferAddress;
                                uint64_t meshletBufferAddress;
                                uint64_t meshletVertexBufferAddress;
                                uint64_t meshletTriangleBufferAddress;

                                uint32_t meshletCount;
                                uint32_t enableNormalMapping;
                                uint32_t padding0;
                                uint32_t padding1;

                                glm::mat4 modelMatrix;
                            };

                            GlobalMeshDataPushConstants pushConstants {
                                .vertexBufferAddress          = sm.vertexBufferAddress,
                                .meshletBufferAddress         = sm.meshletBufferAddress,
                                .meshletVertexBufferAddress   = sm.meshletVertexBufferAddress,
                                .meshletTriangleBufferAddress = sm.meshletTriangleBufferAddress,
                                .meshletCount                 = sm.meshletCount,
                                .enableNormalMapping          = false,
                                .modelMatrix                  = renderable.modelMatrix,
                            };

                            auto* pipeline = getPipeline(passInfo);

                            cb.bindPipeline(*pipeline);

                            rc.resourceSet[2][0] =
                                rhi::bindings::StorageBuffer {.buffer = renderable.mesh->materialBuffer.get()};
                            rc.bindDescriptorSets(*pipeline);

                            cb.pushConstants(rhi::ShaderStages::eTask | rhi::ShaderStages::eMesh, 0, &pushConstants)
                                .drawMeshTask({
                                    DISPATCH_SIZE_X(pushConstants.meshletCount),
                                    1,
                                    1,
                                });
                        }
                    }

                    rc.endRendering();
                });

            add(blackboard, depthPreData);
        }

        rhi::GraphicsPipeline MeshletDepthPrePass::createPipeline(const gfx::BaseGeometryPassInfo& passInfo) const
        {
            rhi::GraphicsPipeline::Builder builder {};

            builder.setDepthFormat(passInfo.depthFormat)
                .setColorFormats(passInfo.colorFormats)
                .setTopology(passInfo.topology)
#if USE_TASK_SHADER
                .addBuiltinShader(rhi::ShaderType::eTask, meshlet_task_spv)
#endif
                .addBuiltinShader(rhi::ShaderType::eMesh, meshlet_depth_pre_mesh_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, depth_pre_frag_spv)
                .setDepthStencil({
                    .depthTest      = true,
                    .depthWrite     = true,
                    .depthCompareOp = rhi::CompareOp::eLessOrEqual,
                })
                .setRasterizer({.polygonMode = rhi::PolygonMode::eFill, .cullMode = rhi::CullMode::eNone});
            for (auto i = 0; i < passInfo.colorFormats.size(); ++i)
            {
                builder.setBlending(i, {.enabled = false});
            }

            return builder.build(getRenderDevice());
        }
    } // namespace gfx
} // namespace vultra