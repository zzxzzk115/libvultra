#include "vultra/core/rhi/backends/vk/vulkan_command_buffer.hpp"
#include "vultra/core/base/visitor_helper.hpp"
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_descriptor_set_allocator.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_descriptor_set_builder.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_render_device_access.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/interfaces/texture_access.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_binding_table.hpp"
#include "vultra/core/rhi/structs/draw_indirect_info.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <glm/gtc/type_ptr.hpp> // value_ptr
#include <memory>

namespace vultra
{
    namespace rhi
    {
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

            using VkPostBeginCommandBufferHook =
                void(VKAPI_PTR*)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
            using VkPostCmdBindPipelineHook =
                void(VKAPI_PTR*)(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
            using VkPostCmdBindDescriptorSetsHook = void(VKAPI_PTR*)(VkCommandBuffer,
                                                                     VkPipelineBindPoint,
                                                                     VkPipelineLayout,
                                                                     uint32_t,
                                                                     uint32_t,
                                                                     const VkDescriptorSet*,
                                                                     uint32_t,
                                                                     const uint32_t*);

            [[nodiscard]] VulkanHookTable vulkanHooks(const RenderDevice* renderDevice)
            {
                return renderDevice != nullptr ? VulkanRenderDeviceAccess::getVulkanHooks(*renderDevice) :
                                                 VulkanHookTable {};
            }

            void notifyPostBeginCommandBuffer(const RenderDevice*                 renderDevice,
                                              const vk::CommandBuffer             commandBuffer,
                                              const vk::CommandBufferBeginInfo&   beginInfo)
            {
                const auto hooks = vulkanHooks(renderDevice);
                if (hooks.vkPostBeginCommandBuffer == 0)
                    return;
                auto* hook = reinterpret_cast<VkPostBeginCommandBufferHook>(hooks.vkPostBeginCommandBuffer);
                hook(static_cast<VkCommandBuffer>(commandBuffer),
                     reinterpret_cast<const VkCommandBufferBeginInfo*>(&beginInfo));
            }

            void notifyPostCmdBindPipeline(const RenderDevice*     renderDevice,
                                           const vk::CommandBuffer commandBuffer,
                                           const vk::PipelineBindPoint bindPoint,
                                           const vk::Pipeline      pipeline)
            {
                const auto hooks = vulkanHooks(renderDevice);
                if (hooks.vkPostCmdBindPipeline == 0)
                    return;
                auto* hook = reinterpret_cast<VkPostCmdBindPipelineHook>(hooks.vkPostCmdBindPipeline);
                hook(static_cast<VkCommandBuffer>(commandBuffer),
                     static_cast<VkPipelineBindPoint>(bindPoint),
                     static_cast<VkPipeline>(pipeline));
            }

            void notifyPostCmdBindDescriptorSets(const RenderDevice*       renderDevice,
                                                 const vk::CommandBuffer   commandBuffer,
                                                 const vk::PipelineBindPoint bindPoint,
                                                 const vk::PipelineLayout  layout,
                                                 const uint32_t            firstSet,
                                                 const uint32_t            descriptorSetCount,
                                                 const vk::DescriptorSet*  descriptorSets,
                                                 const uint32_t            dynamicOffsetCount,
                                                 const uint32_t*           dynamicOffsets)
            {
                const auto hooks = vulkanHooks(renderDevice);
                if (hooks.vkPostCmdBindDescriptorSets == 0)
                    return;
                auto* hook = reinterpret_cast<VkPostCmdBindDescriptorSetsHook>(hooks.vkPostCmdBindDescriptorSets);
                hook(static_cast<VkCommandBuffer>(commandBuffer),
                     static_cast<VkPipelineBindPoint>(bindPoint),
                     static_cast<VkPipelineLayout>(layout),
                     firstSet,
                     descriptorSetCount,
                     reinterpret_cast<const VkDescriptorSet*>(descriptorSets),
                     dynamicOffsetCount,
                     dynamicOffsets);
            }

            [[nodiscard]] vk::RenderingAttachmentInfo toVk(const AttachmentInfo& attachment, const bool readOnly)
            {
                assert(!readOnly || !attachment.clearValue.has_value());
                vk::RenderingAttachmentInfo attachmentInfo {};
                attachmentInfo.imageView =
                    attachment.layer ?
                        vk::ImageView {asVkHandle<VkImageView>(
                            attachment.target->getLayer(*attachment.layer, attachment.face).getHandle())} :
                        vk::ImageView {asVkHandle<VkImageView>(attachment.target->getImageView().getHandle())};
                attachmentInfo.imageLayout = toVk(attachment.target->getImageLayout());
                attachmentInfo.resolveMode = vk::ResolveModeFlagBits::eNone;
                attachmentInfo.loadOp =
                    attachment.clearValue.has_value() || attachment.loadOp == AttachmentLoadOp::eClear ?
                        vk::AttachmentLoadOp::eClear :
                    attachment.loadOp == AttachmentLoadOp::eDontCare ? vk::AttachmentLoadOp::eDontCare :
                                                                       vk::AttachmentLoadOp::eLoad;
                attachmentInfo.storeOp    = readOnly ? vk::AttachmentStoreOp::eNone : vk::AttachmentStoreOp::eStore;
                attachmentInfo.clearValue = attachment.clearValue ? toVk(*attachment.clearValue) : vk::ClearValue {};
                return attachmentInfo;
            }
        } // namespace

#define TRACY_GPU_ZONE_(TracyContext, CommandBufferHandle, Label) \
    ZoneScopedN(Label); \
    TracyGpuZone(TracyContext, CommandBufferHandle, Label)

#define TRACY_GPU_ZONE2_(Label) TRACY_GPU_ZONE_(m_TracyContext, m_Handle, "RHI::" Label)

        VulkanCommandBuffer::VulkanCommandBuffer() { m_DescriptorSetCache.reserve(100); }

