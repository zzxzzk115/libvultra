#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/scoped_enum_flags.hpp"
#include "vultra/core/rhi/barrier.hpp"
#include "vultra/core/rhi/debug_marker.hpp"
#include "vultra/core/rhi/descriptorset_allocator.hpp"
#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/framebuffer_info.hpp"
#include "vultra/core/rhi/geometry_info.hpp"
#include "vultra/core/rhi/image_aspect.hpp"
#include "vultra/core/rhi/rect2d.hpp"
#include "vultra/core/rhi/shader_type.hpp"
#include "vultra/core/rhi/texel_filter.hpp"

#include <glm/ext/vector_uint3.hpp>

#include "vultra/core/profiling/tracky.hpp"
#include "vultra/core/profiling/tracy_wrapper.hpp"

namespace vultra
{
    namespace imgui
    {
        class ImGuiRenderer;
    }

    namespace rhi
    {
        class RenderDevice;
        class VertexBuffer;
        class IndexBuffer;
        class Texture;
        class BasePipeline;
        class ComputePipeline;
        class ShaderBindingTable;

        class CommandBuffer final
        {
            friend class RenderDevice;
            friend class DebugMarker;
            friend class imgui::ImGuiRenderer;

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

        public:
            CommandBuffer();
            CommandBuffer(const CommandBuffer&) = delete;
            CommandBuffer(CommandBuffer&&) noexcept;
            ~CommandBuffer();

            CommandBuffer& operator=(const CommandBuffer&) = delete;
            CommandBuffer& operator=(CommandBuffer&&) noexcept;

            [[nodiscard]] vk::CommandBuffer getHandle() const;
            [[nodiscard]] TracyVkCtx        getTracyContext() const;

            Barrier::Builder&                  getBarrierBuilder();
            [[nodiscard]] DescriptorSetBuilder createDescriptorSetBuilder();

            CommandBuffer& begin();
            CommandBuffer& end();
            CommandBuffer& reset();

            // ---

            CommandBuffer& bindPipeline(const BasePipeline&);

            CommandBuffer& dispatch(const ComputePipeline&, const glm::uvec3&);
            CommandBuffer& dispatch(const glm::uvec3&);

            CommandBuffer& traceRays(const ShaderBindingTable& sbt, const glm::uvec3& extent);

            CommandBuffer& bindDescriptorSet(const DescriptorSetIndex, const vk::DescriptorSet);

            CommandBuffer&
            pushConstants(const ShaderStages, const uint32_t offset, const uint32_t size, const void* data);

            template<typename T>
            CommandBuffer& pushConstants(const ShaderStages shaderStages, const uint32_t offset, const T* v)
            {
                return pushConstants(shaderStages, offset, sizeof(T), v);
            }

            // ---

            // Does not insert barriers for attachments.
            CommandBuffer& beginRendering(const FramebufferInfo&);
            CommandBuffer& endRendering();

            CommandBuffer& setViewport(const Rect2D&);
            CommandBuffer& setScissor(const Rect2D&);

            CommandBuffer& draw(const GeometryInfo&, const uint32_t numInstances = 1);
            CommandBuffer& drawFullScreenTriangle();
            CommandBuffer& drawCube();

            // ---

            CommandBuffer& clear(const Buffer&, const uint32_t value = 0);
            // Texture image must be created with TRANSFER_DST.
            CommandBuffer& clear(Texture&, const ClearValue&);

            CommandBuffer& copyBuffer(const Buffer& src, Buffer& dst, const vk::BufferCopy&);
            CommandBuffer& copyBuffer(const Buffer& src, Texture& dst);
            // Inserts layout transition barrier for dst.
            CommandBuffer& copyBuffer(const Buffer& src, Texture& dst, std::span<const vk::BufferImageCopy>);
            CommandBuffer& copyImage(const Texture& src, const Buffer& dst, const rhi::ImageAspect);

            CommandBuffer& update(Buffer&, const vk::DeviceSize offset, const vk::DeviceSize size, const void* data);

            CommandBuffer&
            blit(Texture& src, Texture& dst, const vk::Filter, uint32_t srcMipLevel = 0, uint32_t dstMipLevel = 0);

            CommandBuffer& generateMipmaps(Texture&, const TexelFilter = TexelFilter::eLinear);

