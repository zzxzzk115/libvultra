#pragma once

#include "vultra/core/rhi/backends/webgpu/webgpu_render_device.hpp"
#include "vultra/core/rhi/interfaces/icommand_buffer.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class WebGPUDescriptorSet;

        class WebGPUCommandBuffer final : public ICommandBuffer
        {
        public:
            explicit WebGPUCommandBuffer(const WebGPURenderDevice&);
            ~WebGPUCommandBuffer() override;

            [[nodiscard]] std::uintptr_t getHandle() const override;
            [[nodiscard]] TracyGpuContext getTracyContext() const override;
            [[nodiscard]] std::uintptr_t getCurrentRenderPassEncoderHandle() const override;
            [[nodiscard]] std::uintptr_t getCurrentComputePassEncoderHandle() const override;

            [[nodiscard]] Barrier::Builder& getBarrierBuilder() override;
            [[nodiscard]] DescriptorSetBuilder createDescriptorSetBuilder() override;

            WebGPUCommandBuffer& begin() override;
            WebGPUCommandBuffer& end() override;
            WebGPUCommandBuffer& reset() override;
            WebGPUCommandBuffer& submit(const JobInfo&, bool oneTime) override;

            WebGPUCommandBuffer& bindPipeline(const BasePipeline&) override;

            WebGPUCommandBuffer& dispatch(const ComputePipeline&, const glm::uvec3&) override;
            WebGPUCommandBuffer& dispatch(const glm::uvec3&) override;
            WebGPUCommandBuffer& dispatchIndirect(const Buffer&, uint64_t offset) override;
            WebGPUCommandBuffer& insertComputeUavBarrier() override;

            WebGPUCommandBuffer& traceRays(const ShaderBindingTable& sbt, const glm::uvec3& extent) override;

            WebGPUCommandBuffer& bindDescriptorSet(DescriptorSetIndex, DescriptorSetHandle) override;
            WebGPUCommandBuffer& pushConstants(ShaderStages, uint32_t offset, uint32_t size, const void* data) override;

            WebGPUCommandBuffer& beginRendering(const FramebufferInfo&) override;
            WebGPUCommandBuffer& endRendering() override;

            WebGPUCommandBuffer& setViewport(const Rect2D&) override;
            WebGPUCommandBuffer& setScissor(const Rect2D&) override;

            WebGPUCommandBuffer& draw(const GeometryInfo&, uint32_t numInstances) override;
            WebGPUCommandBuffer& drawFullScreenTriangle() override;
            WebGPUCommandBuffer& drawCube() override;
            WebGPUCommandBuffer& drawIndirect(const DrawIndirectInfo&) override;
            WebGPUCommandBuffer& drawIndirectCount(const DrawIndirectInfo&, const Buffer& countBuffer, uint32_t countOffset) override;
            WebGPUCommandBuffer& drawMeshTask(const glm::uvec3&) override;

            WebGPUCommandBuffer& clear(const Buffer&, uint32_t value) override;
            WebGPUCommandBuffer& clear(Texture&, const ClearValue&) override;

            WebGPUCommandBuffer& copyBuffer(const Buffer&, Buffer&, const rhi::BufferCopy&) override;
            WebGPUCommandBuffer& copyBuffer(const Buffer&, Texture&) override;
            WebGPUCommandBuffer& copyBuffer(const Buffer&, Texture&, std::span<const BufferImageCopy>) override;
            WebGPUCommandBuffer& copyImage(const Texture&, const Buffer&, const rhi::ImageAspect) override;

            WebGPUCommandBuffer& update(Buffer&, uint64_t offset, uint64_t size, const void* data) override;

            WebGPUCommandBuffer& blit(Texture&, Texture&, TexelFilter, uint32_t srcMipLevel, uint32_t dstMipLevel) override;
            WebGPUCommandBuffer& generateMipmaps(Texture&, TexelFilter) override;

            WebGPUCommandBuffer& flushBarriers() override;
            void pushDebugGroup(std::string_view) const override;
            void popDebugGroup() const override;

        private:
            [[noreturn]] static void unsupported(const char* name);
            void releaseTransientResources() noexcept;

        public:
            [[nodiscard]] WGPURenderPassEncoder getCurrentRenderPassEncoder() const { return m_RenderPass; }
            [[nodiscard]] WGPUComputePassEncoder getCurrentComputePassEncoder() const { return m_ComputePass; }
            void closeActiveComputePassForProfilingBoundary();

        private:
            WGPUInstance m_Instance {nullptr};
            WGPUDevice   m_Device {nullptr};
            WGPUQueue    m_Queue {nullptr};

            WGPUCommandEncoder   m_Encoder {nullptr};
            WGPUComputePassEncoder m_ComputePass {nullptr};
            WGPURenderPassEncoder m_RenderPass {nullptr};
            WGPUTextureView      m_RenderView {nullptr};
            WGPUTextureView      m_DepthView {nullptr};
            std::vector<WGPUBuffer> m_TransientUploadBuffers;
            std::vector<std::unique_ptr<WebGPUDescriptorSet>> m_DescriptorSets;

            WGPURenderPipeline m_BoundPipeline {nullptr};
            WGPUComputePipeline m_BoundComputePipeline {nullptr};
            const BasePipeline* m_BoundPipelineObject {nullptr};
            WebGPURenderDevice* m_Backend {nullptr};
            std::unordered_map<std::size_t, WGPUBindGroup> m_EmptyBindGroups;
            std::array<WGPUBindGroup, kMinNumDescriptorSets> m_PendingComputeBindGroups {};
            std::unordered_map<std::size_t, WGPUBindGroup> m_PushConstantBindGroups;
            WGPUBuffer m_PushConstantBuffer {nullptr};
            uint64_t   m_PushConstantBufferSize {0};
            bool           m_Recording {false};
            bool           m_InsideRendering {false};
            bool           m_SkipCurrentRendering {false};
            bool           m_OwnsRenderView {false};
            bool           m_OwnsDepthView {false};
            bool           m_PipelineBoundInCurrentPass {false};

            Barrier::Builder m_BarrierBuilder;
        };
    } // namespace rhi
} // namespace vultra
