#pragma once

#include "vultra/core/rhi/interfaces/icommand_buffer.hpp"
#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/descriptorset_allocator.hpp"
#include "vultra/core/profiling/tracy_wrapper.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"

#include <glm/ext/vector_uint3.hpp>

#include <string_view>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class VulkanCommandBuffer final : public ICommandBuffer
        {
        public:
            VulkanCommandBuffer();
            VulkanCommandBuffer(const vk::Device,
                                const vk::CommandPool,
                                const vk::CommandBuffer,
                                TracyGpuContext,
                                const vk::Fence,
                                const RenderDevice*,
                                bool useKhrDynamicRendering,
                                bool useKhrSynchronization2,
                                bool enableDebugMarkers,
                                bool enableRaytracing = false);
            VulkanCommandBuffer(VulkanCommandBuffer&&) noexcept;
            ~VulkanCommandBuffer() override;

            VulkanCommandBuffer& operator=(VulkanCommandBuffer&&) noexcept;

            [[nodiscard]] std::uintptr_t getHandle() const override;
            [[nodiscard]] TracyGpuContext    getTracyContext() const override;

            [[nodiscard]] Barrier::Builder& getBarrierBuilder() override;
            [[nodiscard]] DescriptorSetBuilder createDescriptorSetBuilder() override;

            VulkanCommandBuffer& begin() override;
            VulkanCommandBuffer& end() override;
            VulkanCommandBuffer& reset() override;
            VulkanCommandBuffer& submit(const JobInfo&, bool oneTime) override;
            [[nodiscard]] bool isComplete() const override;

            VulkanCommandBuffer& bindPipeline(const BasePipeline&) override;

            VulkanCommandBuffer& dispatch(const ComputePipeline&, const glm::uvec3&) override;
            VulkanCommandBuffer& dispatch(const glm::uvec3&) override;
            VulkanCommandBuffer& dispatchIndirect(const Buffer&, uint64_t offset) override;
            VulkanCommandBuffer& insertComputeUavBarrier() override;

            VulkanCommandBuffer& traceRays(const ShaderBindingTable& sbt, const glm::uvec3& extent) override;

            VulkanCommandBuffer& bindDescriptorSet(DescriptorSetIndex, DescriptorSetHandle) override;
            VulkanCommandBuffer& pushConstants(ShaderStages, uint32_t offset, uint32_t size, const void* data) override;

            VulkanCommandBuffer& beginRendering(const FramebufferInfo&) override;
            VulkanCommandBuffer& endRendering() override;

            VulkanCommandBuffer& setViewport(const Rect2D&) override;
            VulkanCommandBuffer& setScissor(const Rect2D&) override;

            VulkanCommandBuffer& draw(const GeometryInfo&, uint32_t numInstances) override;
            VulkanCommandBuffer& drawFullScreenTriangle() override;
            VulkanCommandBuffer& drawCube() override;
            VulkanCommandBuffer& drawIndirect(const DrawIndirectInfo&) override;
            VulkanCommandBuffer& drawIndirectCount(const DrawIndirectInfo&, const Buffer& countBuffer, uint32_t countOffset) override;
            VulkanCommandBuffer& drawMeshTask(const glm::uvec3&) override;

            VulkanCommandBuffer& clear(const Buffer&, uint32_t value) override;
            VulkanCommandBuffer& clear(Texture&, const ClearValue&) override;

            VulkanCommandBuffer& copyBuffer(const Buffer&, Buffer&, const rhi::BufferCopy&) override;
            VulkanCommandBuffer& copyBuffer(const Buffer&, Texture&) override;
            VulkanCommandBuffer& copyBuffer(const Buffer&, Texture&, std::span<const BufferImageCopy>) override;
            VulkanCommandBuffer& copyImage(const Texture&, const Buffer&, const rhi::ImageAspect) override;

            VulkanCommandBuffer& update(Buffer&, uint64_t offset, uint64_t size, const void* data) override;

            VulkanCommandBuffer& blit(Texture&, Texture&, TexelFilter, uint32_t srcMipLevel, uint32_t dstMipLevel) override;
            VulkanCommandBuffer& generateMipmaps(Texture&, TexelFilter) override;

            VulkanCommandBuffer& flushBarriers() override;
            void pushDebugGroup(std::string_view) const override;
            void popDebugGroup() const override;

        private:
            enum class State
            {
                eInvalid = -1,
                eInitial,
                eRecording,
                eExecutable,
                ePending
            };

            enum class InvariantFlags
            {
                eNone          = ZERO_BIT,
                eValidPipeline = BIT(0),

                eGraphicsPipeline      = BIT(1),
                eValidGraphicsPipeline = eValidPipeline | eGraphicsPipeline,

                eComputePipeline      = BIT(2),
                eValidComputePipeline = eValidPipeline | eComputePipeline,

                eInsideRenderPass  = BIT(3),
                eOutsideRenderPass = BIT(4),

                eRayTracingPipeline      = BIT(5),
                eValidRayTracingPipeline = eValidPipeline | eRayTracingPipeline,
            };

            [[nodiscard]] bool invariant(const State requiredState, const InvariantFlags = InvariantFlags::eNone) const;
            void               destroy() noexcept;
            void               chunkedUpdate(const vk::Buffer, vk::DeviceSize offset, vk::DeviceSize size, const void* data) const;
            void               setVertexBuffer(const VertexBuffer*, const vk::DeviceSize offset);
            void               setIndexBuffer(const IndexBuffer*);

        private:
            vk::Device      m_Device {nullptr};
            vk::CommandPool m_CommandPool {nullptr};

            State m_State {State::eInvalid};

            vk::CommandBuffer m_Handle {nullptr};
            TracyGpuContext   m_TracyContext {nullptr};

            vk::Fence m_Fence {nullptr};
            const RenderDevice* m_RenderDevice {nullptr};

            DescriptorSetAllocator m_DescriptorSetAllocator;
            DescriptorSetCache     m_DescriptorSetCache;

            Barrier::Builder m_BarrierBuilder;

            const BasePipeline* m_Pipeline {nullptr};
            const VertexBuffer* m_VertexBuffer {nullptr};
            const IndexBuffer*  m_IndexBuffer {nullptr};

            bool m_UseKhrDynamicRendering {false};
            bool m_UseKhrSynchronization2 {false};
            bool m_EnableDebugMarkers {defaultRenderDiagnosticsEnabled()};
            bool m_InsideRenderPass {false};
        };
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::VulkanCommandBuffer::InvariantFlags> : std::true_type
{};
