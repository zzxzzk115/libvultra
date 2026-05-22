#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_foveated_composite_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <cmath>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GeneralGaussianSplatFoveatedCompositePass";

        struct GeneralGaussianSplatFoveatedCompositeUniforms
        {
            glm::vec4 foveatedGazeAndRings {0.5f, 0.5f, 5.0f, 15.0f};
            glm::vec4 foveatedParams {1.0f, 1.0f, 2.0f, 0.0f};
        };

        [[nodiscard]] glm::vec2 foveatedTanHalfFov(const RenderView& view)
        {
            if (!view.camera)
                return glm::vec2 {1.0f};

            const glm::mat4& projection = view.camera->projection;
            return glm::vec2 {1.0f / std::max(std::abs(projection[0][0]), 1e-5f),
                              1.0f / std::max(std::abs(projection[1][1]), 1e-5f)};
        }

        [[nodiscard]] GeneralGaussianSplatFoveatedCompositeUniforms makeCompositeUniforms(
            const RenderView&             view,
            const resource::GpuSceneView& gpuSceneView)
        {
            const glm::vec2 tanHalfFov = foveatedTanHalfFov(view);

            GeneralGaussianSplatFoveatedCompositeUniforms pc {};
            pc.foveatedGazeAndRings =
                glm::vec4 {gpuSceneView.generalGaussianSplatFoveatedGaze.x,
                           gpuSceneView.generalGaussianSplatFoveatedGaze.y,
                           gpuSceneView.generalGaussianSplatFoveatedRingDegrees.x,
                           gpuSceneView.generalGaussianSplatFoveatedRingDegrees.y};
            pc.foveatedParams =
                glm::vec4 {tanHalfFov.x,
                           tanHalfFov.y,
                           std::max(gpuSceneView.generalGaussianSplatFoveatedTransitionDegrees, 0.0f),
                           0.0f};
            return pc;
        }
    } // namespace

    GeneralGaussianSplatFoveatedCompositePass::GeneralGaussianSplatFoveatedCompositePass()
    {
        setShaderProfile(rhi::ShaderProfile::eGeneral);
    }

    FrameGraphResource GeneralGaussianSplatFoveatedCompositePass::compose(FrameGraphBuildContext& ctx,
                                                                          FrameGraphResource      foveaLayer,
                                                                          FrameGraphResource      midLayer,
                                                                          FrameGraphResource      outerLayer,
                                                                          FrameGraphResource      baseColor)
    {
        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (!gpuSceneView || !foveaLayer || !midLayer || !outerLayer)
            return {};

        const bool useBase = static_cast<bool>(baseColor);
        const bool useMultiview = ctx.view().enableMultiview && ctx.view().multiviewCameraCount >= 2u;
        const auto resolution = ctx.view().extent;
        const auto uniformsData = makeCompositeUniforms(ctx.view(), *gpuSceneView);
        if (!m_UniformBuffer || m_UniformBuffer.getSize() < sizeof(GeneralGaussianSplatFoveatedCompositeUniforms))
        {
            m_UniformBuffer = ctx.rd.createUniformBuffer(sizeof(GeneralGaussianSplatFoveatedCompositeUniforms));
        }

        struct PassData
        {
            FrameGraphResource color;
            FrameGraphResource foveaLayer;
            FrameGraphResource midLayer;
            FrameGraphResource outerLayer;
            FrameGraphResource baseColor;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution, useMultiview, useBase, foveaLayer, midLayer, outerLayer, baseColor](
                FrameGraph::Builder& builder, PassData& data) {
                PASS_SETUP_ZONE;

                data.foveaLayer = builder.read(foveaLayer,
                                               framegraph::TextureRead {
                                                   .binding =
                                                       {
                                                           .location      = {.set = 3, .binding = 0},
                                                           .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                       },
                                                   .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               });
                data.midLayer = builder.read(midLayer,
                                             framegraph::TextureRead {
                                                 .binding =
                                                     {
                                                         .location      = {.set = 3, .binding = 1},
                                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                     },
                                                 .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                                 .imageAspect = rhi::ImageAspect::eColor,
                                             });
                data.outerLayer = builder.read(outerLayer,
                                               framegraph::TextureRead {
                                                   .binding =
                                                       {
                                                           .location      = {.set = 3, .binding = 2},
                                                           .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                       },
                                                   .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               });

                if (useBase)
                {
                    data.baseColor = builder.read(baseColor,
                                                  framegraph::TextureRead {
                                                      .binding =
                                                          {
                                                              .location      = {.set = 3, .binding = 3},
                                                              .pipelineStage =
                                                                  framegraph::PipelineStage::eFragmentShader,
                                                          },
                                                      .type = framegraph::TextureRead::Type::eCombinedImageSampler,
                                                      .imageAspect = rhi::ImageAspect::eColor,
                                                  });
                }

                data.color = builder.create<framegraph::FrameGraphTexture>(
                    "General Gaussian Foveated Composite Color",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .layers     = useMultiview ? 2u : 0u,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                data.color = builder.write(data.color,
                                           framegraph::Attachment {
                                               .index       = 0,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                               .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                           });
            },
            [this, useMultiview, useBase, uniformsData](
                const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                if (!rc.framebufferInfo().has_value())
                    return;

                const auto* pipeline = getPipeline(rhi::getColorFormat(rc.framebufferInfo().value(), 0),
                                                   useMultiview,
                                                   useBase);
                if (!pipeline)
                    return;

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["linear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["linear"]);
                rc.overrideSampler(rc.resourceSet[3][2], rc.ext.samplers["linear"]);
                if (useBase)
                    rc.overrideSampler(rc.resourceSet[3][3], rc.ext.samplers["nearest"]);

                auto framebufferInfo = rc.framebufferInfo().value();
                if (useMultiview)
                {
                    framebufferInfo.layers   = 2u;
                    framebufferInfo.viewMask = 0x3u;
                }

                rc.cb.update(m_UniformBuffer, 0, sizeof(GeneralGaussianSplatFoveatedCompositeUniforms), &uniformsData);
                rc.resourceSet[1][30] = rhi::bindings::UniformBuffer {.buffer = &m_UniformBuffer};
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.beginRendering(framebufferInfo).drawFullScreenTriangle().endRendering();
            });

        return data.color;
    }

    rhi::GraphicsPipeline GeneralGaussianSplatFoveatedCompositePass::createPipeline(
        const rhi::PixelFormat colorFormat,
        const bool             useMultiview,
        const bool             useBase) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
            return {};

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", useMultiview ? 1u : 0u},
            {"USE_BASE", useBase ? 1u : 0u},
        };

        auto fragmentShader =
            loadGeneralShader("gaussian_splat_foveated_composite.frag",
                              vshadersystem::ShaderStage::eFrag,
                              fragmentKeywords);
        if (!fragmentShader)
            return {};

        auto builder = rhi::GraphicsPipeline::Builder {};
        builder.setViewMask(useMultiview ? 0x3u : 0u)
            .setColorFormats({colorFormat})
            .setInputAssembly({})
            .setDepthStencil({
                .depthTest  = false,
                .depthWrite = false,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0, {.enabled = false});

        if (getRenderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            builder
                .addShader(rhi::ShaderType::eVertex,
                           {
                               .code           = vertexShader->wgsl,
                               .entryPointName = "main",
                               .defines        = {},
                               .reflection     = vertexShader->reflection,
                           })
                .addShader(rhi::ShaderType::eFragment,
                           {
                               .code           = fragmentShader->wgsl,
                               .entryPointName = "main",
                               .defines        = {},
                               .reflection     = fragmentShader->reflection,
                           });
        }
        else
        {
            builder.addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
                .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader);
        }

        return builder.build(getRenderDevice());
    }
} // namespace vultra
