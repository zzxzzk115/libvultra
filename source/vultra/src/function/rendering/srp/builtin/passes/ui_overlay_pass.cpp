#include "vultra/function/rendering/srp/builtin/passes/ui_overlay_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/framework/render_frame_resources.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <vector>

namespace vultra
{
    UiOverlayPass::UiOverlayPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    namespace
    {
        constexpr auto PASS_NAME = "UiOverlayPass";

        struct alignas(16) GpuUiDrawItem
        {
            glm::vec4 rectPx;
            glm::vec4 color;
            glm::vec4 canvas;
            glm::uvec4 texture;
            glm::vec4 uvRect; // uvMin.xy, uvMax.xy (glyph atlas sub-rect; (0,0,1,1) for full-texture items)
            glm::vec4 params; // space, pixelsPerUnit, 0, 0
            glm::mat4 worldMatrix;
        };

        struct UiOverlayPushConstants
        {
            glm::vec2 targetResolutionPx {1.0f};
            uint32_t  itemCount {0u};
            uint32_t  itemIndex {0u};
            glm::vec4 previewTransform {0.0f, 0.0f, 1.0f, 0.0f};
            glm::mat4 viewProjection {1.0f};
        };

        [[nodiscard]] bool sanitizeBindlessTextures(std::vector<const rhi::Texture*>& textures)
        {
            const auto fallbackIt = std::find_if(textures.begin(), textures.end(), [](const auto* tex) { return tex != nullptr; });
            if (fallbackIt == textures.end())
                return false;
            const auto* fallback = *fallbackIt;
            for (auto*& texture : textures)
                if (!texture)
                    texture = fallback;
            return true;
        }
    } // namespace

