#include "vultra/function/rendering/srp/builtin/passes/motion_vector_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/framework/resource_uploader.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"
#include "vultra/function/rendering/srp/render_view.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <format>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/mat4x4.hpp>

namespace vultra
{
    MotionVectorPass::MotionVectorPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    namespace
    {
        constexpr auto PASS_NAME = "MotionVectorPass";

        // Matches MotionVectorBlock in motion_vector.frag (std140 UBO). One reprojection
        // matrix per multiview eye; mono views only use slot 0.
        struct MotionVectorBlock
        {
            glm::mat4 clipToPreviousClip[2] {glm::mat4 {1.0f}, glm::mat4 {1.0f}};
            glm::vec4 params {1.0f, 1.0f, 1.0f, 1.0f}; // xy = resolution, z/w = per-view reset
        };

        [[nodiscard]] glm::mat4 gpuProjection(glm::mat4 projection, const rhi::RenderBackendApi backendApi)
        {
            if (backendApi == rhi::RenderBackendApi::eVulkan)
                projection[1][1] *= -1.0f;
            return projection;
        }
    } // namespace

    FrameGraphResource MotionVectorPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource depth)
    {
        const auto depthDesc  = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(depth);
        const auto outputDesc = makeInheritedTextureDesc(depthDesc, rhi::PixelFormat::eRG16F);
        const auto extent     = depthDesc.extent;

        // One reprojection matrix per eye: multiview renders both layers in one draw, so
        // the right eye cannot reuse the left eye's clip-to-previous-clip transform.
        MotionVectorBlock block {};
        block.params = glm::vec4(static_cast<float>(std::max(extent.width, 1u)),
                                 static_cast<float>(std::max(extent.height, 1u)),
                                 1.0f,
                                 1.0f);
        const auto& view = ctx.view();
        for (uint32_t i = 0; i < 2u; ++i)
        {
            const RenderCamera* camera = (view.multiviewCameraCount > i && view.multiviewCameras[i] != nullptr) ?
                                             view.multiviewCameras[i] :
                                             view.camera;
            if (camera == nullptr || !camera->hasPreviousViewProjection)
                continue;
            const glm::mat4 currentVp  = gpuProjection(camera->projection, ctx.rd.getBackendApi()) * camera->view;
            const glm::mat4 previousVp =
                gpuProjection(camera->previousProjection, ctx.rd.getBackendApi()) * camera->previousView;
            block.clipToPreviousClip[i] = previousVp * glm::inverse(currentVp);
            (i == 0u ? block.params.z : block.params.w) = 0.0f;
        }

        const auto blockResource = uploadFrameGraphStruct(ctx.fg,
                                                          ctx.frameResources,
                                                          ctx.rd,
                                                          "UploadMotionVectorBlock",
                                                          "MotionVectorBlock",
                                                          framegraph::BufferType::eUniformBuffer,
                                                          block);

        struct PassData
        {
            FrameGraphResource block;
            FrameGraphResource depth;
            FrameGraphResource output;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [depth, outputDesc, blockResource](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.block = builder.read(blockResource,
                                        framegraph::BindingInfo {
                                            .location      = {.set = 1, .binding = 0},
                                            .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                        });
                pd.depth = builder.read(depth,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 3, .binding = 0},
                                                    .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                            .imageAspect = rhi::ImageAspect::eDepth,
                                        });

                pd.output = builder.create<framegraph::FrameGraphTexture>("MotionVectors", outputDesc);
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });
            },
            [this, extent](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["nearest"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.beginRendering(framebufferInfo);
                {
                    const auto scopeName = std::format("{} {}x{}", PASS_NAME, extent.width, extent.height);
                    RHI_GPU_ZONE(rc.cb, scopeName.c_str());
                    rc.cb.drawFullScreenTriangle();
                }
                rc.cb.endRendering();
            });

        return data.output;
    }

    rhi::GraphicsPipeline
    MotionVectorPass::createPipeline(const rhi::PixelFormat colorFormat, const uint32_t viewMask) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[MotionVectorPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        auto fragmentShader = loadGeneralShader("motion_vector.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[MotionVectorPass] Failed to load fragment shader");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
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
            .build(getRenderDevice());
    }
} // namespace vultra