            // ---
            CommandBuffer& flushBarriers();

        private:
            CommandBuffer(const vk::Device,
                          const vk::CommandPool,
                          const vk::CommandBuffer,
                          TracyVkCtx,
                          const vk::Fence,
                          const bool enableRaytracing = false);

            [[nodiscard]] bool invariant(const State requiredState, const InvariantFlags = InvariantFlags::eNone) const;

            void destroy() noexcept;

            void chunkedUpdate(const vk::Buffer, vk::DeviceSize offset, vk::DeviceSize size, const void* data) const;

            void setVertexBuffer(const VertexBuffer*, const vk::DeviceSize offset);
            void setIndexBuffer(const IndexBuffer*);

            void pushDebugGroup(const std::string_view) const;
            void popDebugGroup() const;

        private:
            vk::Device      m_Device {VK_NULL_HANDLE};
            vk::CommandPool m_CommandPool {VK_NULL_HANDLE};

            State m_State {State::eInvalid};

            vk::CommandBuffer m_Handle {VK_NULL_HANDLE};
            TracyVkCtx        m_TracyContext {nullptr};

            vk::Fence m_Fence {VK_NULL_HANDLE};

            DescriptorSetAllocator m_DescriptorSetAllocator;
            DescriptorSetCache     m_DescriptorSetCache;

            Barrier::Builder m_BarrierBuilder;

            const BasePipeline* m_Pipeline {nullptr};
            const VertexBuffer* m_VertexBuffer {nullptr};
            const IndexBuffer*  m_IndexBuffer {nullptr};

            bool m_InsideRenderPass {false};
        };

        void prepareForAttachment(CommandBuffer&, const Texture&, const bool readOnly);
        void prepareForReading(CommandBuffer&, const Texture&, uint32_t mipLevel = 0, uint32_t layer = 0);
        void
        clearImageForComputing(CommandBuffer&, Texture&, const ClearValue& clearValue = ClearValue {glm::vec4(0.0f)});
        void prepareForComputing(CommandBuffer& cb, const Texture& texture);
        void prepareForRaytracing(CommandBuffer& cb, const Texture& texture);
        void prepareForComputing(CommandBuffer& cb, const Buffer& buffer);
        void prepareForReading(CommandBuffer& cb, const Buffer& buffer);
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::CommandBuffer::InvariantFlags> : std::true_type
{};

#define TRACY_GPU_ZONE_(TracyContext, CommandBufferHandle, Label) \
    ZoneScopedN(Label); \
    TracyVkZone(TracyContext, CommandBufferHandle, Label)

#define TRACY_GPU_ZONE(CommandBuffer, Label) \
    TRACY_GPU_ZONE_(CommandBuffer.getTracyContext(), CommandBuffer.getHandle(), Label)

#define TRACY_GPU_TRANSIENT_ZONE(CommandBuffer, Label) \
    ZoneTransientN(_tracy_zone, Label, true); \
    TracyVkZoneTransient(CommandBuffer.getTracyContext(), _tracy_vk_zone, CommandBuffer.getHandle(), Label, true)

#define TRACKY_VK_NEXT_FRAME(CommandBuffer) \
    TRACKY_BIND_CMD_BUFFER(CommandBuffer.getHandle()); \
    TRACKY_NEXT_FRAME();

#define TRACKY_VK_SCOPE(CommandBuffer, Label, ...) \
    TRACKY_BIND_CMD_BUFFER(CommandBuffer.getHandle()); \
    TRACKY_SCOPE(Label, __VA_ARGS__);

#define TRACKY_GPU_ZONE(CommandBuffer, Label) TRACKY_VK_SCOPE(CommandBuffer, Label, GPU)

#define RHI_GPU_ZONE(CommandBuffer, Label) \
    RHI_NAMED_DEBUG_MARKER(CommandBuffer, Label); \
    TRACY_GPU_TRANSIENT_ZONE(CommandBuffer, Label) \
    TRACKY_GPU_ZONE(CommandBuffer, Label)

#define FG_GPU_ZONE(CommandBuffer) \
    TRACY_GPU_ZONE(CommandBuffer, "FrameGraph::Execute"); \
    TRACKY_GPU_ZONE(CommandBuffer, "FrameGraph::Execute");