    FrameGraphResource UiOverlayPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource source, FrameGraphResource depth)
    {
        const auto* renderWorld = ctx.view().renderWorld;
        if (!renderWorld || renderWorld->uiDrawItems.empty())
            return source;

        const auto sourceDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);
        const auto extent     = sourceDesc.extent;
        const bool useMultiview = ctx.view().enableMultiview && ctx.view().multiviewCameraCount == 2u;
        const auto viewMask = useMultiview ? ctx.view().renderTargetViewMask() : 0u;

        std::vector<GpuUiDrawItem> gpuItems;
        std::vector<uint32_t>      itemTextureIndices;
        std::vector<uint32_t>      itemFlags;
        gpuItems.reserve(renderWorld->uiDrawItems.size());
        itemTextureIndices.reserve(renderWorld->uiDrawItems.size());
        itemFlags.reserve(renderWorld->uiDrawItems.size());
        for (const auto& item : renderWorld->uiDrawItems)
        {
            if (!renderLayerVisible(ctx.view().camera, item.layerMask))
                continue;
            gpuItems.push_back(GpuUiDrawItem {
                .rectPx = {item.rectMinPx.x, item.rectMinPx.y, item.rectMaxPx.x, item.rectMaxPx.y},
                .color  = item.color,
                .canvas = {item.canvasReferencePx.x,
                           item.canvasReferencePx.y,
                           static_cast<float>(item.scaleMode),
                           static_cast<float>(item.fitMode)},
                .texture     = {item.textureIndex, item.flags, item.space, 0u},
                .uvRect      = {item.uvMin.x, item.uvMin.y, item.uvMax.x, item.uvMax.y},
                .params      = {static_cast<float>(item.space), item.pixelsPerUnit, 0.0f, 0.0f},
                .worldMatrix = item.worldMatrix,
            });
            itemTextureIndices.push_back(item.textureIndex);
            itemFlags.push_back(item.flags);
        }
        if (gpuItems.empty())
            return source;

        auto* drawBuffer = ctx.frameResources ?
            ctx.frameResources->uploadStorage(ctx.rd, gpuItems.data(), gpuItems.size()) :
            nullptr;
        if (!drawBuffer)
            return source;

        struct PassData
        {
            FrameGraphResource output;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [source, depth](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                // Read scene depth as a read-only depth attachment so world-space UI is occluded
                // by geometry (the hardware depth test does the work; screen UI at z=0 passes).
                if (depth)
                    builder.read(depth,
                                 framegraph::Attachment {
                                     .imageAspect = rhi::ImageAspect::eDepth,
                                 });

                pd.output = builder.write(source,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                          });
            },
            [this,
             extent,
             viewMask,
             drawBuffer,
             itemCount = static_cast<uint32_t>(gpuItems.size()),
             itemTextureIndices = std::move(itemTextureIndices),
             itemFlags = std::move(itemFlags)](
                const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                const auto* renderWorld      = rc.view().renderWorld;
                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                if (!renderWorld || itemCount == 0u || !drawBuffer || !rc.framebufferInfo())
                    return;

                auto framebufferInfo = rc.framebufferInfo().value();
                if (!framebufferInfo.colorAttachments.empty())
                {
                    framebufferInfo.colorAttachments[0].clearValue = std::nullopt;
                    framebufferInfo.colorAttachments[0].loadOp     = rhi::AttachmentLoadOp::eLoad;
                }

                const auto colorFormat = rhi::getColorFormat(framebufferInfo, 0);
                const auto depthFormat = rhi::getDepthFormat(framebufferInfo);
                const auto* uiPipeline  = getPipeline(colorFormat, viewMask, depthFormat);
                if (!uiPipeline)
                    return;

                rhi::prepareForReading(rc.cb, *drawBuffer);

                auto materialTextures = (gpuSceneDatabase && gpuSceneDatabase->resources) ?
                    gpuSceneDatabase->resources->getBindlessTextureHandles() :
                    std::vector<const rhi::Texture*> {};
                if (!sanitizeBindlessTextures(materialTextures))
                {
                    return;
                }
                const auto* fallbackTexture = materialTextures.empty() ? nullptr : materialTextures.front();
                if (!fallbackTexture)
                    return;

                UiOverlayPushConstants pc {
                    .targetResolutionPx = {static_cast<float>(extent.width), static_cast<float>(extent.height)},
                    .itemCount          = itemCount,
                    .itemIndex          = 0u,
                    .previewTransform   = rc.view().camera && rc.view().camera->uiOverlayTransformOverride ?
                                            glm::vec4 {rc.view().camera->uiOverlayOffsetPx.x,
                                                       rc.view().camera->uiOverlayOffsetPx.y,
                                                       rc.view().camera->uiOverlayScale,
                                                       1.0f} :
                                            glm::vec4 {0.0f, 0.0f, 1.0f, 0.0f},
                    .viewProjection = rc.view().camera ? rc.view().camera->viewProjection : glm::mat4 {1.0f},
                };

                const auto scopeName = std::format("{} {} items", PASS_NAME, itemCount);
                RHI_GPU_ZONE(rc.cb, scopeName.c_str());
                rc.cb.beginRendering(framebufferInfo);
                {
                    rc.cb.bindPipeline(*uiPipeline);
                    for (uint32_t i = 0u; i < itemCount; ++i)
                    {
                        const uint32_t textureIndex = i < itemTextureIndices.size() ? itemTextureIndices[i] : 0u;
                        const bool isGlyph = i < itemFlags.size() && (itemFlags[i] & 2u) != 0u;
                        const bool textured = i < itemFlags.size() && itemFlags[i] != 0u && textureIndex < materialTextures.size();
                        // Glyph (coverage) items sample the glyph atlas, bound directly from the render
                        // world rather than via the bindless scene pool (which may not carry the atlas).
                        const auto* texture = (isGlyph && renderWorld->glyphAtlasTexture) ?
                                                  renderWorld->glyphAtlasTexture :
                                              textured ? materialTextures[textureIndex] :
                                                         fallbackTexture;
                        if (!texture)
                            texture = fallbackTexture;

                        pc.itemIndex = i;
                        // Keep the draw-item buffer in its own set: on WebGPU set 1 is reserved for
                        // emulated push constants (kWebGPUPushConstantsSet/Binding = 1/31), and the
                        // push-constant UBO is injected as a set-1-only bind group. Mixing a user
                        // binding into set 1 breaks both the pipeline layout and the bind group.
                        rc.resourceSet[2] = {
                            {0, rhi::bindings::StorageBuffer {.buffer = drawBuffer}},
                        };
                        rc.resourceSet[3] = {
                            {4,
                             rhi::bindings::CombinedImageSampler {
                                 .texture     = texture,
                                 .imageAspect = rhi::ImageAspect::eColor,
                                 .sampler     = rc.ext.samplers["bilinear"],
                             }},
                        };
                        rc.cb.pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pc);
                        rc.bindDescriptorSets(*uiPipeline);
                        rc.cb.draw(rhi::GeometryInfo {.numVertices = 6u}, 1u);
                    }
                }
                rc.cb.endRendering();
            });

        return data.output;
    }

    rhi::GraphicsPipeline UiOverlayPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                        const uint32_t         viewMask,
                                                        const rhi::PixelFormat depthFormat) const
    {
        // Merged single-file shader: both stages share the base id "ui_overlay". USE_MULTIVIEW is a
        // file-level permute axis, so pass it at every stage load to select the variant explicitly.
        const rhi::ShaderLibraryRuntime::KeywordValues keywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        auto vertexShader = loadGeneralShader("ui_overlay", vshadersystem::ShaderStage::eVert, keywords);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[UiOverlayPass] Failed to load vertex shader");
            return {};
        }

        auto fragmentShader = loadGeneralShader("ui_overlay", vshadersystem::ShaderStage::eFrag, keywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[UiOverlayPass] Failed to load fragment shader");
            return {};
        }

        const bool useDepth = depthFormat != rhi::PixelFormat::eUndefined;

        auto builder = rhi::GraphicsPipeline::Builder {};
        builder
            .setColorFormats({colorFormat})
            .setDepthFormat(depthFormat)
            .setViewMask(viewMask)
            .setInputAssembly({})
            .setDepthStencil({
                // Test world-space UI against scene depth (read-only) so geometry occludes it;
                // screen-overlay UI is emitted at z=0 and always passes (stays on top).
                .depthTest      = useDepth,
                .depthWrite     = false,
                .depthCompareOp = rhi::CompareOp::eLessOrEqual,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            });

        // WebGPU consumes WGSL (the builtin web lib has no SPIR-V); Vulkan uses the builtin SPIR-V.
        if (getRenderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            builder
                .addShader(rhi::ShaderType::eVertex,
                           rhi::ShaderStageInfo {
                               .code           = vertexShader->wgsl,
                               .entryPointName = "main",
                               .defines        = {},
                               .reflection     = vertexShader->reflection,
                           })
                .addShader(rhi::ShaderType::eFragment,
                           rhi::ShaderStageInfo {
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

        builder.setBlending(0,
                            {
                                .enabled  = true,
                                .srcColor = rhi::BlendFactor::eSrcAlpha,
                                .dstColor = rhi::BlendFactor::eOneMinusSrcAlpha,
                                .srcAlpha = rhi::BlendFactor::eOne,
                                .dstAlpha = rhi::BlendFactor::eOneMinusSrcAlpha,
                            });

        return builder.build(getRenderDevice());
    }
} // namespace vultra
