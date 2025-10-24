#include "vultra/function/renderer/builtin/passes/meshlet_gbuffer_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/depth_pre_data.hpp"
#include "vultra/function/renderer/builtin/resources/gbuffer_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/function/renderer/shader_config/shader_config.hpp"

#include <shader_headers/meshlet.frag.spv.h>
#include <shader_headers/meshlet.mesh.spv.h>
#include <shader_headers/meshlet.task.spv.h>
#include <shader_headers/meshlet_earlyz.frag.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        struct GlobalMeshDataPushConstants
        {
            uint64_t vertexBufferAddress;
            uint64_t meshletBufferAddress;
            uint64_t meshletVertexBufferAddress;
            uint64_t meshletTriangleBufferAddress;

            uint32_t meshletCount;
            uint32_t enableNormalMapping;
            uint32_t debugMode;
            uint32_t padding1;

            glm::mat4 modelMatrix;
        };
        static_assert(sizeof(GlobalMeshDataPushConstants) % 16 == 0,
                      "Push constants size must be multiple of 16 bytes");

        const rhi::SPIRV& getMeshletFragmentShader(bool earlyZ)
        {
            if (earlyZ)
            {
                return meshlet_earlyz_frag_spv;
            }
            else
            {
                return meshlet_frag_spv;
            }
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
            auto&       depthPreData = blackboard.get<DepthPreData>();
            const auto& gbufferData  = fg.addCallbackPass<GBufferData>(
                PASS_NAME,
                [this, &fg, &blackboard, &depthPreData, resolution](FrameGraph::Builder& builder, GBufferData& data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());

                    depthPreData.depth = builder.write(depthPreData.depth,
                                                       framegraph::Attachment {
                                                            .imageAspect = rhi::ImageAspect::eDepth,
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

                    // Draw opaque meshlets with Early-Z
                    for (const auto& renderable : renderableGroup.renderables)
                    {
                        for (const auto& sm : renderable.mesh->renderMesh.subMeshes)
                        {
                            if (sm.meshletCount == 0 || !sm.opaque)
                            {
                                continue;
                            }

                            GlobalMeshDataPushConstants pushConstants {
                                 .vertexBufferAddress          = sm.vertexBufferAddress,
                                 .meshletBufferAddress         = sm.meshletBufferAddress,
                                 .meshletVertexBufferAddress   = sm.meshletVertexBufferAddress,
                                 .meshletTriangleBufferAddress = sm.meshletTriangleBufferAddress,
                                 .meshletCount                 = sm.meshletCount,
                                 .enableNormalMapping          = normalMappingFlag,
                                 .debugMode                    = debugMode,
                                 .modelMatrix                  = renderable.modelMatrix,
                            };

                            auto* pipeline = getPipeline(passInfo, true);

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
                                    DISPATCH_SIZE_X(pushConstants.meshletCount),
                                    1,
                                    1,
                                });
                        }
                    }

                    // Draw other meshlets without Early-Z
                    for (const auto& renderable : renderableGroup.renderables)
                    {
                        for (const auto& sm : renderable.mesh->renderMesh.subMeshes)
                        {
                            if (sm.meshletCount == 0 || sm.opaque)
                            {
                                continue;
                            }

                            GlobalMeshDataPushConstants pushConstants {
                                 .vertexBufferAddress          = sm.vertexBufferAddress,
                                 .meshletBufferAddress         = sm.meshletBufferAddress,
                                 .meshletVertexBufferAddress   = sm.meshletVertexBufferAddress,
                                 .meshletTriangleBufferAddress = sm.meshletTriangleBufferAddress,
                                 .meshletCount                 = sm.meshletCount,
                                 .enableNormalMapping          = normalMappingFlag,
                                 .debugMode                    = debugMode,
                                 .modelMatrix                  = renderable.modelMatrix,
                            };

                            auto* pipeline = getPipeline(passInfo, false);

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
                                    DISPATCH_SIZE_X(pushConstants.meshletCount),
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
                                                                 bool                             earlyZ) const
        {
            rhi::GraphicsPipeline::Builder builder {};

            builder.setDepthFormat(passInfo.depthFormat)
                .setColorFormats(passInfo.colorFormats)
                .setTopology(passInfo.topology)
#if USE_TASK_SHADER
                .addBuiltinShader(rhi::ShaderType::eTask, meshlet_task_spv)
#endif
                .addBuiltinShader(rhi::ShaderType::eMesh, meshlet_mesh_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, getMeshletFragmentShader(earlyZ))
                .setDepthStencil({
                    .depthTest      = true,
                    .depthWrite     = !earlyZ,
                    .depthCompareOp = earlyZ ? rhi::CompareOp::eEqual : rhi::CompareOp::eLessOrEqual,
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