#include "vultra/function/renderer/builtin/passes/meshlet_gbuffer_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/gbuffer_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/meshlet.mesh.spv.h>
#include <shader_headers/meshlet.task.spv.h>
#include <shader_headers/meshlet_debug_0.frag.spv.h>
#include <shader_headers/meshlet_debug_1.frag.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        const rhi::SPIRV& getMeshletDebugShader(uint32_t debugMode)
        {
            if (debugMode == 0)
            {
                return meshlet_debug_0_frag_spv;
            }
            else if (debugMode == 1)
            {
                return meshlet_debug_1_frag_spv;
            }
            throw std::runtime_error("Invalid meshlet debug mode");
        }

        constexpr auto PASS_NAME = "MeshletGBufferPass";

        MeshletGBufferPass::MeshletGBufferPass(rhi::RenderDevice& rd) : rhi::RenderPass<MeshletGBufferPass>(rd) {}

        void MeshletGBufferPass::addPass(FrameGraph&            fg,
                                         FrameGraphBlackboard&  blackboard,
                                         const rhi::Extent2D&   resolution,
                                         const RenderableGroup& renderableGroup,
                                         bool                   enableNormalMapping,
                                         uint32_t               debugMode)
        {
            const auto& gbufferData = fg.addCallbackPass<GBufferData>(
                PASS_NAME,
                [this, &fg, &blackboard, resolution](FrameGraph::Builder& builder, GBufferData& data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());

                    data.depth = builder.create<framegraph::FrameGraphTexture>(
                        "GBuffer - Depth",
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

                    data.albedo = builder.create<framegraph::FrameGraphTexture>(
                        "GBuffer - Albedo",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.albedo = builder.write(data.albedo,
                                                framegraph::Attachment {
                                                    .index       = 0,
                                                    .imageAspect = rhi::ImageAspect::eColor,
                                                    .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                });

                    data.normal = builder.create<framegraph::FrameGraphTexture>(
                        "GBuffer - Normal",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA16F,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.normal = builder.write(data.normal,
                                                framegraph::Attachment {
                                                    .index       = 1,
                                                    .imageAspect = rhi::ImageAspect::eColor,
                                                    .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                });

                    data.emissive = builder.create<framegraph::FrameGraphTexture>(
                        "GBuffer - Emissive",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.emissive = builder.write(data.emissive,
                                                  framegraph::Attachment {
                                                      .index       = 2,
                                                      .imageAspect = rhi::ImageAspect::eColor,
                                                      .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                  });

                    data.metallicRoughnessAO = builder.create<framegraph::FrameGraphTexture>(
                        "GBuffer - MetallicRoughnessAO",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.metallicRoughnessAO = builder.write(data.metallicRoughnessAO,
                                                             framegraph::Attachment {
                                                                 .index       = 3,
                                                                 .imageAspect = rhi::ImageAspect::eColor,
                                                                 .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                             });

                    data.textureLodDebug = builder.create<framegraph::FrameGraphTexture>(
                        "GBuffer - LOD Debug",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.textureLodDebug = builder.write(data.textureLodDebug,
                                                         framegraph::Attachment {
                                                             .index       = 4,
                                                             .imageAspect = rhi::ImageAspect::eColor,
                                                             .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                         });

                    data.meshletDebug = builder.create<framegraph::FrameGraphTexture>(
                        "GBuffer - Meshlet Debug",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.meshletDebug = builder.write(data.meshletDebug,
                                                      framegraph::Attachment {
                                                          .index       = 5,
                                                          .imageAspect = rhi::ImageAspect::eColor,
                                                          .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                      });
                },
                [this, &renderableGroup, enableNormalMapping, debugMode](const GBufferData&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    gfx::BaseGeometryPassInfo passInfo {
                        .depthFormat  = rhi::getDepthFormat(*framebufferInfo),
                        .colorFormats = rhi::getColorFormats(*framebufferInfo),
                    };

                    cb.beginRendering(*framebufferInfo);

                    const uint32_t normalMappingFlag = enableNormalMapping ? 1u : 0u;

                    for (const auto& renderable : renderableGroup.renderables)
                    {
                        for (const auto& sm : renderable.mesh->renderMesh.subMeshes)
                        {
                            if (sm.meshletCount == 0)
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
                                .enableNormalMapping          = normalMappingFlag,
                                .modelMatrix                  = renderable.modelMatrix,
                            };

                            auto* pipeline = getPipeline(passInfo, debugMode);

                            cb.bindPipeline(*pipeline);

                            rc.resourceSet[2][0] =
                                rhi::bindings::StorageBuffer {.buffer = renderable.mesh->materialBuffer.get()};
                            rc.resourceSet[2][1] = rhi::bindings::CombinedImageSamplerArray {
                                .textures    = getRenderDevice().getAllLoadedTextures(),
                                .imageAspect = rhi::ImageAspect::eColor,
                            };
                            rc.bindDescriptorSets(*pipeline);

                            cb.pushConstants(rhi::ShaderStages::eTask | rhi::ShaderStages::eMesh |
                                                 rhi::ShaderStages::eFragment,
                                             0,
                                             &pushConstants)
                                .drawMeshTask({
                                    (pushConstants.meshletCount + 31) / 32,
                                    1,
                                    1,
                                });
                        }
                    }

                    rc.endRendering();
                });

            add(blackboard, gbufferData);
        }

        rhi::GraphicsPipeline MeshletGBufferPass::createPipeline(const gfx::BaseGeometryPassInfo& passInfo,
                                                                 uint32_t                         debugMode) const
        {
            rhi::GraphicsPipeline::Builder builder {};

            builder.setDepthFormat(passInfo.depthFormat)
                .setColorFormats(passInfo.colorFormats)
                .setTopology(passInfo.topology)
                .addBuiltinShader(rhi::ShaderType::eTask, meshlet_task_spv)
                .addBuiltinShader(rhi::ShaderType::eMesh, meshlet_mesh_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, getMeshletDebugShader(debugMode))
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