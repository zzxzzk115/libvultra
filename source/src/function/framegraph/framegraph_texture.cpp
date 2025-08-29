#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/core/base/map_optional.hpp"
#include "vultra/core/base/string_util.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/render_context.hpp"
#include "vultra/function/framegraph/transient_resources.hpp"

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
                return static_cast<VkDeviceSize>(size);
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

        void FrameGraphTexture::preRead(const Desc&, const uint32_t bits, void* ctx) const
        {
            ZoneScopedN("T*");

            auto& [cb, framebufferInfo, sets, _] = *static_cast<RenderContext*>(ctx);

            if (holdsAttachment(bits))
            {
                if (!framebufferInfo)
                    framebufferInfo.emplace().area = {.extent = texture->getExtent()};

                switch (decodeAttachment(bits).imageAspect)
                {
                    using enum rhi::ImageAspect;

                    case eDepth:
                        framebufferInfo->depthAttachment = rhi::AttachmentInfo {.target = texture};
                        framebufferInfo->depthReadOnly   = true;
                        break;
                    case eStencil:
                        framebufferInfo->stencilAttachment = rhi::AttachmentInfo {.target = texture};
                        framebufferInfo->stencilReadOnly   = true;
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
                            VkImageSubresourceRange {
                                .levelCount = VK_REMAINING_MIP_LEVELS,
                                .layerCount = VK_REMAINING_ARRAY_LAYERS,
                            },
                    },
                    {
                        .stageMask  = convert(pipelineStage),
                        .accessMask = dstAccess,
                    });
            }
        }
        void FrameGraphTexture::preWrite(const Desc&, const uint32_t bits, void* ctx) const
        {
            ZoneScopedN("+T");

            auto& [cb, framebufferInfo, sets, _] = *static_cast<RenderContext*>(ctx);

            if (holdsAttachment(bits))
            {
                if (!framebufferInfo)
                    framebufferInfo.emplace().area = {.extent = texture->getExtent()};

                const auto attachment = decodeAttachment(bits);

                switch (attachment.imageAspect)
                {
                    using enum rhi::ImageAspect;

                    case eDepth:
                        framebufferInfo->depthAttachment = makeAttachment(attachment, texture);
                        framebufferInfo->depthReadOnly   = false;
                        break;
                    case eStencil:
                        framebufferInfo->stencilAttachment = makeAttachment(attachment, texture);
                        framebufferInfo->stencilReadOnly   = false;
                        break;
                    case eColor: {
                        auto& v = framebufferInfo->colorAttachments;
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
                        .subresourceRange = {vk::ImageAspectFlagBits::eNone, 0, 1, 0, 1},
                    },
                    {
                        .stageMask  = convert(pipelineStage),
                        .accessMask = rhi::Access::eShaderStorageWrite,
                    });
            }
        }

        std::string FrameGraphTexture::toString(const Desc& desc)
        {
            return std::format("{}x{} [{}]<BR/>Size = ~{}<BR/>Usage = {}",
                               desc.extent.width,
                               desc.extent.height,
                               rhi::toString(desc.format),
                               util::formatBytes(getApproximateSize(desc)),
                               rhi::toString(desc.usageFlags));
        }
    } // namespace framegraph
} // namespace vultra