#include "vultra/function/renderer/builtin/passes/gbuffer_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/mesh_constants.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/depth_pre_data.hpp"
#include "vultra/function/renderer/builtin/resources/gbuffer_data.hpp"
#include "vultra/function/renderer/builtin/resources/light_data.hpp"
#include "vultra/function/renderer/builtin/upload_resources.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/function/renderer/vertex_format.hpp"

#include <shader_headers/area_light_debug.frag.spv.h>
#include <shader_headers/area_light_debug.vert.spv.h>
#include <shader_headers/decal.frag.spv.h>
#include <shader_headers/gbuffer.frag.spv.h>
#include <shader_headers/gbuffer_alpha_masking.frag.spv.h>
#include <shader_headers/gbuffer_earlyz.frag.spv.h>
#include <shader_headers/geometry.vert.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "GBufferPass";

        GBufferPass::GBufferPass(rhi::RenderDevice& rd) : rhi::RenderPass<GBufferPass>(rd) {}

        void GBufferPass::addPass(FrameGraph&                 fg,
                                  FrameGraphBlackboard&       blackboard,
                                  const rhi::Extent2D&        resolution,
                                  const RenderPrimitiveGroup& renderPrimitiveGroup,
                                  bool                        enableAreaLight,
                                  bool                        enableNormalMapping)
        {
            auto&       depthPreData = blackboard.get<DepthPreData>();
            const auto& gBufferData  = fg.addCallbackPass<GBufferData>(
                PASS_NAME,
                [this, &fg, &blackboard, &depthPreData, resolution, enableAreaLight](FrameGraph::Builder& builder,
                                                                                     GBufferData&         data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());
                    read(builder, blackboard.get<LightData>());

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
                },
                [this, &renderPrimitiveGroup, enableAreaLight, enableNormalMapping](
                    const GBufferData&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    gfx::BaseGeometryPassInfo passInfo {
                         .depthFormat  = rhi::getDepthFormat(*framebufferInfo),
                         .colorFormats = rhi::getColorFormats(*framebufferInfo),
                    };
                    uint32_t meshEnableNormalMapping = enableNormalMapping ? 1 : 0;

                    cb.beginRendering(*framebufferInfo);

                    const auto& [opaquePrimitives, alphaMaskingPrimitives, decalPrimitives] = renderPrimitiveGroup;

                    // Phase 1: Draw opaque renderables
                    for (const auto& primitive : opaquePrimitives)
                    {
                        passInfo.vertexFormat = primitive.mesh->vertexFormat.get();

                        const auto&   material = primitive.mesh->materials[primitive.renderSubMesh.materialIndex];
                        MeshConstants meshConstants(
                            primitive.modelMatrix, primitive.renderSubMesh.materialIndex, meshEnableNormalMapping);

                        // Enable earlyZ for opaque objects
                        const auto* pipeline = getPipeline(passInfo, material.doubleSided, false, true);

                        cb.bindPipeline(*pipeline).pushConstants(rhi::ShaderStages::eVertex |
                                                                     rhi::ShaderStages::eFragment,
                                                                 0,
                                                                 sizeof(MeshConstants),
                                                                 &meshConstants);

                        rc.resourceSet[3][0] = rhi::bindings::StorageBuffer {
                             .buffer = primitive.mesh->materialBuffer.get(),
                        };

                        rc.resourceSet[3][1] = rhi::bindings::CombinedImageSamplerArray {
                             .textures    = getRenderDevice().getAllLoadedTextures(),
                             .imageAspect = rhi::ImageAspect::eColor,
                        };

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

                    // Phase 2: Draw alpha masking renderables
                    rc.resourceSet.erase(3);
                    for (const auto& primitive : alphaMaskingPrimitives)
                    {
                        passInfo.vertexFormat = primitive.mesh->vertexFormat.get();

                        const auto&   material = primitive.mesh->materials[primitive.renderSubMesh.materialIndex];
                        MeshConstants meshConstants(
                            primitive.modelMatrix, primitive.renderSubMesh.materialIndex, meshEnableNormalMapping);

                        const auto* pipeline = getPipeline(passInfo, material.doubleSided, true);

                        cb.bindPipeline(*pipeline).pushConstants(rhi::ShaderStages::eVertex |
                                                                     rhi::ShaderStages::eFragment,
                                                                 0,
                                                                 sizeof(MeshConstants),
                                                                 &meshConstants);

                        rc.resourceSet[3][0] = rhi::bindings::StorageBuffer {
                             .buffer = primitive.mesh->materialBuffer.get(),
                        };
                        rc.resourceSet[3][1] = rhi::bindings::CombinedImageSamplerArray {
                             .textures    = getRenderDevice().getAllLoadedTextures(),
                             .imageAspect = rhi::ImageAspect::eColor,
                        };

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

                    // (Optional) Phase 3: Draw area lights if enabled
                    if (enableAreaLight)
                    {
                        if (!m_AreaLightDebugCreated)
                        {
                            auto builder = rhi::GraphicsPipeline::Builder {};
                            builder.setDepthFormat(passInfo.depthFormat)
                                .setColorFormats(passInfo.colorFormats)
                                .setInputAssembly({})
                                .setTopology(passInfo.topology)
                                .addBuiltinShader(rhi::ShaderType::eVertex, area_light_debug_vert_spv)
                                .addBuiltinShader(rhi::ShaderType::eFragment, area_light_debug_frag_spv)
                                .setDepthStencil({.depthTest      = true,
                                                   .depthWrite     = true,
                                                   .depthCompareOp = rhi::CompareOp::eLessOrEqual})
                                .setRasterizer(
                                    {.polygonMode = rhi::PolygonMode::eFill, .cullMode = rhi::CullMode::eNone});

                            for (auto i = 0; i < passInfo.colorFormats.size(); ++i)
                            {
                                builder.setBlending(i, {.enabled = false});
                            }

                            m_AreaLightDebugPipeline = builder.build(getRenderDevice());
                            m_AreaLightDebugCreated  = true;
                        }

                        rc.resourceSet.erase(3);

                        cb.bindPipeline(m_AreaLightDebugPipeline);
                        rc.bindDescriptorSets(m_AreaLightDebugPipeline);
                        rhi::GeometryInfo gi {.numVertices = 6 * LIGHTINFO_MAX_AREA_LIGHTS};
                        cb.draw(gi);
                    }

                    // Phase 4: Draw decal renderables
                    rc.resourceSet.erase(3);
                    for (const auto& primitive : decalPrimitives)
                    {
                        passInfo.vertexFormat = primitive.mesh->vertexFormat.get();

                        const auto&   material = primitive.mesh->materials[primitive.renderSubMesh.materialIndex];
                        MeshConstants meshConstants(
                            primitive.modelMatrix, primitive.renderSubMesh.materialIndex, meshEnableNormalMapping);
                        if (!m_DecalPipelineCreated)
                        {
                            auto builder = rhi::GraphicsPipeline::Builder {};
                            builder.setDepthFormat(passInfo.depthFormat)
                                .setColorFormats(passInfo.colorFormats)
                                .setInputAssembly(passInfo.vertexFormat->getAttributes())
                                .setTopology(passInfo.topology)
                                .addBuiltinShader(rhi::ShaderType::eVertex, geometry_vert_spv)
                                .addBuiltinShader(rhi::ShaderType::eFragment, decal_frag_spv)
                                .setDepthStencil({
                                     .depthTest      = true,
                                     .depthWrite     = false,
                                     .depthCompareOp = rhi::CompareOp::eLessOrEqual,
                                })
                                .setDepthBias({.constantFactor = 1.25f, .slopeFactor = 1.75f})
                                .setRasterizer(
                                    {.polygonMode = rhi::PolygonMode::eFill, .cullMode = rhi::CullMode::eBack});
                            for (auto i = 0; i < passInfo.colorFormats.size(); ++i)
                            {
                                builder.setBlending(i, material.blendState);
                            }
                            m_DecalPipeline        = builder.build(getRenderDevice());
                            m_DecalPipelineCreated = true;
                        }

                        cb.bindPipeline(m_DecalPipeline)
                            .pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment,
                                           0,
                                           sizeof(MeshConstants),
                                           &meshConstants);

                        rc.resourceSet[3][0] = rhi::bindings::StorageBuffer {
                             .buffer = primitive.mesh->materialBuffer.get(),
                        };

                        rc.resourceSet[3][1] = rhi::bindings::CombinedImageSamplerArray {
                             .textures    = getRenderDevice().getAllLoadedTextures(),
                             .imageAspect = rhi::ImageAspect::eColor,
                        };

                        rc.bindDescriptorSets(m_DecalPipeline);

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

            add(blackboard, gBufferData);
        }

        rhi::GraphicsPipeline GBufferPass::createPipeline(const gfx::BaseGeometryPassInfo& passInfo,
                                                          bool                             doubleSided,
                                                          bool                             alphaMasking,
                                                          bool                             earlyZ) const
        {
            rhi::GraphicsPipeline::Builder builder {};

            builder.setDepthFormat(passInfo.depthFormat)
                .setColorFormats(passInfo.colorFormats)
                .setInputAssembly(passInfo.vertexFormat->getAttributes())
                .setTopology(passInfo.topology)
                .addBuiltinShader(rhi::ShaderType::eVertex, geometry_vert_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment,
                                  alphaMasking ? gbuffer_alpha_masking_frag_spv :
                                                 (earlyZ ? gbuffer_earlyz_frag_spv : gbuffer_frag_spv))
                .setDepthStencil({
                    .depthTest      = true,
                    .depthWrite     = !earlyZ,
                    .depthCompareOp = earlyZ ? rhi::CompareOp::eEqual : rhi::CompareOp::eLessOrEqual,
                })
                .setRasterizer({.polygonMode = rhi::PolygonMode::eFill,
                                .cullMode    = doubleSided ? rhi::CullMode::eNone : rhi::CullMode::eBack});
            for (auto i = 0; i < passInfo.colorFormats.size(); ++i)
            {
                builder.setBlending(i, {.enabled = false});
            }

            return builder.build(getRenderDevice());
        }
    } // namespace gfx
} // namespace vultra