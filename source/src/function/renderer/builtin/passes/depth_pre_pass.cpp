#include "vultra/function/renderer/builtin/passes/depth_pre_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/depth_pre_data.hpp"
#include "vultra/function/renderer/builtin/resources/light_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/function/renderer/vertex_format.hpp"

#include <shader_headers/depth_pre.frag.spv.h>
#include <shader_headers/geometry.vert.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "DepthPrePass";

        DepthPrePass::DepthPrePass(rhi::RenderDevice& rd) : rhi::RenderPass<DepthPrePass>(rd) {}

        void DepthPrePass::addPass(FrameGraph&                 fg,
                                   FrameGraphBlackboard&       blackboard,
                                   const rhi::Extent2D&        resolution,
                                   const RenderPrimitiveGroup& renderPrimitiveGroup)
        {
            const auto& depthPreData = fg.addCallbackPass<DepthPreData>(
                PASS_NAME,
                [this, &fg, &blackboard, resolution](FrameGraph::Builder& builder, DepthPreData& data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());
                    read(builder, blackboard.get<LightData>());

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
                [this, renderPrimitiveGroup](const DepthPreData&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    gfx::BaseGeometryPassInfo passInfo {
                        .depthFormat  = rhi::getDepthFormat(*framebufferInfo),
                        .colorFormats = rhi::getColorFormats(*framebufferInfo),
                    };

                    cb.beginRendering(*framebufferInfo);

                    const auto& [opaquePrimitives, _, __] = renderPrimitiveGroup;

                    // Only draw opaque primitives in depth pre-pass
                    for (const auto& primitive : opaquePrimitives)
                    {
                        passInfo.vertexFormat = primitive.mesh->vertexFormat.get();
                        const auto* pipeline  = getPipeline(passInfo);

                        cb.bindPipeline(*pipeline);

                        rc.bindDescriptorSets(*pipeline);

                        cb.draw({
                            .vertexBuffer = primitive.mesh->vertexBuffer.get(),
                            .vertexOffset = primitive.renderSubMesh.vertexOffset,
                            .numVertices  = primitive.renderSubMesh.vertexCount,
                            .indexBuffer  = primitive.mesh->indexBuffer.get(),
                            .indexOffset  = primitive.renderSubMesh.indexOffset,
                            .numIndices   = primitive.renderSubMesh.indexCount,
                        });
                    }

                    rc.endRendering();
                });

            add(blackboard, depthPreData);
        }

        rhi::GraphicsPipeline DepthPrePass::createPipeline(const gfx::BaseGeometryPassInfo& passInfo) const
        {
            rhi::GraphicsPipeline::Builder builder {};

            builder.setDepthFormat(passInfo.depthFormat)
                .setColorFormats(passInfo.colorFormats)
                .setInputAssembly(passInfo.vertexFormat->getAttributes())
                .setTopology(passInfo.topology)
                .addBuiltinShader(rhi::ShaderType::eVertex, geometry_vert_spv)
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