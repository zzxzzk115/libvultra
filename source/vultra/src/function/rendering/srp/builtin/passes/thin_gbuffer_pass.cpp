#include "vultra/function/rendering/srp/builtin/passes/thin_gbuffer_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    ThinGBufferPass::ThinGBufferPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "ThinGBufferPass";

        struct ThinGBufferPushConstants
        {
            uint32_t maxDraws {0};
            uint32_t maxMeshlets {0};
            uint32_t maxMeshletVertices {0};
            uint32_t maxMeshletTriangles {0};
        };
    }

    FrameGraphResource ThinGBufferPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource visibility)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource visibility;
            FrameGraphResource color;
            FrameGraphResource normal;
            FrameGraphResource material;
            FrameGraphResource entityId;

            FrameGraphResource drawBuffer;
            FrameGraphResource meshletsBuffer;
            FrameGraphResource materialTableBuffer;
            FrameGraphResource materialParamsBuffer;
            FrameGraphResource meshletVertexBuffer;
            FrameGraphResource meshletTriangleBuffer;
        };

        const auto colorDesc =
            makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA8_UNorm, rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled);
        const auto normalDesc =
            makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRG8_UNorm, rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled);
        const auto materialDesc =
            makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA8_UNorm, rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled);
        const bool writeEntityId =
            ctx.view().camera != nullptr &&
            (ctx.view().camera->debugEntityIdOutput || ctx.view().camera->selectionOutlineEnabled);
        const auto entityIdDesc = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA8_UNorm);
        const auto cameraBlock           = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto drawBuffer            = ctx.data.tryGet(kResKey_DrawBuffer);
        const auto meshletsBuffer        = ctx.data.tryGet(kResKey_MeshletsBuffer);
        const auto materialTableBuffer   = ctx.data.tryGet(kResKey_MaterialTableBuffer);
        const auto materialParamsBuffer  = ctx.data.tryGet(kResKey_MaterialParametersBuffer);
        const auto meshletVertexBuffer   = ctx.data.tryGet(kResKey_MeshletVertexBuffer);
        const auto meshletTriangleBuffer = ctx.data.tryGet(kResKey_MeshletTriangleBuffer);

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [colorDesc,
             normalDesc,
             materialDesc,
             entityIdDesc,
             writeEntityId,
             cameraBlock,
             visibility,
             drawBuffer,
             meshletsBuffer,
             materialTableBuffer,
             materialParamsBuffer,
             meshletVertexBuffer,
             meshletTriangleBuffer](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         });
                pd.visibility =
                    builder.read(visibility,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 1},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                if (drawBuffer)
                    pd.drawBuffer = builder.read(drawBuffer,
                                                 framegraph::BindingInfo {
                                                     .location      = {.set = 0, .binding = 1},
                                                     .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                 });
                if (meshletsBuffer)
                    pd.meshletsBuffer = builder.read(meshletsBuffer,
                                                     framegraph::BindingInfo {
                                                         .location      = {.set = 0, .binding = 4},
                                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                     });
                if (materialTableBuffer)
                    pd.materialTableBuffer =
                        builder.read(materialTableBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 8},
                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     });
                if (materialParamsBuffer)
                    pd.materialParamsBuffer =
                        builder.read(materialParamsBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 9},
                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     });
                if (meshletVertexBuffer)
                    pd.meshletVertexBuffer =
                        builder.read(meshletVertexBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 10},
                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     });
                if (meshletTriangleBuffer)
                    pd.meshletTriangleBuffer =
                        builder.read(meshletTriangleBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 11},
                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     });

                pd.color = builder.create<framegraph::FrameGraphTexture>(
                    "ThinGBufferColor",
                    colorDesc);
                pd.color = builder.write(pd.color,
                                         framegraph::Attachment {
                                             .index       = 0,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                             .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                         });

                pd.normal = builder.create<framegraph::FrameGraphTexture>(
                    "ThinGBufferNormal",
                    normalDesc);
                pd.normal = builder.write(pd.normal,
                                          framegraph::Attachment {
                                              .index       = 1,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                          });

                pd.material = builder.create<framegraph::FrameGraphTexture>(
                    "ThinGBufferMaterial",
                    materialDesc);
                pd.material = builder.write(pd.material,
                                            framegraph::Attachment {
                                                .index       = 2,
                                                .imageAspect = rhi::ImageAspect::eColor,
                                                .clearValue  = framegraph::ClearValue::eTransparentWhite,
                                            });
                if (writeEntityId)
                {
                    pd.entityId = builder.create<framegraph::FrameGraphTexture>(
                        "ThinGBufferEntityId",
                        entityIdDesc);
                    pd.entityId = builder.write(pd.entityId,
                                                framegraph::Attachment {
                                                    .index       = 3,
                                                    .imageAspect = rhi::ImageAspect::eColor,
                                                    .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                                });
                }
            },
            [this, writeEntityId](const PassData& pd, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                const auto* gpuSceneView     = rc.view().gpuSceneView;
                if (!gpuSceneDatabase || !gpuSceneDatabase->resources || !gpuSceneView || !pd.drawBuffer || !pd.meshletsBuffer ||
                    !pd.materialTableBuffer || !pd.materialParamsBuffer || !pd.meshletVertexBuffer ||
                    !pd.meshletTriangleBuffer)
                    return;

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline       = getPipeline(rhi::getColorFormat(framebufferInfo, 0),
                                                   rhi::getColorFormat(framebufferInfo, 1),
                                                   rhi::getColorFormat(framebufferInfo, 2),
                                                   writeEntityId ? rhi::getColorFormat(framebufferInfo, 3) :
                                                                   rhi::PixelFormat::eUndefined,
                                                   writeEntityId,
                                                   framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                rc.resourceSet[3][4] = rhi::bindings::CombinedImageSamplerArray {
                    .textures    = gpuSceneDatabase->resources->getBindlessTextureHandles(),
                    .imageAspect = rhi::ImageAspect::eColor,
                };
                if (gpuSceneDatabase->skinMatrixBuffer)
                    rc.resourceSet[0][46] =
                        rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->skinMatrixBuffer.get()};

                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["nearest"]);
                rc.cb.beginRendering(framebufferInfo).bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                const ThinGBufferPushConstants pc {
                    .maxDraws            = gpuSceneView->maxDraws,
                    .maxMeshlets         = static_cast<uint32_t>(gpuSceneDatabase->resources->meshlets.cpuMeshlets.size()),
                    .maxMeshletVertices  = static_cast<uint32_t>(
                         gpuSceneDatabase->resources->meshlets.cpuMeshletVertices.size()),
                    .maxMeshletTriangles = static_cast<uint32_t>(
                         gpuSceneDatabase->resources->meshlets.cpuMeshletTriangles.size()),
                };
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.drawFullScreenTriangle().endRendering();
            });

        ctx.data.set(kResKey_ThinGBufferColor, data.color);
        ctx.data.set(kResKey_GBufferNormal, data.normal);
        ctx.data.set(kResKey_GBufferMaterial, data.material);
        if (data.entityId)
            ctx.data.set(kResKey_GBufferEntityId, data.entityId);
        return data.color;
    }

    rhi::GraphicsPipeline ThinGBufferPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                          const rhi::PixelFormat normalFormat,
                                                          const rhi::PixelFormat materialFormat,
                                                          const rhi::PixelFormat entityIdFormat,
                                                          const bool             writeEntityId,
                                                          const uint32_t         viewMask) const
    {
        auto vertexShader = loadHighendShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[ThinGBufferPass] Failed to load vertex shader");
            return {};
        }

        auto fragmentShader = loadHighendShader(
            "thin_gbuffer.frag",
            vshadersystem::ShaderStage::eFrag,
            {{"WRITE_ENTITY_ID", writeEntityId ? 1u : 0u}});
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[ThinGBufferPass] Failed to load fragment shader");
            return {};
        }

        const auto colorFormats = writeEntityId ?
                                      std::vector<rhi::PixelFormat> {colorFormat,
                                                                     normalFormat,
                                                                     materialFormat,
                                                                     entityIdFormat} :
                                      std::vector<rhi::PixelFormat> {colorFormat, normalFormat, materialFormat};
        rhi::GraphicsPipeline::Builder builder;
        builder.setColorFormats(colorFormats)
            .setViewMask(viewMask)
            .setInputAssembly({})
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest  = false,
                .depthWrite = false,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0, {.enabled = false})
            .setBlending(1, {.enabled = false})
            .setBlending(2, {.enabled = false});
        if (writeEntityId)
            builder.setBlending(3, {.enabled = false});
        return builder.build(getRenderDevice());
    }
} // namespace vultra
