#include "vultra/function/renderer/builtin/passes/gbuffer_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
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
#include <shader_headers/geometry.vert.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "GBufferPass";

        struct MeshConstants
        {
            glm::mat4 model {1.0f};
            glm::vec4 baseColor;
            float     opacity;
            float     metallicFactor;
            float     roughnessFactor;

            int useAlbedoTexture;
            int useMetallicTexture;
            int useRoughnessTexture;
            int useNormalTexture;
            int useAOTexture;
            int useEmissiveTexture;
            int useMetallicRoughnessTexture;
            int useSpecularTexture;

            MeshConstants(const glm::mat4& m, const gfx::PBRMaterial& mat) :
                model(m), baseColor(mat.baseColor), opacity(mat.opacity), metallicFactor(mat.metallicFactor),
                roughnessFactor(mat.roughnessFactor), useAlbedoTexture(mat.useAlbedoTexture ? 1 : 0),
                useMetallicTexture(mat.useMetallicTexture ? 1 : 0),
                useRoughnessTexture(mat.useRoughnessTexture ? 1 : 0), useNormalTexture(mat.useNormalTexture ? 1 : 0),
                useAOTexture(mat.useAOTexture ? 1 : 0), useEmissiveTexture(mat.useEmissiveTexture ? 1 : 0),
                useMetallicRoughnessTexture(mat.useMetallicRoughnessTexture ? 1 : 0),
                useSpecularTexture(mat.useSpecularTexture ? 1 : 0)
            {}
        };

        GBufferPass::GBufferPass(rhi::RenderDevice& rd) : rhi::RenderPass<GBufferPass>(rd) {}

        void GBufferPass::addPass(FrameGraph&                 fg,
                                  FrameGraphBlackboard&       blackboard,
                                  const rhi::Extent2D&        resolution,
                                  const RenderPrimitiveGroup& renderPrimitiveGroup,
                                  bool                        enableAreaLight,
                                  bool                        enableNormalMapping)
        {
            const auto& gBufferData = fg.addCallbackPass<GBufferData>(
                PASS_NAME,
                [this, &fg, &blackboard, resolution, enableAreaLight](FrameGraph::Builder& builder, GBufferData& data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());
                    read(builder, blackboard.get<LightData>());

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
                },
                [this, renderPrimitiveGroup, enableAreaLight, enableNormalMapping](
                    const GBufferData&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    gfx::BaseGeometryPassInfo passInfo {
                        .depthFormat  = rhi::getDepthFormat(*framebufferInfo),
                        .colorFormats = rhi::getColorFormats(*framebufferInfo),
                    };

                    cb.beginRendering(*framebufferInfo);

                    const auto& [opaquePrimitives, alphaMaskingPrimitives, decalPrimitives] = renderPrimitiveGroup;

                    // Phase 1: Draw opaque renderables
                    for (const auto& primitive : opaquePrimitives)
                    {
                        passInfo.vertexFormat = primitive.mesh->vertexFormat.get();

                        MeshConstants meshConstants(primitive.modelMatrix, primitive.renderSubMesh.material);
                        if (!enableNormalMapping)
                        {
                            meshConstants.useNormalTexture = 0;
                        }

                        // True for now
                        // TODO: Bake the scene, mark materials that need alpha masking
                        const auto* pipeline =
                            getPipeline(passInfo, primitive.renderSubMesh.material.doubleSided, true);

                        cb.bindPipeline(*pipeline).pushConstants(rhi::ShaderStages::eVertex |
                                                                     rhi::ShaderStages::eFragment,
                                                                 0,
                                                                 sizeof(MeshConstants),
                                                                 &meshConstants);

                        rc.resourceSet[3][0] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.albedo.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][1] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.metallic.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][2] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.roughness.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][3] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.normal.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][4] =
                            rhi::bindings::CombinedImageSampler {.texture = primitive.renderSubMesh.material.ao.get(),
                                                                 .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][5] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.emissive.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][6] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.metallicRoughness.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][7] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.specular.get(),
                            .imageAspect = rhi::ImageAspect::eColor};

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

                        MeshConstants meshConstants(primitive.modelMatrix, primitive.renderSubMesh.material);

                        const auto* pipeline =
                            getPipeline(passInfo, primitive.renderSubMesh.material.doubleSided, true);

                        cb.bindPipeline(*pipeline).pushConstants(rhi::ShaderStages::eVertex |
                                                                     rhi::ShaderStages::eFragment,
                                                                 0,
                                                                 sizeof(MeshConstants),
                                                                 &meshConstants);

                        rc.resourceSet[3][0] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.albedo.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][1] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.metallic.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][2] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.roughness.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][3] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.normal.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][4] =
                            rhi::bindings::CombinedImageSampler {.texture = primitive.renderSubMesh.material.ao.get(),
                                                                 .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][5] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.emissive.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][6] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.metallicRoughness.get(),
                            .imageAspect = rhi::ImageAspect::eColor};
                        rc.resourceSet[3][7] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.specular.get(),
                            .imageAspect = rhi::ImageAspect::eColor};

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

                        MeshConstants meshConstants(primitive.modelMatrix, primitive.renderSubMesh.material);
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
                                builder.setBlending(i, primitive.renderSubMesh.material.blendState);
                            }
                            m_DecalPipeline        = builder.build(getRenderDevice());
                            m_DecalPipelineCreated = true;
                        }

                        cb.bindPipeline(m_DecalPipeline)
                            .pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment,
                                           0,
                                           sizeof(MeshConstants),
                                           &meshConstants);

                        rc.resourceSet[3][0] = rhi::bindings::CombinedImageSampler {
                            .texture     = primitive.renderSubMesh.material.albedo.get(),
                            .imageAspect = rhi::ImageAspect::eColor};

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
                                                          bool                             alphaMasking) const
        {
            rhi::GraphicsPipeline::Builder builder {};

            builder.setDepthFormat(passInfo.depthFormat)
                .setColorFormats(passInfo.colorFormats)
                .setInputAssembly(passInfo.vertexFormat->getAttributes())
                .setTopology(passInfo.topology)
                .addBuiltinShader(rhi::ShaderType::eVertex, geometry_vert_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment,
                                  alphaMasking ? gbuffer_alpha_masking_frag_spv : gbuffer_frag_spv)
                .setDepthStencil({
                    .depthTest      = true,
                    .depthWrite     = true,
                    .depthCompareOp = rhi::CompareOp::eLessOrEqual,
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