        VulkanCommandBuffer::VulkanCommandBuffer(VulkanCommandBuffer&& other) noexcept :
            m_Device(other.m_Device), m_CommandPool(other.m_CommandPool), m_State(other.m_State),
            m_Handle(other.m_Handle), m_TracyContext(other.m_TracyContext), m_Fence(other.m_Fence),
            m_RenderDevice(other.m_RenderDevice), m_DescriptorSetAllocator(std::move(other.m_DescriptorSetAllocator)),
            m_DescriptorSetCache(std::move(other.m_DescriptorSetCache)),
            m_BarrierBuilder(std::move(other.m_BarrierBuilder)), m_Pipeline(other.m_Pipeline),
            m_VertexBuffer(other.m_VertexBuffer), m_IndexBuffer(other.m_IndexBuffer),
            m_UseKhrDynamicRendering(other.m_UseKhrDynamicRendering),
            m_UseKhrSynchronization2(other.m_UseKhrSynchronization2), m_EnableDebugMarkers(other.m_EnableDebugMarkers),
            m_InsideRenderPass(other.m_InsideRenderPass)
        {
            other.m_Device                 = nullptr;
            other.m_CommandPool            = nullptr;
            other.m_Handle                 = nullptr;
            other.m_TracyContext           = nullptr;
            other.m_Fence                  = nullptr;
            other.m_RenderDevice           = nullptr;
            other.m_State                  = State::eInvalid;
            other.m_Pipeline               = nullptr;
            other.m_VertexBuffer           = nullptr;
            other.m_IndexBuffer            = nullptr;
            other.m_UseKhrDynamicRendering = false;
            other.m_UseKhrSynchronization2 = false;
            other.m_EnableDebugMarkers     = false;
            other.m_InsideRenderPass       = false;
        }

        VulkanCommandBuffer::~VulkanCommandBuffer() { destroy(); }

        VulkanCommandBuffer& VulkanCommandBuffer::operator=(VulkanCommandBuffer&& rhs) noexcept
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
                std::swap(m_RenderDevice, rhs.m_RenderDevice);

                std::swap(m_DescriptorSetAllocator, rhs.m_DescriptorSetAllocator);
                std::swap(m_DescriptorSetCache, rhs.m_DescriptorSetCache);

                std::swap(m_BarrierBuilder, rhs.m_BarrierBuilder);

                std::swap(m_Pipeline, rhs.m_Pipeline);
                std::swap(m_VertexBuffer, rhs.m_VertexBuffer);
                std::swap(m_IndexBuffer, rhs.m_IndexBuffer);

                std::swap(m_UseKhrDynamicRendering, rhs.m_UseKhrDynamicRendering);
                std::swap(m_UseKhrSynchronization2, rhs.m_UseKhrSynchronization2);
                std::swap(m_EnableDebugMarkers, rhs.m_EnableDebugMarkers);
                std::swap(m_InsideRenderPass, rhs.m_InsideRenderPass);
            }

