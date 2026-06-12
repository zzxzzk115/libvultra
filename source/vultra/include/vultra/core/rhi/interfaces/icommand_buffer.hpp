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

        class ICommandBuffer
        {
        public:
            virtual ~ICommandBuffer() = default;

            [[nodiscard]] virtual std::uintptr_t     getHandle() const = 0;
            [[nodiscard]] virtual TracyGpuContext    getTracyContext() const = 0;
            [[nodiscard]] virtual std::uintptr_t     getCurrentRenderPassEncoderHandle() const { return 0; }
            [[nodiscard]] virtual std::uintptr_t     getCurrentComputePassEncoderHandle() const { return 0; }

            [[nodiscard]] virtual Barrier::Builder& getBarrierBuilder() = 0;
            [[nodiscard]] virtual DescriptorSetBuilder createDescriptorSetBuilder() = 0;

            virtual ICommandBuffer& begin() = 0;
            virtual ICommandBuffer& end() = 0;
            virtual ICommandBuffer& reset() = 0;
            virtual ICommandBuffer& submit(const JobInfo&, bool oneTime) = 0;
            [[nodiscard]] virtual bool isComplete() const = 0;

            virtual ICommandBuffer& bindPipeline(const BasePipeline&) = 0;

            virtual ICommandBuffer& dispatch(const ComputePipeline&, const glm::uvec3&) = 0;
            virtual ICommandBuffer& dispatch(const glm::uvec3&) = 0;
            virtual ICommandBuffer& dispatchIndirect(const Buffer&, uint64_t offset) = 0;
            virtual ICommandBuffer& insertComputeUavBarrier() = 0;

            virtual ICommandBuffer& traceRays(const ShaderBindingTable&, const glm::uvec3&) = 0;

            virtual ICommandBuffer& bindDescriptorSet(DescriptorSetIndex, DescriptorSetHandle) = 0;
            virtual ICommandBuffer& pushConstants(ShaderStages, uint32_t offset, uint32_t size, const void* data) = 0;

            virtual ICommandBuffer& beginRendering(const FramebufferInfo&) = 0;
            virtual ICommandBuffer& endRendering() = 0;

            virtual ICommandBuffer& setViewport(const Rect2D&) = 0;
            virtual ICommandBuffer& setScissor(const Rect2D&) = 0;

            virtual ICommandBuffer& draw(const GeometryInfo&, uint32_t numInstances) = 0;
            virtual ICommandBuffer& drawFullScreenTriangle() = 0;
            virtual ICommandBuffer& drawCube() = 0;
            virtual ICommandBuffer& drawIndirect(const DrawIndirectInfo&) = 0;
            virtual ICommandBuffer& drawIndirectCount(const DrawIndirectInfo&, const Buffer& countBuffer, uint32_t countOffset) = 0;
            virtual ICommandBuffer& drawMeshTask(const glm::uvec3&) = 0;

            virtual ICommandBuffer& clear(const Buffer&, uint32_t value) = 0;
            virtual ICommandBuffer& clear(Texture&, const ClearValue&) = 0;

            virtual ICommandBuffer& copyBuffer(const Buffer&, Buffer&, const rhi::BufferCopy&) = 0;
            virtual ICommandBuffer& copyBuffer(const Buffer&, Texture&) = 0;
            virtual ICommandBuffer& copyBuffer(const Buffer&, Texture&, std::span<const BufferImageCopy>) = 0;
            virtual ICommandBuffer& copyImage(const Texture&, const Buffer&, const rhi::ImageAspect) = 0;

            virtual ICommandBuffer& update(Buffer&, uint64_t offset, uint64_t size, const void* data) = 0;

            // layerCount == 0 blits all layers shared by both textures.
            virtual ICommandBuffer& blit(Texture&,
                                         Texture&,
                                         TexelFilter,
                                         uint32_t srcMipLevel,
                                         uint32_t dstMipLevel,
                                         uint32_t srcBaseLayer,
                                         uint32_t dstBaseLayer,
                                         uint32_t layerCount) = 0;
            virtual ICommandBuffer& generateMipmaps(Texture&, TexelFilter) = 0;

            virtual ICommandBuffer& flushBarriers() = 0;
            virtual void pushDebugGroup(std::string_view) const = 0;
            virtual void popDebugGroup() const                          = 0;
        };
    } // namespace rhi
} // namespace vultra
