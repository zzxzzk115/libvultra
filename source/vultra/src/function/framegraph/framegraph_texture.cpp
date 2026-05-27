#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/core/base/map_optional.hpp"
#include "vultra/core/base/string_util.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/transient_resources.hpp"

#include <cstdint>
#include <cmath>

namespace vultra
{
    namespace framegraph
    {
        namespace
        {
            template<class Target>
            struct StaticCast
            {
                template<class Source>
                Target operator()(Source&& source) const
                {
                    return static_cast<Target>(std::forward<Source>(source));
                }
            };

            [[nodiscard]] rhi::ClearValue convert(const ClearValue in)
            {
                switch (in)
                {
                    using enum ClearValue;

                    case eZero:
                        return 0.0f;
                    case eOne:
                        return 1.0f;

                    case eOpaqueBlack:
                        return glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f};
                    case eOpaqueWhite:
                        return glm::vec4 {1.0f};
                    case eTransparentBlack:
                        return glm::vec4 {0.0f};
                    case eTransparentWhite:
                        return glm::vec4 {1.0f, 1.0f, 1.0f, 0.0f};

                    case eFakeSky:
                        return glm::vec4 {0.529, 0.808, 0.922, 1.0};

                    case eUIntMax:
                        return glm::uvec4 {UINT_MAX};
                }

                assert(false);
                return {};
            }

            [[nodiscard]] auto makeAttachment(const Attachment& in, rhi::Texture* texture)
            {
                assert(texture && *texture);
                return rhi::AttachmentInfo {
                    .target     = texture,
                    .layer      = in.layer,
                    .face       = map(in.face, StaticCast<rhi::CubeFace> {}),
                    .clearValue = map(in.clearValue, convert),
                };
            }

            void applyRenderTargetInfo(const FrameGraphTexture::Desc& desc,
                                       const rhi::Texture&            texture,
                                       const Attachment&              attachment,
                                       rhi::FramebufferInfo&          framebufferInfo)
            {
                if (desc.viewMask != 0u)
                {
                    framebufferInfo.viewMask = desc.viewMask;
                    framebufferInfo.layers   = std::max(framebufferInfo.layers, 1u);
                    return;
                }

                const auto layerCount = attachment.layer ? 1u : std::max(texture.getNumLayers(), 1u);
                framebufferInfo.layers = std::max(framebufferInfo.layers, layerCount);
            }

            auto calculateMipmapFactor(const uint32_t numMipLevels)
            {
                auto factor = 0.0;
                for (auto i = 0u; i < numMipLevels; ++i)
                {
                    factor += std::pow(0.5f, i);
                }
                return factor;
            }
            [[nodiscard]] auto getApproximateSize(const FrameGraphTexture::Desc& desc)
            {
                auto size = static_cast<double>(desc.extent.width * desc.extent.height) * std::max(desc.depth, 1u);
                size *= rhi::getBytesPerPixel(desc.format);
                size *=
                    calculateMipmapFactor(desc.numMipLevels > 0 ? desc.numMipLevels : rhi::calcMipLevels(desc.extent));
                size *= std::max(desc.layers, 1u);
                if (desc.cubemap)
                    size *= 6u;
                return static_cast<std::uint64_t>(size);
            }

        } // namespace

        void FrameGraphTexture::create(const Desc& desc, void* allocator)
        {
            texture = static_cast<TransientResources*>(allocator)->acquireTexture(desc);
        }
        void FrameGraphTexture::destroy(const Desc& desc, void* allocator)
        {
            static_cast<TransientResources*>(allocator)->releaseTexture(desc, texture);
            texture = nullptr;
        }