            return *this;
        }

        std::uintptr_t VulkanCommandBuffer::getHandle() const
        {
            return reinterpret_cast<std::uintptr_t>(static_cast<VkCommandBuffer>(m_Handle));
        }

        TracyGpuContext VulkanCommandBuffer::getTracyContext() const { return m_TracyContext; }

        Barrier::Builder& VulkanCommandBuffer::getBarrierBuilder() { return m_BarrierBuilder; }

        DescriptorSetBuilder VulkanCommandBuffer::createDescriptorSetBuilder()
        {
            assert(m_RenderDevice);
            return DescriptorSetBuilder(std::make_unique<VulkanDescriptorSetBuilder>(
                *m_RenderDevice,
                reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(m_Device)),
                m_DescriptorSetAllocator,
                m_DescriptorSetCache));
        }

        VulkanCommandBuffer& VulkanCommandBuffer::begin()
        {
            assert(invariant(State::eInitial));

            VK_CHECK(m_Device.resetFences(1, &m_Fence), "VulkanCommandBuffer", "Failed to reset fence");

            vk::CommandBufferBeginInfo beginInfo {};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            VK_CHECK(m_Handle.begin(&beginInfo), "VulkanCommandBuffer", "Failed to begin command buffer");
            notifyPostBeginCommandBuffer(m_RenderDevice, m_Handle, beginInfo);

            m_State = State::eRecording;
            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::end()
        {
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            m_Handle.end();

            m_State = State::eExecutable;

            m_Pipeline     = nullptr;
            m_VertexBuffer = nullptr;
            m_IndexBuffer  = nullptr;

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::reset()
        {
            assert(m_State != State::eRecording);

            if (m_State == State::ePending)
            {
                assert(m_Handle);

                VK_CHECK(m_Device.waitForFences(1, &m_Fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
                         "VulkanCommandBuffer",
                         "Failed to wait for fence");

                m_Handle.reset(vk::CommandBufferResetFlagBits::eReleaseResources);

                m_DescriptorSetCache.clear();
                m_DescriptorSetAllocator.reset();

                m_State = State::eInitial;
            }

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::submit(const JobInfo& jobInfo, const bool oneTime)
        {
            assert(invariant(State::eExecutable));
            assert(m_RenderDevice);

            TracyGpuCollect(m_TracyContext, m_Handle);

            vk::CommandBufferSubmitInfo commandBufferInfo {};
            commandBufferInfo.commandBuffer = m_Handle;

            vk::SemaphoreSubmitInfo waitSemaphoreInfo {};
            waitSemaphoreInfo.semaphore = vk::Semaphore {asVkHandle<VkSemaphore>(jobInfo.wait.value)};
            waitSemaphoreInfo.stageMask = toVk(jobInfo.waitStage);

            vk::SemaphoreSubmitInfo signalSemaphoreInfo {};
            signalSemaphoreInfo.semaphore = vk::Semaphore {asVkHandle<VkSemaphore>(jobInfo.signal.value)};
            signalSemaphoreInfo.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

            vk::SubmitInfo2 submitInfo {};
            submitInfo.waitSemaphoreInfoCount   = static_cast<bool>(jobInfo.wait) ? 1u : 0u;
            submitInfo.pWaitSemaphoreInfos      = static_cast<bool>(jobInfo.wait) ? &waitSemaphoreInfo : nullptr;
            submitInfo.commandBufferInfoCount   = 1;
            submitInfo.pCommandBufferInfos      = &commandBufferInfo;
            submitInfo.signalSemaphoreInfoCount = static_cast<bool>(jobInfo.signal) ? 1u : 0u;
            submitInfo.pSignalSemaphoreInfos    = static_cast<bool>(jobInfo.signal) ? &signalSemaphoreInfo : nullptr;

            const vk::Queue queue {asVkHandle<VkQueue>(VulkanRenderDeviceAccess::getQueueHandle(*m_RenderDevice))};
            if (m_UseKhrSynchronization2)
            {
                VK_CHECK(queue.submit2KHR(1, &submitInfo, m_Fence),
                         "VulkanCommandBuffer",
                         "Failed to submit command buffer");
            }
            else
            {
                VK_CHECK(
                    queue.submit2(1, &submitInfo, m_Fence), "VulkanCommandBuffer", "Failed to submit command buffer");
            }

            if (oneTime)
            {
                VK_CHECK(m_Device.waitForFences(1, &m_Fence, VK_TRUE, UINT64_MAX),
                         "VulkanCommandBuffer",
                         "Failed to wait for fence");
                VK_CHECK(m_Device.resetFences(1, &m_Fence), "VulkanCommandBuffer", "Failed to reset fence");
                m_State = State::eInitial;
            }
            else
            {
                m_State = State::ePending;
            }

            return *this;
        }

        bool VulkanCommandBuffer::isComplete() const
        {
            if (!m_Handle || !m_Fence)
                return true;
            if (m_State != State::ePending)
                return true;

            const auto status = m_Device.getFenceStatus(m_Fence);
            if (status == vk::Result::eSuccess)
                return true;
            if (status == vk::Result::eNotReady)
                return false;

            VK_CHECK(status, "VulkanCommandBuffer", "Failed to query fence status");
            return false;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::bindPipeline(const BasePipeline& pipeline)
        {
            assert(pipeline);
            assert(invariant(State::eRecording));

            if (m_Pipeline != &pipeline)
            {
                TRACY_GPU_ZONE2_("BindPipeline");
                const auto bindPoint = toVk(pipeline.getBindPoint());
                const vk::Pipeline vkPipeline {asVkHandle<VkPipeline>(pipeline.getHandle())};
                m_Handle.bindPipeline(bindPoint, vkPipeline);
                notifyPostCmdBindPipeline(m_RenderDevice, m_Handle, bindPoint, vkPipeline);
                m_Pipeline = std::addressof(pipeline);
            }

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::dispatch(const ComputePipeline& pipeline,
                                                           const glm::uvec3&      groupCount)
        {
            return bindPipeline(pipeline).dispatch(groupCount);
        }

        VulkanCommandBuffer& VulkanCommandBuffer::dispatch(const glm::uvec3& groupCount)
        {
            assert(invariant(State::eRecording, InvariantFlags::eValidComputePipeline));

            TRACY_GPU_ZONE2_("Dispatch");
            flushBarriers();
            m_Handle.dispatch(groupCount.x, groupCount.y, groupCount.z);

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::dispatchIndirect(const Buffer& buffer, const uint64_t offset)
        {
            assert(buffer);
            assert(invariant(State::eRecording, InvariantFlags::eValidComputePipeline));

            TRACY_GPU_ZONE2_("DispatchIndirect");
            flushBarriers();
            m_Handle.dispatchIndirect(vk::Buffer {asVkHandle<VkBuffer>(buffer.getHandle())},
                                      static_cast<vk::DeviceSize>(offset));

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::insertComputeUavBarrier()
        {
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            m_BarrierBuilder.memoryBarrier(
                {
                    .srcStage  = PipelineStages::eComputeShader,
                    .srcAccess = Access::eShaderRead | Access::eShaderWrite,
                },
                {
                    .dstStage  = PipelineStages::eComputeShader,
                    .dstAccess = Access::eShaderRead | Access::eShaderWrite,
                });
            flushBarriers();

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::traceRays(const ShaderBindingTable& sbt, const glm::uvec3& extent)
        {
            assert(invariant(State::eRecording, InvariantFlags::eValidRayTracingPipeline));

            TRACY_GPU_ZONE2_("TraceRays");
            flushBarriers();

            vk::StridedDeviceAddressRegionKHR raygenShaderBindingTable {};
            vk::StridedDeviceAddressRegionKHR missShaderBindingTable {};
            vk::StridedDeviceAddressRegionKHR hitShaderBindingTable {};
            vk::StridedDeviceAddressRegionKHR callableShaderBindingTable {};

            raygenShaderBindingTable.deviceAddress = sbt.regions().raygen.deviceAddress.value;
            raygenShaderBindingTable.stride        = sbt.regions().raygen.stride;
            raygenShaderBindingTable.size          = sbt.regions().raygen.size;

            missShaderBindingTable.deviceAddress = sbt.regions().miss.deviceAddress.value;
            missShaderBindingTable.stride        = sbt.regions().miss.stride;
            missShaderBindingTable.size          = sbt.regions().miss.size;

            hitShaderBindingTable.deviceAddress = sbt.regions().hit.deviceAddress.value;
            hitShaderBindingTable.stride        = sbt.regions().hit.stride;
            hitShaderBindingTable.size          = sbt.regions().hit.size;

            if (sbt.regions().callable.has_value())
            {
                callableShaderBindingTable.deviceAddress = sbt.regions().callable->deviceAddress.value;
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

        VulkanCommandBuffer& VulkanCommandBuffer::bindDescriptorSet(const DescriptorSetIndex  index,
                                                                    const DescriptorSetHandle descriptorSet)
        {
            assert(static_cast<bool>(descriptorSet));
            assert(invariant(State::eRecording, InvariantFlags::eValidPipeline));

            TRACY_GPU_ZONE2_("BindDescriptorSet");
            const vk::DescriptorSet vkDescriptorSet {asVkHandle<VkDescriptorSet>(descriptorSet.value)};
            const auto bindPoint = toVk(m_Pipeline->getBindPoint());
            const vk::PipelineLayout layout {asVkHandle<VkPipelineLayout>(m_Pipeline->getLayout().getHandle())};
            m_Handle.bindDescriptorSets(
                bindPoint,
                layout,
                index,
                1,
                &vkDescriptorSet,
                0,
                nullptr);
            notifyPostCmdBindDescriptorSets(m_RenderDevice, m_Handle, bindPoint, layout, index, 1, &vkDescriptorSet, 0, nullptr);

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::pushConstants(const ShaderStages shaderStages,
                                                                const uint32_t     offset,
                                                                const uint32_t     size,
                                                                const void*        data)
        {
            assert(data && size > 0);
            assert(invariant(State::eRecording, InvariantFlags::eValidPipeline));

            TRACY_GPU_ZONE2_("PushConstants");
            m_Handle.pushConstants(
                vk::PipelineLayout {asVkHandle<VkPipelineLayout>(m_Pipeline->getLayout().getHandle())},
                toVk(shaderStages),
                offset,
                size,
                data);

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::beginRendering(const FramebufferInfo& framebufferInfo)
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
            renderingInfo.renderArea = toVk(framebufferInfo.area);
            renderingInfo.layerCount =
                framebufferInfo.viewMask != 0u ? 1u : static_cast<uint32_t>(framebufferInfo.layers);
            renderingInfo.viewMask             = framebufferInfo.viewMask;
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
            renderingInfo.pColorAttachments    = colorAttachments.data();
            renderingInfo.pDepthAttachment     = depthAttachment.imageView ? &depthAttachment : nullptr;
            renderingInfo.pStencilAttachment   = stencilAttachment.imageView ? &stencilAttachment : nullptr;

            flushBarriers();
            if (m_UseKhrDynamicRendering)
            {
                m_Handle.beginRenderingKHR(&renderingInfo);
            }
            else
            {
                m_Handle.beginRendering(&renderingInfo);
            }

            m_InsideRenderPass = true;

            return setViewport(framebufferInfo.area).setScissor(framebufferInfo.area);
        }

        VulkanCommandBuffer& VulkanCommandBuffer::endRendering()
        {
            assert(invariant(State::eRecording, InvariantFlags::eInsideRenderPass));

            TRACY_GPU_ZONE2_("EndRendering");
            if (m_UseKhrDynamicRendering)
            {
                m_Handle.endRenderingKHR();
            }
            else
            {
                m_Handle.endRendering();
            }

            m_InsideRenderPass = false;

            return flushBarriers();
        }

        VulkanCommandBuffer& VulkanCommandBuffer::setViewport(const Rect2D& rect)
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

        VulkanCommandBuffer& VulkanCommandBuffer::setScissor(const Rect2D& rect)
        {
            assert(invariant(State::eRecording));

            TRACY_GPU_ZONE2_("SetScissor");
            const auto scissor = toVk(rect);
            m_Handle.setScissor(0, 1, &scissor);

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::draw(const GeometryInfo& gi, const uint32_t numInstances)
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

        VulkanCommandBuffer& VulkanCommandBuffer::drawFullScreenTriangle()
        {
            return draw(GeometryInfo {.numVertices = 3}, 1);
        }

        VulkanCommandBuffer& VulkanCommandBuffer::drawCube() { return draw(GeometryInfo {.numVertices = 36}, 1); }

        VulkanCommandBuffer& VulkanCommandBuffer::drawIndirect(const DrawIndirectInfo& dii)
        {
            assert(invariant(State::eRecording,
                             InvariantFlags::eValidGraphicsPipeline | InvariantFlags::eInsideRenderPass));

            TRACY_GPU_ZONE2_("DrawIndirect");

            const auto& drawIndirectType = dii.buffer->getDrawIndirectType();
            const auto& gi               = dii.gi;

            constexpr auto kFirstInstance = 0u;
            setVertexBuffer(gi.vertexBuffer, 0);

            if (drawIndirectType == DrawIndirectType::eIndexed)
            {
                assert(gi.indexBuffer);
                assert(gi.numIndices > 0);

                setIndexBuffer(gi.indexBuffer);
                m_Handle.drawIndexedIndirect(vk::Buffer {asVkHandle<VkBuffer>(dii.buffer->getHandle())},
                                             dii.firstCommand * dii.buffer->getStride(),
                                             dii.commandCount,
                                             dii.buffer->getStride());
            }
            else
            {
                m_Handle.drawIndirect(vk::Buffer {asVkHandle<VkBuffer>(dii.buffer->getHandle())},
                                      dii.firstCommand * dii.buffer->getStride(),
                                      dii.commandCount,
                                      dii.buffer->getStride());
            }

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::drawIndirectCount(const DrawIndirectInfo& dii,
                                                                    const Buffer&           countBuffer,
                                                                    const uint32_t          countOffset)
        {
            assert(invariant(State::eRecording,
                             InvariantFlags::eValidGraphicsPipeline | InvariantFlags::eInsideRenderPass));

            TRACY_GPU_ZONE2_("DrawIndirectCount");

            const auto& drawIndirectType = dii.buffer->getDrawIndirectType();
            const auto& gi               = dii.gi;

            setVertexBuffer(gi.vertexBuffer, 0);

            if (drawIndirectType == DrawIndirectType::eIndexed)
            {
                assert(gi.indexBuffer);
                assert(gi.numIndices > 0);

                setIndexBuffer(gi.indexBuffer);
                m_Handle.drawIndexedIndirectCount(vk::Buffer {asVkHandle<VkBuffer>(dii.buffer->getHandle())},
                                                  dii.firstCommand * dii.buffer->getStride(),
                                                  vk::Buffer {asVkHandle<VkBuffer>(countBuffer.getHandle())},
                                                  countOffset,
                                                  dii.commandCount,
                                                  dii.buffer->getStride());
            }
            else
            {
                m_Handle.drawIndirectCount(vk::Buffer {asVkHandle<VkBuffer>(dii.buffer->getHandle())},
                                           dii.firstCommand * dii.buffer->getStride(),
                                           vk::Buffer {asVkHandle<VkBuffer>(countBuffer.getHandle())},
                                           countOffset,
                                           dii.commandCount,
                                           dii.buffer->getStride());
            }

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::drawMeshTask(const glm::uvec3& numTaskGroups)
        {
            assert(invariant(State::eRecording,
                             InvariantFlags::eValidGraphicsPipeline | InvariantFlags::eInsideRenderPass));

            TRACY_GPU_ZONE2_("DrawMeshTask");
            m_Handle.drawMeshTasksEXT(numTaskGroups.x, numTaskGroups.y, numTaskGroups.z);

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::clear(const Buffer& buffer, const uint32_t value)
        {
            assert(buffer);
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("ClearBuffer");
            flushBarriers();

            m_Handle.fillBuffer(vk::Buffer {asVkHandle<VkBuffer>(buffer.getHandle())}, 0, vk::WholeSize, value);
            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::clear(Texture& texture, const ClearValue& clearValue)
        {
            assert(texture && static_cast<bool>(texture.getUsageFlags() & ImageUsage::eTransferDst));
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("ClearTexture");

            const auto imageHandle = vk::Image {asVkHandle<VkImage>(TextureAccess::getImageHandle(texture))};
            const auto imageLayout = toVk(texture.getImageLayout());
            const auto v           = toVk(clearValue);
            vk::ImageSubresourceRange range {};
            range.aspectMask = toVk(getAspectMask(texture));
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

        VulkanCommandBuffer&
        VulkanCommandBuffer::copyBuffer(const Buffer& src, Buffer& dst, const rhi::BufferCopy& copyRegion)
        {
            assert(src && dst);
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("CopyBuffer");
            flushBarriers();

            vk::BufferCopy vkCopyRegion {};
            vkCopyRegion.srcOffset = copyRegion.srcOffset;
            vkCopyRegion.dstOffset = copyRegion.dstOffset;
            vkCopyRegion.size      = copyRegion.size;

            m_Handle.copyBuffer(vk::Buffer {asVkHandle<VkBuffer>(src.getHandle())},
                                vk::Buffer {asVkHandle<VkBuffer>(dst.getHandle())},
                                1,
                                &vkCopyRegion);
            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::copyBuffer(const Buffer& src, Texture& dst)
        {
            const auto extent = dst.getExtent();

            BufferImageCopy copyRegion {};
            copyRegion.aspectMask        = getAspectMask(dst);
            copyRegion.layerCount        = 1;
            copyRegion.imageExtentWidth  = extent.width;
            copyRegion.imageExtentHeight = extent.height;
            copyRegion.imageExtentDepth  = 1;

            return copyBuffer(src, dst, std::array {copyRegion});
        }

        VulkanCommandBuffer&
        VulkanCommandBuffer::copyBuffer(const Buffer& src, Texture& dst, std::span<const BufferImageCopy> copyRegions)
        {
            assert(src && dst && !copyRegions.empty());
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("Buffer->Texture");

            std::vector<vk::BufferImageCopy> vkCopyRegions;
            vkCopyRegions.reserve(copyRegions.size());
            for (const auto& region : copyRegions)
            {
                vk::BufferImageCopy vkRegion {};
                vkRegion.bufferOffset                    = region.bufferOffset;
                vkRegion.bufferRowLength                 = region.bufferRowLength;
                vkRegion.bufferImageHeight               = region.bufferImageHeight;
                vkRegion.imageSubresource.aspectMask     = toVk(region.aspectMask);
                vkRegion.imageSubresource.mipLevel       = region.mipLevel;
                vkRegion.imageSubresource.baseArrayLayer = region.baseArrayLayer;
                vkRegion.imageSubresource.layerCount     = region.layerCount;
                vkRegion.imageOffset.x                   = region.imageOffsetX;
                vkRegion.imageOffset.y                   = region.imageOffsetY;
                vkRegion.imageOffset.z                   = region.imageOffsetZ;
                vkRegion.imageExtent.width               = region.imageExtentWidth;
                vkRegion.imageExtent.height              = region.imageExtentHeight;
                vkRegion.imageExtent.depth               = region.imageExtentDepth;
                vkCopyRegions.push_back(vkRegion);
            }

            constexpr auto kExpectedLayout = ImageLayout::eTransferDst;
            m_BarrierBuilder.imageBarrier({.image = dst, .newLayout = kExpectedLayout},
                                          {.dstStage = PipelineStages::eTransfer, .dstAccess = Access::eTransferWrite});
            flushBarriers();
            m_Handle.copyBufferToImage(vk::Buffer {asVkHandle<VkBuffer>(src.getHandle())},
                                       vk::Image {asVkHandle<VkImage>(TextureAccess::getImageHandle(dst))},
                                       toVk(kExpectedLayout),
                                       static_cast<uint32_t>(vkCopyRegions.size()),
                                       vkCopyRegions.data());

            return *this;
        }

        VulkanCommandBuffer&
        VulkanCommandBuffer::copyImage(const Texture& src, const Buffer& dst, const ImageAspect aspectMask)
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
            info.srcImage       = vk::Image {asVkHandle<VkImage>(TextureAccess::getImageHandle(src))};
            info.srcImageLayout = toVk(src.getImageLayout());
            info.dstBuffer      = vk::Buffer {asVkHandle<VkBuffer>(dst.getHandle())};
            info.regionCount    = 1;
            info.pRegions       = &region;

            flushBarriers();
            m_Handle.copyImageToBuffer2(&info);

            return *this;
        }

        VulkanCommandBuffer&
        VulkanCommandBuffer::update(Buffer& buffer, const uint64_t offset, const uint64_t size, const void* data)
        {
            assert(buffer && data);
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("UpdateBuffer");

            flushBarriers();
            if (size > kMaxDataSize)
            {
                chunkedUpdate(vk::Buffer {asVkHandle<VkBuffer>(buffer.getHandle())},
                              static_cast<vk::DeviceSize>(offset),
                              static_cast<vk::DeviceSize>(size),
                              data);
            }
            else
            {
                m_Handle.updateBuffer(vk::Buffer {asVkHandle<VkBuffer>(buffer.getHandle())},
                                      static_cast<vk::DeviceSize>(offset),
                                      static_cast<vk::DeviceSize>(size),
                                      data);
            }

            m_BarrierBuilder.memoryBarrier(
                {
                    .srcStage  = PipelineStages::eTransfer,
                    .srcAccess = Access::eTransferWrite,
                },
                {
                    .dstStage  = PipelineStages::eAllCommands,
                    .dstAccess = Access::eMemoryRead | Access::eUniformRead | Access::eShaderRead |
                                 Access::eShaderStorageRead | Access::eIndirectCommandRead | Access::eIndexRead |
                                 Access::eVertexAttributeRead | Access::eTransferRead,
                });
            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::blit(Texture&          src,
                                                       Texture&          dst,
                                                       const TexelFilter filter,
                                                       uint32_t          srcMipLevel,
                                                       uint32_t          dstMipLevel,
                                                       uint32_t          srcBaseLayer,
                                                       uint32_t          dstBaseLayer,
                                                       uint32_t          layerCount)
        {
            assert(src && static_cast<bool>(src.getUsageFlags() & ImageUsage::eTransferSrc));
            const auto aspectMask = getAspectMask(dst);
            assert(getAspectMask(src) == aspectMask);
            assert(dst && static_cast<bool>(dst.getUsageFlags() & ImageUsage::eTransferDst));
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("Texture->Texture");

            const uint32_t srcLayers = std::max(src.getNumLayers(), 1u);
            const uint32_t dstLayers = std::max(dst.getNumLayers(), 1u);
            if (layerCount == 0u)
                layerCount = std::min(srcLayers, dstLayers);
            assert(srcBaseLayer + layerCount <= srcLayers && dstBaseLayer + layerCount <= dstLayers);

            getBarrierBuilder()
                .imageBarrier(
                    {
                        .image     = src,
                        .newLayout = ImageLayout::eTransferSrc,
                        .subresourceRange =
                            ImageSubresourceRange {
                                .aspectMask     = aspectMask,
                                .baseMipLevel   = srcMipLevel,
                                .levelCount     = 1u,
                                .baseArrayLayer = src.getBaseArrayLayer() + srcBaseLayer,
                                .layerCount     = layerCount,
                            },
                    },
                    {
                        .dstStage  = PipelineStages::eTransfer,
                        .dstAccess = Access::eTransferRead,
                    })
                .imageBarrier(
                    {
                        .image     = dst,
                        .newLayout = ImageLayout::eTransferDst,
                        .subresourceRange =
                            ImageSubresourceRange {
                                .aspectMask     = aspectMask,
                                .baseMipLevel   = dstMipLevel,
                                .levelCount     = 1u,
                                .baseArrayLayer = dst.getBaseArrayLayer() + dstBaseLayer,
                                .layerCount     = layerCount,
                            },
                    },
                    {
                        .dstStage  = PipelineStages::eTransfer,
                        .dstAccess = Access::eTransferWrite,
                    });

            flushBarriers();

            static const auto GetRegion = [](const Texture& texture, const uint32_t mipLevel) {
                const auto extent = texture.getExtent();
                const auto size   = calcMipSize(glm::uvec3 {extent.width, extent.height, std::max(texture.getDepth(), 1u)},
                                               mipLevel);
                return vk::Offset3D {
                    static_cast<int32_t>(size.x),
                    static_cast<int32_t>(size.y),
                    static_cast<int32_t>(std::max(size.z, 1u)),
                };
            };

            vk::ImageBlit region {};
            region.srcSubresource.aspectMask     = toVk(aspectMask);
            region.srcSubresource.mipLevel       = srcMipLevel;
            region.srcSubresource.baseArrayLayer = src.getBaseArrayLayer() + srcBaseLayer;
            region.srcSubresource.layerCount     = layerCount;
            region.srcOffsets                    = std::array<vk::Offset3D, 2> {vk::Offset3D {}, GetRegion(src, srcMipLevel)};
            region.dstSubresource.aspectMask     = toVk(aspectMask);
            region.dstSubresource.mipLevel       = dstMipLevel;
            region.dstSubresource.baseArrayLayer = dst.getBaseArrayLayer() + dstBaseLayer;
            region.dstSubresource.layerCount     = layerCount;
            region.dstOffsets                    = std::array<vk::Offset3D, 2> {vk::Offset3D {}, GetRegion(dst, dstMipLevel)};

            m_Handle.blitImage(vk::Image {asVkHandle<VkImage>(TextureAccess::getImageHandle(src))},
                               toVk(src.getImageLayout()),
                               vk::Image {asVkHandle<VkImage>(TextureAccess::getImageHandle(dst))},
                               toVk(dst.getImageLayout()),
                               1,
                               &region,
                               toVk(filter));

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::generateMipmaps(Texture& texture, const TexelFilter filter)
        {
            assert(texture);
            assert(invariant(State::eRecording, InvariantFlags::eOutsideRenderPass));

            TRACY_GPU_ZONE2_("GenerateMipmaps");

            m_BarrierBuilder.imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = ImageLayout::eTransferSrc,
                    .subresourceRange =
                        ImageSubresourceRange {
                            .aspectMask     = ImageAspectFlags::eColor,
                            .baseMipLevel   = 0u,
                            .levelCount     = 1u,
                            .baseArrayLayer = 0u,
                            .layerCount     = texture.getLayerFaceCount(),
                        },
                },
                {
                    .dstStage  = PipelineStages::eTransfer,
                    .dstAccess = Access::eTransferRead,
                });
            flushBarriers();

            // Generate the mip chain, blit level n from level n-1.
            for (auto i = 1u; i < texture.getNumMipLevels(); ++i)
            {
                const ImageSubresourceRange mipSubRange {
                    .aspectMask     = ImageAspectFlags::eColor,
                    .baseMipLevel   = i,
                    .levelCount     = 1u,
                    .baseArrayLayer = 0u,
                    .layerCount     = texture.getLayerFaceCount(),
                };

                vk::ImageBlit blitInfo {};
                blitInfo.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                blitInfo.srcSubresource.mipLevel   = i - 1;
                blitInfo.srcSubresource.layerCount = texture.getLayerFaceCount();
                const auto extent                  = texture.getExtent();
                blitInfo.srcOffsets                = std::array {vk::Offset3D {},
                                                  vk::Offset3D {
                                                      std::max(1, static_cast<int32_t>(extent.width) >> (i - 1)),
                                                      std::max(1, static_cast<int32_t>(extent.height) >> (i - 1)),
                                                      std::max(1, static_cast<int32_t>(texture.getDepth()) >> (i - 1)),
                                                  }};
                blitInfo.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                blitInfo.dstSubresource.mipLevel   = i;
                blitInfo.dstSubresource.layerCount = texture.getLayerFaceCount();
                blitInfo.dstOffsets                = std::array {vk::Offset3D {},
                                                  vk::Offset3D {
                                                      std::max(1, static_cast<int32_t>(extent.width) >> i),
                                                      std::max(1, static_cast<int32_t>(extent.height) >> i),
                                                      std::max(1, static_cast<int32_t>(texture.getDepth()) >> i),
                                                  }};

                // Blit from previous level
                m_Handle.blitImage(vk::Image {asVkHandle<VkImage>(TextureAccess::getImageHandle(texture))},
                                   vk::ImageLayout::eTransferSrcOptimal,
                                   vk::Image {asVkHandle<VkImage>(TextureAccess::getImageHandle(texture))},
                                   vk::ImageLayout::eTransferDstOptimal,
                                   1,
                                   &blitInfo,
                                   toVk(filter));

                vk::ImageMemoryBarrier2 mipBarrier {};
                mipBarrier.srcStageMask        = vk::PipelineStageFlagBits2::eTransfer;
                mipBarrier.srcAccessMask       = vk::AccessFlagBits2::eTransferWrite;
                mipBarrier.dstStageMask        = vk::PipelineStageFlagBits2::eTransfer;
                mipBarrier.dstAccessMask       = vk::AccessFlagBits2::eTransferRead;
                mipBarrier.oldLayout           = vk::ImageLayout::eTransferDstOptimal;
                mipBarrier.newLayout           = vk::ImageLayout::eTransferSrcOptimal;
                mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                mipBarrier.image = vk::Image {asVkHandle<VkImage>(TextureAccess::getImageHandle(texture))};
                mipBarrier.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
                mipBarrier.subresourceRange.baseMipLevel   = i;
                mipBarrier.subresourceRange.levelCount     = 1u;
                mipBarrier.subresourceRange.baseArrayLayer = 0u;
                mipBarrier.subresourceRange.layerCount     = texture.getLayerFaceCount();

                vk::DependencyInfo dependencyInfo {};
                dependencyInfo.imageMemoryBarrierCount = 1u;
                dependencyInfo.pImageMemoryBarriers    = &mipBarrier;
                m_Handle.pipelineBarrier2(dependencyInfo);
            }

            // After the loop, all mip layers are in transfer src layout.
            texture.setBarrierState(
                {
                    .dstStage  = PipelineStages::eBlit,
                    .dstAccess = Access::eTransferRead,
                },
                ImageLayout::eTransferSrc);

            return *this;
        }

        VulkanCommandBuffer& VulkanCommandBuffer::flushBarriers()
        {
            if (auto barrier = m_BarrierBuilder.build(); barrier.isEffective())
            {
                TRACY_GPU_ZONE2_("FlushBarriers");
                std::vector<vk::MemoryBarrier2>       memoryBarriers;
                std::vector<vk::BufferMemoryBarrier2> bufferBarriers;
                std::vector<vk::ImageMemoryBarrier2>  imageBarriers;

                const auto& memoryBarrierList = barrier.getMemoryBarriers();
                const auto& bufferBarrierList = barrier.getBufferBarriers();
                const auto& imageBarrierList  = barrier.getImageBarriers();

                memoryBarriers.reserve(memoryBarrierList.size());
                for (const auto& memory : memoryBarrierList)
                {
                    vk::MemoryBarrier2 vkBarrier {};
                    vkBarrier.srcStageMask  = toVk(memory.src.srcStage);
                    vkBarrier.srcAccessMask = toVk(memory.src.srcAccess);
                    vkBarrier.dstStageMask  = toVk(memory.dst.dstStage);
                    vkBarrier.dstAccessMask = toVk(memory.dst.dstAccess);
                    memoryBarriers.emplace_back(vkBarrier);
                }

                bufferBarriers.reserve(bufferBarrierList.size());
                for (const auto& buffer : bufferBarrierList)
                {
                    vk::BufferMemoryBarrier2 vkBarrier {};
                    vkBarrier.srcStageMask        = toVk(buffer.src.srcStage);
                    vkBarrier.srcAccessMask       = toVk(buffer.src.srcAccess);
                    vkBarrier.dstStageMask        = toVk(buffer.dst.dstStage);
                    vkBarrier.dstAccessMask       = toVk(buffer.dst.dstAccess);
                    vkBarrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
                    vkBarrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
                    vkBarrier.buffer              = vk::Buffer {asVkHandle<VkBuffer>(buffer.buffer->getHandle())};
                    vkBarrier.offset              = buffer.offset;
                    vkBarrier.size                = buffer.size;
                    bufferBarriers.emplace_back(vkBarrier);
                }

                imageBarriers.reserve(imageBarrierList.size());
                for (const auto& image : imageBarrierList)
                {
                    vk::ImageMemoryBarrier2 vkBarrier {};
                    vkBarrier.srcStageMask        = toVk(image.src.srcStage);
                    vkBarrier.srcAccessMask       = toVk(image.src.srcAccess);
                    vkBarrier.dstStageMask        = toVk(image.dst.dstStage);
                    vkBarrier.dstAccessMask       = toVk(image.dst.dstAccess);
                    vkBarrier.oldLayout           = toVk(image.oldLayout);
                    vkBarrier.newLayout           = toVk(image.newLayout);
                    vkBarrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
                    vkBarrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
                    vkBarrier.image = vk::Image {asVkHandle<VkImage>(TextureAccess::getImageHandle(*image.image))};
                    vkBarrier.subresourceRange = vk::ImageSubresourceRange {
                        toVk(image.subresourceRange.aspectMask),
                        image.subresourceRange.baseMipLevel,
                        image.subresourceRange.levelCount,
                        image.subresourceRange.baseArrayLayer,
                        image.subresourceRange.layerCount,
                    };
                    imageBarriers.emplace_back(vkBarrier);
                }

                vk::DependencyInfo dependencyInfo {};
                dependencyInfo.memoryBarrierCount       = static_cast<uint32_t>(memoryBarriers.size());
                dependencyInfo.pMemoryBarriers          = memoryBarriers.data();
                dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
                dependencyInfo.pBufferMemoryBarriers    = bufferBarriers.data();
                dependencyInfo.imageMemoryBarrierCount  = static_cast<uint32_t>(imageBarriers.size());
                dependencyInfo.pImageMemoryBarriers     = imageBarriers.data();

                if (m_UseKhrSynchronization2)
                {
                    m_Handle.pipelineBarrier2KHR(&dependencyInfo);
                }
                else
                {
                    m_Handle.pipelineBarrier2(&dependencyInfo);
                }
            }
            return *this;
        }

        VulkanCommandBuffer::VulkanCommandBuffer(const vk::Device        device,
                                                 const vk::CommandPool   commandPool,
                                                 const vk::CommandBuffer handle,
                                                 TracyGpuContext         tracyContext,
                                                 const vk::Fence         fence,
                                                 const RenderDevice*     renderDevice,
                                                 const bool              useKhrDynamicRendering,
                                                 const bool              useKhrSynchronization2,
                                                 const bool              enableDebugMarkers,
                                                 const bool              enableRaytracing) :
            m_Device(device), m_CommandPool(commandPool), m_State(State::eInitial), m_Handle(handle),
            m_TracyContext(tracyContext), m_Fence(fence), m_RenderDevice(renderDevice),
            m_DescriptorSetAllocator(std::make_unique<VulkanDescriptorSetAllocator>(
                                         reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(device))),
                                     enableRaytracing),
            m_UseKhrDynamicRendering(useKhrDynamicRendering), m_UseKhrSynchronization2(useKhrSynchronization2),
            m_EnableDebugMarkers(enableDebugMarkers)
        {}

        bool VulkanCommandBuffer::invariant(const State requiredState, const InvariantFlags flags) const
        {
            if (!m_Handle || m_State != requiredState)
                return false;

            auto valid = true;
            if (static_cast<bool>(flags & InvariantFlags::eValidPipeline))
            {
                valid = valid && (m_Pipeline && *m_Pipeline);
            }
            if (static_cast<bool>(flags & InvariantFlags::eGraphicsPipeline))
            {
                assert(m_Pipeline);
                valid = valid && (m_Pipeline->getBindPoint() == PipelineBindPoint::eGraphics);
            }
            if (static_cast<bool>(flags & InvariantFlags::eComputePipeline))
            {
                assert(m_Pipeline);
                valid = valid && (m_Pipeline->getBindPoint() == PipelineBindPoint::eCompute);
            }
            if (static_cast<bool>(flags & InvariantFlags::eRayTracingPipeline))
            {
                assert(m_Pipeline);
                valid = valid && (m_Pipeline->getBindPoint() == PipelineBindPoint::eRayTracing);
            }
            if (static_cast<bool>(flags & InvariantFlags::eInsideRenderPass))
            {
                valid = valid && m_InsideRenderPass;
            }
            if (static_cast<bool>(flags & InvariantFlags::eOutsideRenderPass))
            {
                valid = valid && !m_InsideRenderPass;
            }
            return valid;
        }

        void VulkanCommandBuffer::destroy() noexcept
        {
            if (!m_Handle)
            {
                return;
            }

            m_Device.waitIdle();

            reset();

            m_Device.destroyFence(m_Fence, nullptr);
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

        void VulkanCommandBuffer::chunkedUpdate(const vk::Buffer bufferHandle,
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

        void VulkanCommandBuffer::setVertexBuffer(const VertexBuffer* vertexBuffer, const vk::DeviceSize offset)
        {
            if (vertexBuffer == m_VertexBuffer)
            {
                return;
            }

            if (vertexBuffer)
            {
                TRACY_GPU_ZONE2_("SetVertexBuffer");
                const auto bufferHandle = vk::Buffer {asVkHandle<VkBuffer>(vertexBuffer->getHandle())};
                m_Handle.bindVertexBuffers(0, 1, &bufferHandle, &offset);
            }
            m_VertexBuffer = vertexBuffer;
        }

        void VulkanCommandBuffer::setIndexBuffer(const IndexBuffer* indexBuffer)
        {
            if (indexBuffer == m_IndexBuffer)
            {
                return;
            }

            if (indexBuffer)
            {
                TRACY_GPU_ZONE2_("SetIndexBuffer");
                const auto indexType = toVk(indexBuffer->getIndexType());
                m_Handle.bindIndexBuffer(vk::Buffer {asVkHandle<VkBuffer>(indexBuffer->getHandle())}, 0, indexType);
            }
            m_IndexBuffer = indexBuffer;
        }

        void VulkanCommandBuffer::pushDebugGroup(const std::string_view label) const
        {
            assert(invariant(State::eRecording));

            if (!m_EnableDebugMarkers)
                return;

            if (!vk::detail::defaultDispatchLoaderDynamic.vkCmdBeginDebugUtilsLabelEXT)
                return;

            vk::DebugUtilsLabelEXT labelInfo = {};
            labelInfo.pLabelName             = label.data();

            m_Handle.beginDebugUtilsLabelEXT(&labelInfo);
        }

        void VulkanCommandBuffer::popDebugGroup() const
        {
            assert(invariant(State::eRecording));

            if (!m_EnableDebugMarkers)
                return;

            if (!vk::detail::defaultDispatchLoaderDynamic.vkCmdEndDebugUtilsLabelEXT)
                return;

            m_Handle.endDebugUtilsLabelEXT();
        }

        void prepareForAttachment(VulkanCommandBuffer& cb, const Texture& texture, const bool readOnly)
        {
            assert(texture);

            BarrierScope dst {};
            ImageLayout  newLayout {ImageLayout::eUndefined};

            const auto aspectMask = getAspectMask(texture);

            if (HasFlagValues(aspectMask, ImageAspectFlags::eColor))
            {
                dst.dstStage  = PipelineStages::eColorAttachmentOutput;
                dst.dstAccess = Access::eColorAttachmentRead | Access::eColorAttachmentWrite;
                newLayout     = ImageLayout::eAttachment;
            }
            else
            {
                dst.dstStage  = PipelineStages::eFragmentTests;
                dst.dstAccess = readOnly ? Access::eDepthStencilAttachmentRead : Access::eDepthStencilAttachmentWrite;
                newLayout     = readOnly ? ImageLayout::eReadOnly : ImageLayout::eAttachment;
            }

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = newLayout,
                    .subresourceRange =
                        ImageSubresourceRange {
                            .aspectMask     = aspectMask,
                            .baseMipLevel   = 0u,
                            .levelCount     = UINT32_MAX,
                            .baseArrayLayer = 0u,
                            .layerCount     = UINT32_MAX,
                        },
                },
                dst);
        }

        void prepareForReading(VulkanCommandBuffer& cb, const Texture& texture, uint32_t mipLevel, uint32_t layer)
        {
            assert(texture);
            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = ImageLayout::eReadOnly,
                    .subresourceRange =
                        ImageSubresourceRange {
                            .aspectMask     = ImageAspectFlags::eNone,
                            .baseMipLevel   = mipLevel,
                            .levelCount     = mipLevel > 0 ? 1u : UINT32_MAX,
                            .baseArrayLayer = layer,
                            .layerCount     = layer > 0 ? 1u : UINT32_MAX,
                        },
                },
                {
                    .dstStage  = PipelineStages::eVertexShader | PipelineStages::eFragmentShader,
                    .dstAccess = Access::eShaderRead,
                });
        }

        void prepareForPresent(VulkanCommandBuffer& cb, const Texture& texture)
        {
            assert(texture);

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = ImageLayout::ePresent,
                },
                {
                    .dstStage  = PipelineStages::eColorAttachmentOutput,
                    .dstAccess = Access::eColorAttachmentWrite,
                });
        }

        void clearImageForComputing(VulkanCommandBuffer& cb, Texture& texture, const ClearValue& clearValue)
        {
            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .dstStage  = rhi::PipelineStages::eTransfer,
                    .dstAccess = rhi::Access::eTransferWrite,
                });
            cb.clear(texture, clearValue);
        }

        void prepareForComputing(VulkanCommandBuffer& cb, const Texture& texture)
        {
            assert(texture);

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .dstStage  = rhi::PipelineStages::eComputeShader,
                    .dstAccess = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                });
        }

        void prepareForRaytracing(VulkanCommandBuffer& cb, const Texture& texture)
        {
            assert(texture);

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .dstStage  = rhi::PipelineStages::eRayTracingShader,
                    .dstAccess = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                });
        }

        void prepareForComputing(VulkanCommandBuffer& cb, const Buffer& buffer)
        {
            assert(buffer);

            cb.getBarrierBuilder().bufferBarrier(
                {
                    .buffer = const_cast<Buffer&>(buffer),
                },
                {
                    .dstStage  = rhi::PipelineStages::eComputeShader,
                    .dstAccess = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                });
        }

        void prepareForDrawingIndirect(VulkanCommandBuffer& cb, const Buffer& buffer)
        {
            assert(buffer);

            cb.getBarrierBuilder().bufferBarrier(
                {
                    .buffer = const_cast<Buffer&>(buffer),
                },
                {
                    .dstStage  = rhi::PipelineStages::eDrawIndirect,
                    .dstAccess = rhi::Access::eIndirectCommandRead,
                });
        }

        void prepareForReading(VulkanCommandBuffer& cb, const Buffer& buffer)
        {
            assert(buffer);

            cb.getBarrierBuilder().bufferBarrier(
                {
                    .buffer = const_cast<Buffer&>(buffer),
                    .offset = 0,
                    .size   = buffer.getSize(),
                },
                {
                    .dstStage  = rhi::PipelineStages::eVertexShader | rhi::PipelineStages::eFragmentShader,
                    .dstAccess = rhi::Access::eShaderRead,
                });
        }
    } // namespace rhi
} // namespace vultra
