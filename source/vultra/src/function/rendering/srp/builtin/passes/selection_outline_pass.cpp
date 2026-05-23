#include "vultra/function/rendering/srp/builtin/passes/selection_outline_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>

namespace vultra
{
    SelectionOutlinePass::SelectionOutlinePass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "SelectionOutlinePass";

        struct alignas(16) PushConstants
        {
            glm::vec4 outlineColor {1.0f, 0.55f, 0.08f, 1.0f};
            glm::vec4 texelSize {0.0f};
            glm::uvec4 entityInfo {0u};
        };
    } // namespace

    FrameGraphResource
    SelectionOutlinePass::addPass(FrameGraphBuildContext&                                ctx,
                                  FrameGraphResource                                     source,
                                  FrameGraphResource                                     entityId,
                                  FrameGraphResource                                     depth,
                                  const BuiltinRenderSettings::SelectionOutlineSettings& settings)
    {
        struct PassData
        {
            FrameGraphResource source;
            FrameGraphResource entityId;
            FrameGraphResource depth;
            FrameGraphResource output;
        };

        const auto resolution = ctx.view().extent;
        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution, source, entityId, depth](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.source = builder.read(source,
                                         framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = 0},
                                                     .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                 },
                                             .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                         });
                pd.entityId = builder.read(entityId,
                                           framegraph::TextureRead {
                                               .binding =
                                                   {
                                                       .location      = {.set = 3, .binding = 1},
                                                       .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                   },
                                               .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                           });
                pd.depth = builder.read(depth,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 3, .binding = 2},
                                                    .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                            .imageAspect = rhi::ImageAspect::eDepth,
                                        });

                pd.output = builder.create<framegraph::FrameGraphTexture>(
                    "SelectionOutlineOutput",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });
            },
            [this, settings, resolution](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0));
                if (!pipeline)
                    return;

                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["linear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][2], rc.ext.samplers["nearest"]);

                const float width  = static_cast<float>(std::max(resolution.width, 1u));
                const float height = static_cast<float>(std::max(resolution.height, 1u));
                PushConstants pc {
                    .outlineColor = glm::vec4(settings.color.x,
                                              settings.color.y,
                                              settings.color.z,
                                              std::clamp(settings.fillOpacity, 0.0f, 1.0f)),
                    .texelSize = glm::vec4(1.0f / width, 1.0f / height, 0.0f, 0.0f),
                    .entityInfo = glm::uvec4(settings.selectedEntityId & 0x00FFFFFFu,
                                             static_cast<uint32_t>(std::clamp(settings.thickness, 1.0f, 8.0f)),
                                             static_cast<uint32_t>(std::clamp(settings.edgeOpacity, 0.0f, 1.0f) * 255.0f),
                                             0u),
                };

                rc.cb.beginRendering(framebufferInfo).bindPipeline(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.drawFullScreenTriangle().endRendering();
            });

        return data.output;
    }

    rhi::GraphicsPipeline SelectionOutlinePass::createPipeline(const rhi::PixelFormat colorFormat) const
    {
        auto vertexShader = loadHighendShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        auto fragmentShader = loadHighendShader("selection_outline.frag", vshadersystem::ShaderStage::eFrag);
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[SelectionOutlinePass] Failed to load shaders");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
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
