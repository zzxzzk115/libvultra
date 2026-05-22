#include "vultra/function/rendering/srp/builtin/passes/hzb_generate_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>

namespace vultra
{
    HzbGeneratePass::HzbGeneratePass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "HZBGeneratePass";

        struct HzbPushConstants
        {
            uint32_t srcWidth {0};
            uint32_t srcHeight {0};
            uint32_t mipCount {0};
            uint32_t padding0 {0};
        };

        uint32_t calcHzbMipLevels(const rhi::Extent2D extent)
        {
            uint32_t levels = 1;
            uint32_t maxDim = std::max(extent.width, extent.height);
            while (maxDim > 1u)
            {
                maxDim = std::max(maxDim / 2u, 1u);
                ++levels;
            }
            return levels;
        }
    } // namespace

    void HzbGeneratePass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource depth)
    {
        struct PassData
        {
            FrameGraphResource depth;
            FrameGraphResource hzb;
        };

        const auto resolution = ctx.view().extent;
        const auto mipLevels  = calcHzbMipLevels(resolution);

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [depth, resolution, mipLevels](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.depth = builder.read(depth,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 0, .binding = 27},
                                                    .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eSampledImage,
                                            .imageAspect = rhi::ImageAspect::eDepth,
                                        });

                pd.hzb = builder.create<framegraph::FrameGraphTexture>(
                    "HZB",
                    {
                        .extent       = resolution,
                        .format       = rhi::PixelFormat::eR32F,
                        .numMipLevels = mipLevels,
                        .usageFlags   = rhi::ImageUsage::eSampled | rhi::ImageUsage::eStorage,
                    });
                pd.hzb = builder.write(pd.hzb,
                                       framegraph::ImageWrite {
                                           .binding =
                                               {
                                                   .location      = {.set = 0, .binding = 29},
                                                   .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                               },
                                           .imageAspect = rhi::ImageAspect::eColor,
                                       });
            },
            [this, resolution, mipLevels](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);

                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                {
                    return;
                }
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto variantHash =
                    computeHighendVariantHash("hzb_generate.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                {
                    return;
                }

                HzbPushConstants pc {};
                pc.srcWidth  = resolution.width;
                pc.srcHeight = resolution.height;
                pc.mipCount  = mipLevels;

                rc.cb.bindPipeline(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({(resolution.width + 7u) / 8u, (resolution.height + 7u) / 8u, 1u});
            });

        ctx.data.set(kResKey_HzbTexture, data.hzb);
    }

    rhi::ComputePipeline HzbGeneratePass::createPipeline(uint64_t variantHash) const
    {
        auto shader = loadHighendShaderVariant(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[HZBGeneratePass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
