#include "vultra/function/renderer/builtin/passes/meshlet_generation_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/meshlet_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/meshlet.mesh.spv.h>
#include <shader_headers/meshlet.task.spv.h>
#include <shader_headers/meshlet_debug.frag.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "MeshletGenerationPass";

        MeshletGenerationPass::MeshletGenerationPass(rhi::RenderDevice& rd) : rhi::RenderPass<MeshletGenerationPass>(rd)
        {}

        void MeshletGenerationPass::addPass(FrameGraph&            fg,
                                            FrameGraphBlackboard&  blackboard,
                                            const rhi::Extent2D&   resolution,
                                            const RenderableGroup& renderableGroup)
        {
            const auto& meshletData = fg.addCallbackPass<MeshletData>(
                PASS_NAME,
                [this, &fg, &blackboard, resolution](FrameGraph::Builder& builder, MeshletData& data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());

                    data.debug = builder.create<framegraph::FrameGraphTexture>(
                        "Meshlet Debug",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.debug = builder.write(data.debug,
                                               framegraph::Attachment {
                                                   .index       = 0,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                                   .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                               });
                },
                [this, &renderableGroup](const MeshletData&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    gfx::BaseGeometryPassInfo passInfo {
                        .depthFormat  = rhi::getDepthFormat(*framebufferInfo),
                        .colorFormats = rhi::getColorFormats(*framebufferInfo),
                    };

                    cb.beginRendering(*framebufferInfo);

                    struct GlobalMeshDataPushConstants
                    {
                        uint64_t vertexBufferAddress;
                        uint64_t meshletBufferAddress;
                        uint64_t meshletVertexBufferAddress;
                        uint64_t meshletTriangleBufferAddress;
                        uint32_t meshletCount;
                    };

                    GlobalMeshDataPushConstants pushConstants {
                        .vertexBufferAddress =
                            getRenderDevice().getBufferDeviceAddress(*renderableGroup.globalVertexBuffer),
                        .meshletBufferAddress =
                            getRenderDevice().getBufferDeviceAddress(*renderableGroup.globalMeshletBuffer),
                        .meshletVertexBufferAddress =
                            getRenderDevice().getBufferDeviceAddress(*renderableGroup.globalMeshletVertexBuffer),
                        .meshletTriangleBufferAddress =
                            getRenderDevice().getBufferDeviceAddress(*renderableGroup.globalMeshletTriangleBuffer),
                        .meshletCount = static_cast<uint32_t>(renderableGroup.globalMeshletGroup.meshlets.size()),
                    };

                    auto* pipeline = getPipeline(passInfo);

                    cb.bindPipeline(*pipeline);

                    rc.resourceSet[2][0] = rhi::bindings::StorageBuffer {
                        .buffer = renderableGroup.globalMeshletBuffer.get(),
                    };
                    rc.resourceSet[2][1] = rhi::bindings::StorageBuffer {
                        .buffer = renderableGroup.globalMeshletVertexBuffer.get(),
                    };
                    rc.resourceSet[2][2] = rhi::bindings::StorageBuffer {
                        .buffer = renderableGroup.globalMeshletTriangleBuffer.get(),
                    };

                    rc.bindDescriptorSets(*pipeline);

                    cb.pushConstants(rhi::ShaderStages::eTask | rhi::ShaderStages::eMesh, 0, &pushConstants)
                        .drawMeshTask({
                            pushConstants.meshletCount,
                            1,
                            1,
                        });

                    rc.endRendering();
                });

            add(blackboard, meshletData);
        }

        rhi::GraphicsPipeline MeshletGenerationPass::createPipeline(const gfx::BaseGeometryPassInfo& passInfo) const
        {
            rhi::GraphicsPipeline::Builder builder {};

            builder.setColorFormats(passInfo.colorFormats)
                .setTopology(passInfo.topology)
                // .addBuiltinShader(rhi::ShaderType::eTask, meshlet_task_spv)
                .addBuiltinShader(rhi::ShaderType::eMesh, meshlet_mesh_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, meshlet_debug_frag_spv)
                .setDepthStencil({
                    .depthTest  = false,
                    .depthWrite = false,
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