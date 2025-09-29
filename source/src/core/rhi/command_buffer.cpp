#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/base/visitor_helper.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/raytracing/shader_binding_table.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"
#include "vultra/core/rhi/vk/macro.hpp"

#include <glm/gtc/type_ptr.hpp> // value_ptr

namespace vultra
{
    namespace rhi
    {
        [[nodiscard]] vk::ImageAspectFlags toVk(const ImageAspect imageAspect)
        {
            switch (imageAspect)
            {
                using enum ImageAspect;

                case eDepth:
                    return vk::ImageAspectFlagBits::eDepth;
                case eStencil:
                    return vk::ImageAspectFlagBits::eStencil;
                case eColor:
                    return vk::ImageAspectFlagBits::eColor;

                default:
                    assert(false);
                    return vk::ImageAspectFlagBits::eNone;
            }
        }

        namespace
        {
            constexpr vk::DeviceSize kMaxDataSize {65536};

            [[nodiscard]] vk::IndexType toVk(const IndexType indexType)
            {
                switch (indexType)
                {
                    case IndexType::eUInt16:
                        return vk::IndexType::eUint16;
                    case IndexType::eUInt32:
                        return vk::IndexType::eUint32;

                    default:
                        assert(false);
                        return vk::IndexType::eNoneKHR;
                }
            }

            [[nodiscard]] auto toVk(const ClearValue& clearValue)
            {
                return std::visit(
                    Overload {
                        [](const glm::vec4& v) {
                            vk::ClearValue result {};
                            // Sonarlint is wrong (S3519)
                            // The following .color is a float[4] array (same size as vec4).
                            std::memcpy(&result.color.float32, glm::value_ptr(v), sizeof(glm::vec4));
                            return result;
                        },
                        [](const glm::ivec4& v) {
                            vk::ClearValue result {};
                            std::memcpy(&result.color.int32, glm::value_ptr(v), sizeof(glm::ivec4));
                            return result;
                        },
                        [](const glm::uvec4& v) {
                            vk::ClearValue result {};
                            std::memcpy(&result.color.uint32, glm::value_ptr(v), sizeof(glm::uvec4));
                            return result;
                        },
                        [](const float v) { return vk::ClearValue {vk::ClearDepthStencilValue {v, 0}}; },
                        [](const uint32_t v) { return vk::ClearValue {vk::ClearDepthStencilValue {0, v}}; },
                    },
                    clearValue);
            }
            [[nodiscard]] vk::RenderingAttachmentInfo toVk(const AttachmentInfo& attachment, const bool readOnly)
            {
                const auto& [target, layer, face, clearValue] = attachment;
                assert(!readOnly || !clearValue.has_value());
                vk::RenderingAttachmentInfo attachmentInfo {};
                attachmentInfo.imageView   = layer ? target->getLayer(*layer, face) : target->getImageView();
                attachmentInfo.imageLayout = static_cast<vk::ImageLayout>(target->getImageLayout());
                attachmentInfo.resolveMode = vk::ResolveModeFlagBits::eNone;
                attachmentInfo.loadOp      = clearValue ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                attachmentInfo.storeOp     = readOnly ? vk::AttachmentStoreOp::eNone : vk::AttachmentStoreOp::eStore;
                attachmentInfo.clearValue  = clearValue ? toVk(*clearValue) : vk::ClearValue {};
                return attachmentInfo;
            }
        } // namespace

#define TRACY_GPU_ZONE2_(Label) TRACY_GPU_ZONE_(m_TracyContext, m_Handle, "RHI::" Label)

        CommandBuffer::CommandBuffer() { m_DescriptorSetCache.reserve(100); }

        CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept :
            m_Device(other.m_Device), m_CommandPool(other.m_CommandPool), m_State(other.m_State),
            m_Handle(other.m_Handle), m_TracyContext(other.m_TracyContext), m_Fence(other.m_Fence),
            m_DescriptorSetAllocator(std::move(other.m_DescriptorSetAllocator)),
            m_DescriptorSetCache(std::move(other.m_DescriptorSetCache)),
            m_BarrierBuilder(std::move(other.m_BarrierBuilder)), m_Pipeline(other.m_Pipeline),
            m_VertexBuffer(other.m_VertexBuffer), m_IndexBuffer(other.m_IndexBuffer),
            m_InsideRenderPass(other.m_InsideRenderPass)
        {
            other.m_Device           = nullptr;
            other.m_CommandPool      = nullptr;
            other.m_Handle           = nullptr;
            other.m_TracyContext     = nullptr;
            other.m_Fence            = nullptr;
            other.m_State            = State::eInvalid;
            other.m_Pipeline         = nullptr;
            other.m_VertexBuffer     = nullptr;
            other.m_IndexBuffer      = nullptr;
            other.m_InsideRenderPass = false;
        }