        void FrameGraphTexture::preRead(const Desc& desc, const uint32_t bits, void* ctx) const
        {
            ZoneScopedN("T*");

            auto& [cb, _, __, viewData, sets, ___] = *static_cast<FrameGraphExecContext*>(ctx);

            if (holdsAttachment(bits))
            {
                if (!viewData.framebufferInfo)
                    viewData.framebufferInfo.emplace().area = {.extent = texture->getExtent()};
                const auto attachment = decodeAttachment(bits);
                applyRenderTargetInfo(desc, *texture, attachment, *viewData.framebufferInfo);

                switch (attachment.imageAspect)
                {
                    using enum rhi::ImageAspect;

                    case eDepth:
                        viewData.framebufferInfo->depthAttachment = makeAttachment(attachment, texture);
                        viewData.framebufferInfo->depthReadOnly   = true;
                        break;
                    case eStencil:
                        viewData.framebufferInfo->stencilAttachment = makeAttachment(attachment, texture);
                        viewData.framebufferInfo->stencilReadOnly   = true;
                        break;

                    default:
                        assert(false);
                }

                rhi::prepareForAttachment(cb, *texture, true);
            }
            else
            {
                const auto& [bindingInfo, type, imageAspect] = decodeTextureRead(bits);
                const auto [location, pipelineStage]         = bindingInfo;

                auto imageLayout = rhi::ImageLayout::eUndefined;
                auto dstAccess   = rhi::Access::eNone;

                if (static_cast<bool>(pipelineStage & PipelineStage::eTransfer))
                {
                    imageLayout = rhi::ImageLayout::eTransferSrc;
                    dstAccess   = rhi::Access::eTransferRead;
                }
                else
                {
                    const auto [set, binding] = location;
                    switch (type)
                    {
                        using enum TextureRead::Type;

                        case eCombinedImageSampler:
                            imageLayout        = rhi::ImageLayout::eReadOnly;
                            sets[set][binding] = rhi::bindings::CombinedImageSampler {
                                .texture     = texture,
                                .imageAspect = imageAspect,
                            };
                            break;
                        case eSampledImage:
                            imageLayout        = rhi::ImageLayout::eReadOnly;
                            sets[set][binding] = rhi::bindings::SampledImage {
                                .texture     = texture,
                                .imageAspect = imageAspect,
                            };
                            break;
                        case eStorageImage:
                            imageLayout        = rhi::ImageLayout::eGeneral;
                            sets[set][binding] = rhi::bindings::StorageImage {
                                .texture     = texture,
                                .imageAspect = imageAspect,
                                .mipLevel    = 0,
                            };
                            break;
                    }
                    dstAccess = rhi::Access::eShaderRead;
                }

                assert(imageLayout != rhi::ImageLayout::eUndefined);

                cb.getBarrierBuilder().imageBarrier(
                    {
                        .image     = *texture,
                        .newLayout = imageLayout,
                        .subresourceRange =
                            rhi::ImageSubresourceRange {
                                .levelCount = UINT32_MAX,
                                .layerCount = UINT32_MAX,
                            },
                    },
                    {
                        .dstStage  = convert(pipelineStage),
                        .dstAccess = dstAccess,
                    });
            }

        }
        void FrameGraphTexture::preWrite(const Desc& desc, const uint32_t bits, void* ctx) const
        {
            ZoneScopedN("+T");

            auto& [cb, _, __, viewData, sets, ___] = *static_cast<FrameGraphExecContext*>(ctx);

            if (holdsAttachment(bits))
            {
                if (!viewData.framebufferInfo)
                    viewData.framebufferInfo.emplace().area = {.extent = texture->getExtent()};

                const auto attachment = decodeAttachment(bits);
                applyRenderTargetInfo(desc, *texture, attachment, *viewData.framebufferInfo);

                switch (attachment.imageAspect)
                {
                    using enum rhi::ImageAspect;

                    case eDepth:
                        viewData.framebufferInfo->depthAttachment = makeAttachment(attachment, texture);
                        viewData.framebufferInfo->depthReadOnly   = false;
                        break;
                    case eStencil:
                        viewData.framebufferInfo->stencilAttachment = makeAttachment(attachment, texture);
                        viewData.framebufferInfo->stencilReadOnly   = false;
                        break;
                    case eColor: {
                        auto& v = viewData.framebufferInfo->colorAttachments;
                        v.resize(attachment.index + 1);
                        v[attachment.index] = makeAttachment(attachment, texture);
                    }
                    break;
                    default:
                        break;
                }

                rhi::prepareForAttachment(cb, *texture, false);
            }
            else
            {
                const auto [bindingInfo, imageAspect] = decodeImageWrite(bits);
                assert(imageAspect != rhi::ImageAspect::eNone);
                const auto [location, pipelineStage] = bindingInfo;
                const auto [set, binding]            = location;
                sets[set][binding]                   = rhi::bindings::StorageImage {
                                      .texture     = texture,
                                      .imageAspect = imageAspect,
                                      .mipLevel    = 0,
                };

                cb.getBarrierBuilder().imageBarrier(
                    {
                        .image            = *texture,
                        .newLayout        = rhi::ImageLayout::eGeneral,
                        .subresourceRange = rhi::ImageSubresourceRange {.levelCount = 1u, .layerCount = UINT32_MAX},
                    },
                    {
                        .dstStage  = convert(pipelineStage),
                        .dstAccess = rhi::Access::eShaderStorageWrite,
                    });
            }

        }

        std::string FrameGraphTexture::toString(const Desc& desc)
        {
            return std::format("{}x{} [{}]<BR/>Layers = {}<BR/>ViewMask = 0x{:X}<BR/>Size = ~{}<BR/>Usage = {}",
                               desc.extent.width,
                               desc.extent.height,
                               rhi::toString(desc.format),
                               std::max(desc.layers, 1u),
                               desc.viewMask,
                               util::formatBytes(getApproximateSize(desc)),
                               rhi::toString(desc.usageFlags));
        }
    } // namespace framegraph
} // namespace vultra
