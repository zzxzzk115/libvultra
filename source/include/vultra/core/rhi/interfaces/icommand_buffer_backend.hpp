#pragma once

#include "vultra/core/profiling/tracy_wrapper.hpp"
#include "vultra/core/rhi/barrier.hpp"
#include "vultra/core/rhi/base_pipeline.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/structs/draw_indirect_info.hpp"
#include "vultra/core/rhi/structs/framebuffer_info.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/buffer_image_copy.hpp"
#include "vultra/core/rhi/structs/handles.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/shader_binding_table.hpp"
#include "vultra/core/rhi/structs/rect2d.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"
#include "vultra/core/rhi/structs/texel_filter.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <cstdint>
#include <glm/ext/vector_uint3.hpp>
#include <span>
#include <string_view>

namespace vultra
{
    namespace rhi
    {
        struct JobInfo;
        class DescriptorSetBuilder;

        class ICommandBufferBackend
        {
        public:
            virtual ~ICommandBufferBackend() = default;

            [[nodiscard]] virtual std::uintptr_t     getHandle() const = 0;
            [[nodiscard]] virtual TracyGpuContext    getTracyContext() const = 0;

            [[nodiscard]] virtual Barrier::Builder& getBarrierBuilder() = 0;
            [[nodiscard]] virtual DescriptorSetBuilder createDescriptorSetBuilder() = 0;

            virtual ICommandBufferBackend& begin() = 0;
            virtual ICommandBufferBackend& end() = 0;
            virtual ICommandBufferBackend& reset() = 0;
            virtual ICommandBufferBackend& submit(const JobInfo&, bool oneTime) = 0;

            virtual ICommandBufferBackend& bindPipeline(const BasePipeline&) = 0;

            virtual ICommandBufferBackend& dispatch(const ComputePipeline&, const glm::uvec3&) = 0;
            virtual ICommandBufferBackend& dispatch(const glm::uvec3&) = 0;
            virtual ICommandBufferBackend& dispatchIndirect(const Buffer&, uint64_t offset) = 0;
            virtual ICommandBufferBackend& insertComputeUavBarrier() = 0;

            virtual ICommandBufferBackend& traceRays(const ShaderBindingTable&, const glm::uvec3&) = 0;

            virtual ICommandBufferBackend& bindDescriptorSet(DescriptorSetIndex, DescriptorSetHandle) = 0;
            virtual ICommandBufferBackend& pushConstants(ShaderStages, uint32_t offset, uint32_t size, const void* data) = 0;

            virtual ICommandBufferBackend& beginRendering(const FramebufferInfo&) = 0;
            virtual ICommandBufferBackend& endRendering() = 0;

            virtual ICommandBufferBackend& setViewport(const Rect2D&) = 0;
            virtual ICommandBufferBackend& setScissor(const Rect2D&) = 0;

            virtual ICommandBufferBackend& draw(const GeometryInfo&, uint32_t numInstances) = 0;
            virtual ICommandBufferBackend& drawFullScreenTriangle() = 0;
            virtual ICommandBufferBackend& drawCube() = 0;
            virtual ICommandBufferBackend& drawIndirect(const DrawIndirectInfo&) = 0;
            virtual ICommandBufferBackend& drawIndirectCount(const DrawIndirectInfo&, const Buffer& countBuffer, uint32_t countOffset) = 0;
            virtual ICommandBufferBackend& drawMeshTask(const glm::uvec3&) = 0;

            virtual ICommandBufferBackend& clear(const Buffer&, uint32_t value) = 0;
            virtual ICommandBufferBackend& clear(Texture&, const ClearValue&) = 0;

            virtual ICommandBufferBackend& copyBuffer(const Buffer&, Buffer&, const rhi::BufferCopy&) = 0;
            virtual ICommandBufferBackend& copyBuffer(const Buffer&, Texture&) = 0;
            virtual ICommandBufferBackend& copyBuffer(const Buffer&, Texture&, std::span<const BufferImageCopy>) = 0;
            virtual ICommandBufferBackend& copyImage(const Texture&, const Buffer&, const rhi::ImageAspect) = 0;

            virtual ICommandBufferBackend& update(Buffer&, uint64_t offset, uint64_t size, const void* data) = 0;

            virtual ICommandBufferBackend& blit(Texture&, Texture&, TexelFilter, uint32_t srcMipLevel, uint32_t dstMipLevel) = 0;
            virtual ICommandBufferBackend& generateMipmaps(Texture&, TexelFilter) = 0;

            virtual ICommandBufferBackend& flushBarriers() = 0;
            virtual void pushDebugGroup(std::string_view) const = 0;
            virtual void popDebugGroup() const                          = 0;
        };
    } // namespace rhi
} // namespace vultra