        CommandBuffer::~CommandBuffer() { destroy(); }

        CommandBuffer& CommandBuffer::operator=(CommandBuffer&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                std::swap(m_Device, rhs.m_Device);
                std::swap(m_CommandPool, rhs.m_CommandPool);

                std::swap(m_State, rhs.m_State);

                std::swap(m_Handle, rhs.m_Handle);
                std::swap(m_TracyContext, rhs.m_TracyContext);

                std::swap(m_Fence, rhs.m_Fence);

                std::swap(m_DescriptorSetAllocator, rhs.m_DescriptorSetAllocator);
                std::swap(m_DescriptorSetCache, rhs.m_DescriptorSetCache);

                std::swap(m_BarrierBuilder, rhs.m_BarrierBuilder);

                std::swap(m_Pipeline, rhs.m_Pipeline);
                std::swap(m_VertexBuffer, rhs.m_VertexBuffer);
                std::swap(m_IndexBuffer, rhs.m_IndexBuffer);

                std::swap(m_InsideRenderPass, rhs.m_InsideRenderPass);
            }

            return *this;
        }

        vk::CommandBuffer CommandBuffer::getHandle() const { return m_Handle; }

        TracyVkCtx CommandBuffer::getTracyContext() const { return m_TracyContext; }

        Barrier::Builder& CommandBuffer::getBarrierBuilder() { return m_BarrierBuilder; }

        DescriptorSetBuilder CommandBuffer::createDescriptorSetBuilder()
        {
            return DescriptorSetBuilder {m_Device, m_DescriptorSetAllocator, m_DescriptorSetCache};
        }

        CommandBuffer& CommandBuffer::begin()
        {
            assert(invariant(State::eInitial));

            VK_CHECK(m_Device.resetFences(1, &m_Fence), "CommandBuffer", "Failed to reset fence");

            vk::CommandBufferBeginInfo beginInfo {};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            VK_CHECK(m_Handle.begin(&beginInfo), "CommandBuffer", "Failed to begin command buffer");

            m_State = State::eRecording;
            return *this;
        }

        CommandBuffer& CommandBuffer::end()
        {
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            m_Handle.end();

            m_State = State::eExecutable;

            m_Pipeline     = nullptr;
            m_VertexBuffer = nullptr;
            m_IndexBuffer  = nullptr;

            return *this;
        }

