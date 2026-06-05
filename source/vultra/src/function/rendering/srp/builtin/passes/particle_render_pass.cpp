#include "vultra/function/rendering/srp/builtin/passes/particle_render_pass.hpp"

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/framebuffer_info.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/primitive_topology.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/framework/prepared_render_data.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <optional>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "ParticleRenderPass";
    } // namespace

    ParticleRenderPass::ParticleRenderPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    FrameGraphResource ParticleRenderPass::addPass(FrameGraphBuildContext&                ctx,
                                                   FrameGraphResource                     source,
                                                   FrameGraphResource                     depth,
                                                   const std::vector<FrameGraphResource>& particleBuffers)
    {
        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (gpuSceneView == nullptr || gpuSceneView->particleEmitters.empty() || particleBuffers.empty())
            return source;

        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        if (!cameraBlock || !depth)
            return source;

        FrameGraphResource color = source;
        const auto         count = std::min(gpuSceneView->particleEmitters.size(), particleBuffers.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto particleBuffer = particleBuffers[i];
            if (!particleBuffer)
                continue;
            const auto& emitter = gpuSceneView->particleEmitters[i];

            struct PassData
            {
                FrameGraphResource color;
            };

            const auto data = ctx.fg.addCallbackPass<PassData>(
                PASS_NAME,
                [cameraBlock, depth, particleBuffer, color](FrameGraph::Builder& builder, PassData& pd) {
                    PASS_SETUP_ZONE;

                    builder.read(cameraBlock,
                                 framegraph::BindingInfo {
                                     .location      = {.set = 0, .binding = 0},
                                     .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                 });
                    builder.read(depth,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 0, .binding = 27},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eDepth,
                                 });
                    builder.read(particleBuffer,
                                 framegraph::BindingInfo {
                                     .location      = {.set = 2, .binding = 0},
                                     .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                 });
                    pd.color = builder.write(color,
                                             framegraph::Attachment {
                                                 .index       = 0,
                                                 .imageAspect = rhi::ImageAspect::eColor,
                                             });
                },
                [this, pc = emitter.pc, instanceCount = emitter.maxParticles](
                    const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                    VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);

                    setRenderDevice(rc.rd);
                    if (!rc.ext.builtinShaderLib)
                        return;
                    setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                    if (!rc.framebufferInfo().has_value())
                        return;
                    auto framebufferInfo = rc.framebufferInfo().value();
                    if (framebufferInfo.colorAttachments.empty())
                        return;
                    framebufferInfo.colorAttachments[0].clearValue = std::nullopt;
                    framebufferInfo.colorAttachments[0].loadOp     = rhi::AttachmentLoadOp::eLoad;

                    const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0));
                    if (!pipeline)
                        return;

                    RHI_GPU_ZONE(rc.cb, PASS_NAME);

                    // Depth is sampled (texelFetch) for the soft-particle fade; bind any valid sampler.
                    rc.overrideSampler(rc.resourceSet[0][27], rc.ext.samplers["linear"]);

                    rc.cb.beginRendering(framebufferInfo).bindPipeline(*pipeline);
                    rc.bindDescriptorSets(*pipeline);
                    rc.cb.pushConstants(rhi::ShaderStages::eVertex, 0, &pc);
                    rc.cb.draw(rhi::GeometryInfo {
                                   .topology    = rhi::PrimitiveTopology::eTriangleStrip,
                                   .numVertices = 4u,
                               },
                               instanceCount);
                    rc.cb.endRendering();
                });

            color = data.color;
        }

        return color;
    }

    rhi::GraphicsPipeline ParticleRenderPass::createPipeline(const rhi::PixelFormat colorFormat) const
    {
        // Multi-stage single-file shader: both stages share the base shader id "particle_billboard".
        auto vertexShader   = loadHighendShader("particle_billboard", vshadersystem::ShaderStage::eVert);
        auto fragmentShader = loadHighendShader("particle_billboard", vshadersystem::ShaderStage::eFrag);
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[ParticleRenderPass] Failed to load billboard shaders");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setTopology(rhi::PrimitiveTopology::eTriangleStrip)
            .setDepthStencil({
                .depthTest  = false,
                .depthWrite = false,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0,
                         {
                             .enabled  = true,
                             .srcColor = rhi::BlendFactor::eOne,
                             .dstColor = rhi::BlendFactor::eOne,
                             .colorOp  = rhi::BlendOp::eAdd,
                             .srcAlpha = rhi::BlendFactor::eOne,
                             .dstAlpha = rhi::BlendFactor::eOne,
                             .alphaOp  = rhi::BlendOp::eAdd,
                         })
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .build(getRenderDevice());
    }
} // namespace vultra