        CommandBuffer& CommandBuffer::reset()
        {
            assert(m_State != State::eRecording);

            if (m_State == State::ePending)
            {
                assert(m_Handle);

                VK_CHECK(m_Device.waitForFences(1, &m_Fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
                         "CommandBuffer",
                         "Failed to wait for fence");

                m_Handle.reset(vk::CommandBufferResetFlagBits::eReleaseResources);

                m_DescriptorSetCache.clear();
                m_DescriptorSetAllocator.reset();

                m_State = State::eInitial;
            }

            return *this;
        }

        CommandBuffer& CommandBuffer::bindPipeline(const BasePipeline& pipeline)
        {
            assert(pipeline);
            assert(invariant(State::eRecording));

            if (m_Pipeline != &pipeline)
            {
                TRACY_GPU_ZONE2_("BindPipeline");
                m_Handle.bindPipeline(pipeline.getBindPoint(), pipeline.getHandle());
                m_Pipeline = std::addressof(pipeline);
            }

            return *this;
        }

        CommandBuffer& CommandBuffer::dispatch(const ComputePipeline& pipeline, const glm::uvec3& groupCount)
        {
            return bindPipeline(pipeline).dispatch(groupCount);
        }

        CommandBuffer& CommandBuffer::dispatch(const glm::uvec3& groupCount)
        {
            assert(invariant(State::eRecording, InvariantFlags::eValidComputePipeline));

            TRACY_GPU_ZONE2_("Dispatch");
            flushBarriers();
            m_Handle.dispatch(groupCount.x, groupCount.y, groupCount.z);

            return *this;
        }

        CommandBuffer& CommandBuffer::traceRays(const ShaderBindingTable& sbt, const glm::uvec3& extent)
        {
            assert(invariant(State::eRecording, InvariantFlags::eValidRayTracingPipeline));

            TRACY_GPU_ZONE2_("TraceRays");
            flushBarriers();

            vk::StridedDeviceAddressRegionKHR raygenShaderBindingTable {};
            vk::StridedDeviceAddressRegionKHR missShaderBindingTable {};
            vk::StridedDeviceAddressRegionKHR hitShaderBindingTable {};
            vk::StridedDeviceAddressRegionKHR callableShaderBindingTable {};

            raygenShaderBindingTable.deviceAddress = sbt.regions().raygen.deviceAddress;
            raygenShaderBindingTable.stride        = sbt.regions().raygen.stride;
            raygenShaderBindingTable.size          = sbt.regions().raygen.size;

            missShaderBindingTable.deviceAddress = sbt.regions().miss.deviceAddress;
            missShaderBindingTable.stride        = sbt.regions().miss.stride;
            missShaderBindingTable.size          = sbt.regions().miss.size;

            hitShaderBindingTable.deviceAddress = sbt.regions().hit.deviceAddress;
            hitShaderBindingTable.stride        = sbt.regions().hit.stride;
            hitShaderBindingTable.size          = sbt.regions().hit.size;

            if (sbt.regions().callable.has_value())
            {
                callableShaderBindingTable.deviceAddress = sbt.regions().callable->deviceAddress;
                callableShaderBindingTable.stride        = sbt.regions().callable->stride;
                callableShaderBindingTable.size          = sbt.regions().callable->size;
            }

            m_Handle.traceRaysKHR(&raygenShaderBindingTable,
                                  &missShaderBindingTable,
                                  &hitShaderBindingTable,
                                  &callableShaderBindingTable,
                                  extent.x,
                                  extent.y,
                                  extent.z);

            return *this;
        }

        CommandBuffer& CommandBuffer::bindDescriptorSet(const DescriptorSetIndex index,
                                                        const vk::DescriptorSet  descriptorSet)
        {
            assert(descriptorSet);
            assert(invariant(State::eRecording, InvariantFlags::eValidPipeline));

            TRACY_GPU_ZONE2_("BindDescriptorSet");
            m_Handle.bindDescriptorSets(
                m_Pipeline->getBindPoint(), m_Pipeline->getLayout().getHandle(), index, 1, &descriptorSet, 0, nullptr);

            return *this;
        }

        CommandBuffer& CommandBuffer::pushConstants(const ShaderStages shaderStages,
                                                    const uint32_t     offset,
                                                    const uint32_t     size,
                                                    const void*        data)
        {
            assert(data && size > 0);
            assert(invariant(State::eRecording, InvariantFlags::eValidPipeline));

            TRACY_GPU_ZONE2_("PushConstants");
            m_Handle.pushConstants(m_Pipeline->getLayout().getHandle(),
                                   static_cast<vk::ShaderStageFlagBits>(shaderStages),
                                   offset,
                                   size,
                                   data);

            return *this;
        }

        CommandBuffer& CommandBuffer::beginRendering(const FramebufferInfo& framebufferInfo)
        {
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("BeginRendering");
            vk::RenderingAttachmentInfo depthAttachment {};
            if (framebufferInfo.depthAttachment)
            {
                depthAttachment = toVk(*framebufferInfo.depthAttachment, framebufferInfo.depthReadOnly);
            }
            vk::RenderingAttachmentInfo stencilAttachment {};
            if (framebufferInfo.stencilAttachment)
            {
                stencilAttachment = toVk(*framebufferInfo.stencilAttachment, framebufferInfo.stencilReadOnly);
            }
            std::vector<vk::RenderingAttachmentInfo> colorAttachments {};
            colorAttachments.reserve(framebufferInfo.colorAttachments.size());
            for (const auto& attachment : framebufferInfo.colorAttachments)
            {
                colorAttachments.push_back(toVk(attachment, false));
            }

            vk::RenderingInfo renderingInfo {};
            renderingInfo.renderArea           = static_cast<vk::Rect2D>(framebufferInfo.area);
            renderingInfo.layerCount           = static_cast<uint32_t>(framebufferInfo.layers);
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
            renderingInfo.pColorAttachments    = colorAttachments.data();
            renderingInfo.pDepthAttachment     = depthAttachment.imageView ? &depthAttachment : nullptr;
            renderingInfo.pStencilAttachment   = stencilAttachment.imageView ? &stencilAttachment : nullptr;

            flushBarriers();
            m_Handle.beginRenderingKHR(&renderingInfo);

            m_InsideRenderPass = true;

            return setViewport(framebufferInfo.area).setScissor(framebufferInfo.area);
        }

        CommandBuffer& CommandBuffer::endRendering()
        {
            assert(invariant(State::eRecording, InvariantFlags::eInsideRenderPass));

            TRACY_GPU_ZONE2_("EndRendering");
            m_Handle.endRenderingKHR();

            m_InsideRenderPass = false;

            return *this;
        }

        CommandBuffer& CommandBuffer::setViewport(const Rect2D& rect)
        {
            assert(invariant(State::eRecording));

            TRACY_GPU_ZONE2_("SetViewport");

            vk::Viewport viewport {};
            viewport.x        = static_cast<float>(rect.offset.x);
            viewport.y        = static_cast<float>(rect.offset.y);
            viewport.width    = static_cast<float>(rect.extent.width);
            viewport.height   = static_cast<float>(rect.extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            m_Handle.setViewport(0, 1, &viewport);
            return *this;
        }

        CommandBuffer& CommandBuffer::setScissor(const Rect2D& rect)
        {
            assert(invariant(State::eRecording));

            TRACY_GPU_ZONE2_("SetScissor");
            const auto scissor = static_cast<vk::Rect2D>(rect);
            m_Handle.setScissor(0, 1, &scissor);

            return *this;
        }

        CommandBuffer& CommandBuffer::draw(const GeometryInfo& gi, const uint32_t numInstances)
        {
            assert(invariant(State::eRecording,
                             InvariantFlags::eValidGraphicsPipeline | InvariantFlags::eInsideRenderPass));

            TRACY_GPU_ZONE2_("Draw");

            constexpr auto kFirstInstance = 0u;
            setVertexBuffer(gi.vertexBuffer, 0);
            if (gi.indexBuffer && gi.numIndices > 0)
            {
                setIndexBuffer(gi.indexBuffer);
                m_Handle.drawIndexed(gi.numIndices, numInstances, gi.indexOffset, gi.vertexOffset, kFirstInstance);
            }
            else
            {
                assert(gi.numVertices > 0);
                m_Handle.draw(gi.numVertices, numInstances, gi.vertexOffset, kFirstInstance);
            }
            return *this;
        }

        CommandBuffer& CommandBuffer::drawFullScreenTriangle() { return draw({.numVertices = 3}); }

        CommandBuffer& CommandBuffer::drawCube() { return draw({.numVertices = 36}); }

        CommandBuffer& CommandBuffer::clear(const Buffer& buffer, const uint32_t value)
        {
            assert(buffer);
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("ClearBuffer");
            flushBarriers();

            m_Handle.fillBuffer(buffer.getHandle(), 0, vk::WholeSize, value);
            return *this;
        }

        CommandBuffer& CommandBuffer::clear(Texture& texture, const ClearValue& clearValue)
        {
            assert(texture && static_cast<bool>(texture.getUsageFlags() & ImageUsage::eTransferDst));
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("ClearTexture");

            const auto                imageHandle = texture.getImageHandle();
            const auto                imageLayout = static_cast<vk::ImageLayout>(texture.getImageLayout());
            const auto                v           = toVk(clearValue);
            vk::ImageSubresourceRange range {};
            range.aspectMask = getAspectMask(texture);
            range.levelCount = vk::RemainingMipLevels;
            range.layerCount = vk::RemainingArrayLayers;

            flushBarriers();
            if (range.aspectMask & vk::ImageAspectFlagBits::eColor)
            {
                m_Handle.clearColorImage(imageHandle, imageLayout, &v.color, 1, &range);
            }
            else
            {
                m_Handle.clearDepthStencilImage(imageHandle, imageLayout, &v.depthStencil, 1, &range);
            }
            return *this;
        }

        CommandBuffer& CommandBuffer::copyBuffer(const Buffer& src, Buffer& dst, const vk::BufferCopy& copyRegion)
        {
            assert(src && dst);
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("CopyBuffer");
            flushBarriers();

            m_Handle.copyBuffer(src.getHandle(), dst.getHandle(), 1, &copyRegion);
            return *this;
        }

        CommandBuffer& CommandBuffer::copyBuffer(const Buffer& src, Texture& dst)
        {
            const auto extent = dst.getExtent();

            vk::BufferImageCopy copyRegion {};
            copyRegion.imageSubresource.aspectMask = getAspectMask(dst);
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageExtent.width           = extent.width;
            copyRegion.imageExtent.height          = extent.height;
            copyRegion.imageExtent.depth           = 1;

            return copyBuffer(src, dst, std::array {copyRegion});
        }

        CommandBuffer&
        CommandBuffer::copyBuffer(const Buffer& src, Texture& dst, std::span<const vk::BufferImageCopy> copyRegions)
        {
            assert(src && dst && !copyRegions.empty());
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("Buffer->Texture");

            constexpr auto kExpectedLayout = ImageLayout::eTransferDst;
            m_BarrierBuilder.imageBarrier(
                {.image = dst, .newLayout = kExpectedLayout},
                {.stageMask = PipelineStages::eTransfer, .accessMask = Access::eTransferWrite});
            flushBarriers();
            m_Handle.copyBufferToImage(
                src.getHandle(), dst.getImageHandle(), static_cast<vk::ImageLayout>(kExpectedLayout), copyRegions);

            return *this;
        }

        CommandBuffer& CommandBuffer::copyImage(const Texture& src, const Buffer& dst, const ImageAspect aspectMask)
        {
            assert(src && dst);
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            const auto extent = src.getExtent();

            vk::BufferImageCopy2 region {};
            region.imageSubresource.aspectMask = toVk(aspectMask);
            region.imageSubresource.layerCount = 1;
            region.imageExtent.width           = extent.width;
            region.imageExtent.height          = extent.height;
            region.imageExtent.depth           = 1;

            vk::CopyImageToBufferInfo2 info {};
            info.srcImage       = src.getImageHandle();
            info.srcImageLayout = static_cast<vk::ImageLayout>(src.getImageLayout());
            info.dstBuffer      = dst.getHandle();
            info.regionCount    = 1;
            info.pRegions       = &region;

            flushBarriers();
            m_Handle.copyImageToBuffer2(&info);

            return *this;
        }

        CommandBuffer&
        CommandBuffer::update(Buffer& buffer, const vk::DeviceSize offset, const vk::DeviceSize size, const void* data)
        {
            assert(buffer && data);
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("UpdateBuffer");

            flushBarriers();
            if (size > kMaxDataSize)
            {
                chunkedUpdate(buffer.getHandle(), offset, size, data);
            }
            else
            {
                m_Handle.updateBuffer(buffer.getHandle(), offset, size, data);
            }
            return *this;
        }

        CommandBuffer& CommandBuffer::blit(Texture&         src,
                                           Texture&         dst,
                                           const vk::Filter filter,
                                           uint32_t         srcMipLevel,
                                           uint32_t         dstMipLevel)
        {
            assert(src && static_cast<bool>(src.getUsageFlags() & ImageUsage::eTransferSrc));
            const auto aspectMask = getAspectMask(dst);
            assert(getAspectMask(src) == aspectMask);
            assert(dst && static_cast<bool>(dst.getUsageFlags() & ImageUsage::eTransferDst));
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("Texture->Texture");

            getBarrierBuilder()
                .imageBarrier(
                    {
                        .image     = src,
                        .newLayout = ImageLayout::eTransferSrc,
                        .subresourceRange =
                            vk::ImageSubresourceRange {
                                aspectMask,
                                srcMipLevel,
                                1u,
                                0u,
                                1u,
                            },
                    },
                    {
                        .stageMask  = PipelineStages::eTransfer,
                        .accessMask = Access::eTransferRead,
                    })
                .imageBarrier(
                    {
                        .image     = dst,
                        .newLayout = ImageLayout::eTransferDst,
                        .subresourceRange =
                            vk::ImageSubresourceRange {
                                aspectMask,
                                dstMipLevel,
                                1u,
                                0u,
                                1u,
                            },
                    },
                    {
                        .stageMask  = PipelineStages::eTransfer,
                        .accessMask = Access::eTransferWrite,
                    });

            flushBarriers();

            static const auto GetRegion = [](const Texture& texture) {
                const auto extent = texture.getExtent();
                return vk::Offset3D {
                    static_cast<int32_t>(extent.width),
                    static_cast<int32_t>(extent.height),
                    1,
                };
            };

            vk::ImageBlit region {};
            region.srcSubresource.aspectMask = aspectMask;
            region.srcSubresource.mipLevel   = srcMipLevel;
            region.srcSubresource.layerCount = 1;
            region.srcOffsets                = std::array<vk::Offset3D, 2> {vk::Offset3D {}, GetRegion(src)};
            region.dstSubresource.aspectMask = aspectMask;
            region.dstSubresource.mipLevel   = dstMipLevel;
            region.dstSubresource.layerCount = 1;
            region.dstOffsets                = std::array<vk::Offset3D, 2> {vk::Offset3D {}, GetRegion(src)};

            m_Handle.blitImage(src.getImageHandle(),
                               static_cast<vk::ImageLayout>(src.getImageLayout()),
                               dst.getImageHandle(),
                               static_cast<vk::ImageLayout>(dst.getImageLayout()),
                               1,
                               &region,
                               filter);

            return *this;
        }

        CommandBuffer& CommandBuffer::generateMipmaps(Texture& texture, const TexelFilter filter)
        {
            assert(texture);
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("GenerateMipmaps");

            const auto imageHandle = texture.getImageHandle();
            m_BarrierBuilder.imageBarrier(
                {
                    .image     = texture,
                    .newLayout = ImageLayout::eTransferSrc,
                    .subresourceRange =
                        vk::ImageSubresourceRange {
                            vk::ImageAspectFlagBits::eColor,
                            0u,
                            1u,
                            0u,
                            texture.m_LayerFaces,
                        },
                },
                {
                    .stageMask  = PipelineStages::eTransfer,
                    .accessMask = Access::eTransferRead,
                });
            flushBarriers();

            // Generate the mip chain, blit level n from level n-1.
            for (auto i = 1u; i < texture.getNumMipLevels(); ++i)
            {
                const vk::ImageSubresourceRange mipSubRange {
                    vk::ImageAspectFlagBits::eColor,
                    i,
                    1u,
                    0u,
                    texture.m_LayerFaces,
                };

                // Transition current mip level to transfer dst
                m_BarrierBuilder.imageBarrier(imageHandle,
                                              {
                                                  .stageMask  = PipelineStages::eNone,
                                                  .accessMask = Access::eNone,
                                              },
                                              {
                                                  .stageMask  = PipelineStages::eTransfer,
                                                  .accessMask = Access::eTransferWrite,
                                              },
                                              ImageLayout::eUndefined,
                                              ImageLayout::eTransferDst,
                                              mipSubRange);

                flushBarriers();

                vk::ImageBlit blitInfo {};
                blitInfo.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                blitInfo.srcSubresource.mipLevel   = i - 1;
                blitInfo.srcSubresource.layerCount = texture.m_LayerFaces;
                blitInfo.srcOffsets =
                    std::array {vk::Offset3D {},
                                vk::Offset3D {
                                    std::max(1, static_cast<int32_t>(texture.m_Extent.width) >> (i - 1)),
                                    std::max(1, static_cast<int32_t>(texture.m_Extent.height) >> (i - 1)),
                                    std::max(1, static_cast<int32_t>(texture.m_Depth) >> (i - 1)),
                                }};
                blitInfo.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                blitInfo.dstSubresource.mipLevel   = i;
                blitInfo.dstSubresource.layerCount = texture.m_LayerFaces;
                blitInfo.dstOffsets                = std::array {vk::Offset3D {},
                                                  vk::Offset3D {
                                                      std::max(1, static_cast<int32_t>(texture.m_Extent.width) >> i),
                                                      std::max(1, static_cast<int32_t>(texture.m_Extent.height) >> i),
                                                      std::max(1, static_cast<int32_t>(texture.m_Depth) >> i),
                                                  }};

                // Blit from previous level
                m_Handle.blitImage(imageHandle,
                                   vk::ImageLayout::eTransferSrcOptimal,
                                   imageHandle,
                                   vk::ImageLayout::eTransferDstOptimal,
                                   1,
                                   &blitInfo,
                                   static_cast<vk::Filter>(filter));

                // Transition current mip level to transfer src
                m_BarrierBuilder.imageBarrier(imageHandle,
                                              {
                                                  .stageMask  = PipelineStages::eBlit,
                                                  .accessMask = Access::eTransferWrite,
                                              },
                                              {
                                                  .stageMask  = PipelineStages::eTransfer,
                                                  .accessMask = Access::eTransferRead,
                                              },
                                              ImageLayout::eTransferDst,
                                              ImageLayout::eTransferSrc,
                                              mipSubRange);

                flushBarriers();
            }

            // After the loop, all mip layers are in transfer src layout.
            texture.m_Layout    = ImageLayout::eTransferSrc;
            texture.m_LastScope = {
                .stageMask  = PipelineStages::eBlit,
                .accessMask = Access::eTransferRead,
            };

            return *this;
        }

        CommandBuffer& CommandBuffer::flushBarriers()
        {
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            if (auto barrier = m_BarrierBuilder.build(); barrier.isEffective())
            {
                TRACY_GPU_ZONE2_("FlushBarriers");
                m_Handle.pipelineBarrier2KHR(&barrier.m_Info);
            }
            return *this;
        }

        CommandBuffer::CommandBuffer(const vk::Device        device,
                                     const vk::CommandPool   commandPool,
                                     const vk::CommandBuffer handle,
                                     TracyVkCtx              tracyContext,
                                     const vk::Fence         fence,
                                     const bool              enableRaytracing) :
            m_Device(device), m_CommandPool(commandPool), m_State(State::eInitial), m_Handle(handle),
            m_TracyContext(tracyContext), m_Fence(fence), m_DescriptorSetAllocator(device, enableRaytracing)
        {}

        bool CommandBuffer::invariant(const State requiredState, const InvariantFlags flags) const
        {
            if (!m_Handle || m_State != requiredState)
                return false;

            auto valid = true;
            if (static_cast<bool>(flags & InvariantFlags::eValidPipeline))
            {
                valid |= m_Pipeline && *m_Pipeline;
            }
            if (static_cast<bool>(flags & InvariantFlags::eGraphicsPipeline))
            {
                assert(m_Pipeline);
                valid |= m_Pipeline->getBindPoint() == vk::PipelineBindPoint::eGraphics;
            }
            if (static_cast<bool>(flags & InvariantFlags::eComputePipeline))
            {
                assert(m_Pipeline);
                valid |= m_Pipeline->getBindPoint() == vk::PipelineBindPoint::eCompute;
            }
            if (static_cast<bool>(flags & InvariantFlags::eRayTracingPipeline))
            {
                assert(m_Pipeline);
                valid |= m_Pipeline->getBindPoint() == vk::PipelineBindPoint::eRayTracingKHR;
            }
            if (static_cast<bool>(flags & InvariantFlags::eInsideRenderPass))
            {
                valid |= m_InsideRenderPass;
            }
            if (static_cast<bool>(flags & InvariantFlags::eOutsideRenderPass))
            {
                valid |= !m_InsideRenderPass;
            }
            return valid;
        }

        void CommandBuffer::destroy() noexcept
        {
            if (!m_Handle)
            {
                return;
            }

            m_Device.waitIdle();

            reset();

            m_Device.destroyFence(m_Fence);
            m_Device.freeCommandBuffers(m_CommandPool, 1, &m_Handle);

            m_Device      = nullptr;
            m_CommandPool = nullptr;

            m_State = State::eInvalid;

            m_Handle       = nullptr;
            m_TracyContext = nullptr;

            m_Fence = nullptr;
            m_DescriptorSetAllocator.reset();
            m_DescriptorSetCache.clear();

            m_Pipeline     = nullptr;
            m_VertexBuffer = nullptr;
            m_IndexBuffer  = nullptr;

            m_InsideRenderPass = false;
        }

        void CommandBuffer::chunkedUpdate(const vk::Buffer bufferHandle,
                                          vk::DeviceSize   offset,
                                          vk::DeviceSize   size,
                                          const void*      data) const
        {
            const auto numChunks =
                static_cast<vk::DeviceSize>(std::ceil(static_cast<float>(size) / static_cast<float>(kMaxDataSize)));
            assert(numChunks > 1);

            const auto* bytes = static_cast<const std::byte*>(data);
            for (auto i = 0u; i < numChunks; ++i)
            {
                const auto chunkSize = std::min(size, kMaxDataSize);
                m_Handle.updateBuffer(bufferHandle, offset, chunkSize, bytes);

                bytes += chunkSize;
                offset += chunkSize;
                size -= chunkSize;
            }
        }

        void CommandBuffer::setVertexBuffer(const VertexBuffer* vertexBuffer, const vk::DeviceSize offset)
        {
            if (vertexBuffer == m_VertexBuffer)
            {
                return;
            }

            if (vertexBuffer)
            {
                TRACY_GPU_ZONE2_("SetVertexBuffer");
                const auto bufferHandle = vertexBuffer->getHandle();
                m_Handle.bindVertexBuffers(0, 1, &bufferHandle, &offset);
            }
            m_VertexBuffer = vertexBuffer;
        }

        void CommandBuffer::setIndexBuffer(const IndexBuffer* indexBuffer)
        {
            if (indexBuffer == m_IndexBuffer)
            {
                return;
            }

            if (indexBuffer)
            {
                TRACY_GPU_ZONE2_("SetIndexBuffer");
                const auto indexType = toVk(indexBuffer->getIndexType());
                m_Handle.bindIndexBuffer(indexBuffer->getHandle(), 0, indexType);
            }
            m_IndexBuffer = indexBuffer;
        }

        void CommandBuffer::pushDebugGroup(const std::string_view label) const
        {
            assert(invariant(State::eRecording));

            vk::DebugUtilsLabelEXT labelInfo = {};
            labelInfo.pLabelName             = label.data();

            m_Handle.beginDebugUtilsLabelEXT(&labelInfo);
        }

        void CommandBuffer::popDebugGroup() const
        {
            assert(invariant(State::eRecording));
            m_Handle.endDebugUtilsLabelEXT();
        }

        void prepareForAttachment(CommandBuffer& cb, const Texture& texture, const bool readOnly)
        {
            assert(texture);

            BarrierScope dst {};
            ImageLayout  newLayout {ImageLayout::eUndefined};

            const auto aspectMask = getAspectMask(texture);

            if (aspectMask & vk::ImageAspectFlagBits::eColor)
            {
                dst.stageMask  = PipelineStages::eColorAttachmentOutput;
                dst.accessMask = Access::eColorAttachmentRead | Access::eColorAttachmentWrite;
                newLayout      = ImageLayout::eAttachment;
            }
            else
            {
                dst.stageMask  = PipelineStages::eFragmentTests;
                dst.accessMask = readOnly ? Access::eDepthStencilAttachmentRead : Access::eDepthStencilAttachmentWrite;
                newLayout      = readOnly ? ImageLayout::eReadOnly : ImageLayout::eAttachment;
            }

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = texture,
                    .newLayout = newLayout,
                    .subresourceRange =
                        vk::ImageSubresourceRange {
                            aspectMask,
                            0u,
                            vk::RemainingMipLevels,
                            0u,
                            vk::RemainingArrayLayers,
                        },
                },
                dst);
        }

        void prepareForReading(CommandBuffer& cb, const Texture& texture, uint32_t mipLevel, uint32_t layer)
        {
            assert(texture);
            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = texture,
                    .newLayout = ImageLayout::eReadOnly,
                    .subresourceRange =
                        vk::ImageSubresourceRange {
                            vk::ImageAspectFlagBits::eNone,
                            mipLevel,
                            mipLevel > 0 ? 1 : vk::RemainingMipLevels,
                            layer,
                            layer > 0 ? 1 : vk::RemainingArrayLayers,
                        },
                },
                {
                    .stageMask  = PipelineStages::eVertexShader | PipelineStages::eFragmentShader,
                    .accessMask = Access::eShaderRead,
                });
        }

        void clearImageForComputing(CommandBuffer& cb, Texture& texture, const ClearValue& clearValue)
        {
            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = texture,
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .stageMask  = rhi::PipelineStages::eTransfer,
                    .accessMask = rhi::Access::eTransferWrite,
                });
            cb.clear(texture, clearValue);
        }

        void prepareForComputing(CommandBuffer& cb, const Texture& texture)
        {
            assert(texture);

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = texture,
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .stageMask  = rhi::PipelineStages::eComputeShader,
                    .accessMask = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                });
        }

        void prepareForRaytracing(CommandBuffer& cb, const Texture& texture)
        {
            assert(texture);

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = texture,
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .stageMask  = rhi::PipelineStages::eRaytracingShader,
                    .accessMask = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                });
        }

        void prepareForComputing(CommandBuffer& cb, const Buffer& buffer)
        {
            assert(buffer);

            cb.getBarrierBuilder().bufferBarrier(
                {
                    .buffer = buffer,
                },
                {
                    .stageMask  = rhi::PipelineStages::eComputeShader,
                    .accessMask = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                });
        }

        void prepareForReading(CommandBuffer& cb, const Buffer& buffer)
        {
            assert(buffer);

            cb.getBarrierBuilder().bufferBarrier(
                {
                    .buffer = buffer,
                    .offset = 0,
                    .size   = buffer.getSize(),
                },
                {
                    .stageMask  = rhi::PipelineStages::eVertexShader | rhi::PipelineStages::eFragmentShader,
                    .accessMask = rhi::Access::eShaderRead,
                });
        }
    } // namespace rhi
} // namespace vultra