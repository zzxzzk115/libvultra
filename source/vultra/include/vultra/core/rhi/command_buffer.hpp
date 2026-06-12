#pragma once

#include "vultra/core/profiling/tracky.hpp"
#include "vultra/core/profiling/tracy_wrapper.hpp"
#include "vultra/core/rhi/debug_marker.hpp"
#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/interfaces/icommand_buffer.hpp"
#if defined(TRACY_ENABLE) && defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#endif
#include "vultra/core/rhi/structs/buffer_image_copy.hpp"
#include "vultra/core/rhi/structs/handles.hpp"

#include <vbase/core/scoped_enum_flags.hpp>

#include <glm/ext/vector_uint3.hpp>

#include <atomic>
#include <functional>
#include <memory>

namespace vultra
{
    class ImGuiSystem;

    namespace rhi
    {
        struct BuiltinProfilerGpuScopeContext
        {
            std::uintptr_t commandBufferHandle {0};
            std::uintptr_t renderPassEncoderHandle {0};
            std::uintptr_t computePassEncoderHandle {0};
        };

        struct BuiltinProfilerGpuScopeCallbacks
        {
            std::function<void(const BuiltinProfilerGpuScopeContext&)> bind;
            std::function<void(const BuiltinProfilerGpuScopeContext&, const char*)> begin;
            std::function<void(const BuiltinProfilerGpuScopeContext&)>               end;
        };

        BuiltinProfilerGpuScopeCallbacks& builtinProfilerGpuScopeCallbacks();

        void setBuiltinProfilerGpuScopeCallbacks(
            std::function<void(const BuiltinProfilerGpuScopeContext&)> bind,
            std::function<void(const BuiltinProfilerGpuScopeContext&, const char*)> begin,
            std::function<void(const BuiltinProfilerGpuScopeContext&)>               end);

        inline void setBuiltinProfilerGpuScopeCallbacks(
            std::function<void(const BuiltinProfilerGpuScopeContext&, const char*)> begin,
            std::function<void(const BuiltinProfilerGpuScopeContext&)>               end)
        {
            setBuiltinProfilerGpuScopeCallbacks({}, std::move(begin), std::move(end));
        }

        class BuiltinProfilerGpuScope final
        {
        public:
            BuiltinProfilerGpuScope(const BuiltinProfilerGpuScopeContext& context, const char* label) : m_Context(context)
            {
                auto& callbacks = builtinProfilerGpuScopeCallbacks();
                if (callbacks.bind)
                    callbacks.bind(m_Context);
                if (callbacks.begin)
                {
                    callbacks.begin(m_Context, label);
                    m_Active = true;
                }
            }

            ~BuiltinProfilerGpuScope()
            {
                if (!m_Active)
                    return;

                auto& callbacks = builtinProfilerGpuScopeCallbacks();
                if (callbacks.end)
                    callbacks.end(m_Context);
            }

            BuiltinProfilerGpuScope(const BuiltinProfilerGpuScope&)            = delete;
            BuiltinProfilerGpuScope& operator=(const BuiltinProfilerGpuScope&) = delete;

        private:
            BuiltinProfilerGpuScopeContext m_Context {};
            bool m_Active {false};
        };

        class WebGPUCommandBufferAccess;

        class RenderDevice;
        class Buffer;
        class VertexBuffer;
        class IndexBuffer;
        class Texture;
        class BasePipeline;
        class ComputePipeline;
        class ShaderBindingTable;
        class DebugMarker;
        struct JobInfo;

        class CommandBuffer final
        {
            friend class RenderDevice;
            friend class DebugMarker;
            friend class vultra::ImGuiSystem;
            friend class WebGPUCommandBufferAccess;

        public:
            struct FrameStats
            {
                uint64_t drawCalls {0};
                uint64_t dispatchCalls {0};
                uint64_t traceRaysCalls {0};
                uint64_t copyOps {0};
                uint64_t updateOps {0};
            };

            static void resetFrameStats()
            {
                s_DrawCalls.store(0, std::memory_order_relaxed);
                s_DispatchCalls.store(0, std::memory_order_relaxed);
                s_TraceRaysCalls.store(0, std::memory_order_relaxed);
                s_CopyOps.store(0, std::memory_order_relaxed);
                s_UpdateOps.store(0, std::memory_order_relaxed);
            }

            [[nodiscard]] static FrameStats consumeFrameStats()
            {
                FrameStats out {};
                out.drawCalls      = s_DrawCalls.exchange(0, std::memory_order_relaxed);
                out.dispatchCalls  = s_DispatchCalls.exchange(0, std::memory_order_relaxed);
                out.traceRaysCalls = s_TraceRaysCalls.exchange(0, std::memory_order_relaxed);
                out.copyOps        = s_CopyOps.exchange(0, std::memory_order_relaxed);
                out.updateOps      = s_UpdateOps.exchange(0, std::memory_order_relaxed);
                return out;
            }

            CommandBuffer()                         = default;
            CommandBuffer(const CommandBuffer&)     = delete;
            CommandBuffer(CommandBuffer&&) noexcept = default;
            ~CommandBuffer()                        = default;

            CommandBuffer& operator=(const CommandBuffer&)     = delete;
            CommandBuffer& operator=(CommandBuffer&&) noexcept = default;

            [[nodiscard]] std::uintptr_t getHandle() const
            {
                assert(m_Impl);
                return m_Impl->getHandle();
            }
            [[nodiscard]] std::uintptr_t getCurrentRenderPassEncoderHandle() const
            {
                assert(m_Impl);
                return m_Impl->getCurrentRenderPassEncoderHandle();
            }
            [[nodiscard]] std::uintptr_t getCurrentComputePassEncoderHandle() const
            {
                assert(m_Impl);
                return m_Impl->getCurrentComputePassEncoderHandle();
            }
            [[nodiscard]] TracyGpuContext getTracyContext() const
            {
                assert(m_Impl);
                return m_Impl->getTracyContext();
            }

            [[nodiscard]] Barrier::Builder& getBarrierBuilder()
            {
                assert(m_Impl);
                return m_Impl->getBarrierBuilder();
            }
            [[nodiscard]] DescriptorSetBuilder createDescriptorSetBuilder()
            {
                assert(m_Impl);
                return m_Impl->createDescriptorSetBuilder();
            }

            CommandBuffer& begin()
            {
                assert(m_Impl);
                m_Impl->begin();
                return *this;
            }
            CommandBuffer& end()
            {
                assert(m_Impl);
                m_Impl->end();
                return *this;
            }
            CommandBuffer& reset()
            {
                assert(m_Impl);
                m_Impl->reset();
                return *this;
            }
            CommandBuffer& submit(const JobInfo& jobInfo, bool oneTime = false)
            {
                assert(m_Impl);
                m_Impl->submit(jobInfo, oneTime);
                return *this;
            }
            [[nodiscard]] bool isComplete() const { return !m_Impl || m_Impl->isComplete(); }
            [[nodiscard]] explicit operator bool() const { return static_cast<bool>(m_Impl); }

            // ---

            CommandBuffer& bindPipeline(const BasePipeline& pipeline)
            {
                assert(m_Impl);
                m_Impl->bindPipeline(pipeline);
                return *this;
            }

            CommandBuffer& dispatch(const ComputePipeline& pipeline, const glm::uvec3& groupCount)
            {
                assert(m_Impl);
                s_DispatchCalls.fetch_add(1, std::memory_order_relaxed);
                m_Impl->dispatch(pipeline, groupCount);
                return *this;
            }
            CommandBuffer& dispatch(const glm::uvec3& groupCount)
            {
                assert(m_Impl);
                s_DispatchCalls.fetch_add(1, std::memory_order_relaxed);
                m_Impl->dispatch(groupCount);
                return *this;
            }
            CommandBuffer& dispatchIndirect(const Buffer& buffer, uint64_t offset = 0)
            {
                assert(m_Impl);
                m_Impl->dispatchIndirect(buffer, offset);
                return *this;
            }
            CommandBuffer& insertComputeUavBarrier()
            {
                assert(m_Impl);
                m_Impl->insertComputeUavBarrier();
                return *this;
            }

            CommandBuffer& traceRays(const ShaderBindingTable& sbt, const glm::uvec3& extent)
            {
                assert(m_Impl);
                s_TraceRaysCalls.fetch_add(1, std::memory_order_relaxed);
                m_Impl->traceRays(sbt, extent);
                return *this;
            }

            CommandBuffer& bindDescriptorSet(const DescriptorSetIndex index, const DescriptorSetHandle descriptorSet)
            {
                assert(m_Impl);
                m_Impl->bindDescriptorSet(index, descriptorSet);
                return *this;
            }

            CommandBuffer&
            pushConstants(const ShaderStages shaderStages, const uint32_t offset, const uint32_t size, const void* data)
            {
                assert(m_Impl);
                m_Impl->pushConstants(shaderStages, offset, size, data);
                return *this;
            }

            template<typename T>
            CommandBuffer& pushConstants(const ShaderStages shaderStages, const uint32_t offset, const T* v)
            {
                return pushConstants(shaderStages, offset, sizeof(T), v);
            }

            // ---

            // Does not insert barriers for attachments.
            CommandBuffer& beginRendering(const FramebufferInfo& framebufferInfo)
            {
                assert(m_Impl);
                m_Impl->beginRendering(framebufferInfo);
                return *this;
            }
            CommandBuffer& endRendering()
            {
                assert(m_Impl);
                m_Impl->endRendering();
                return *this;
            }

            CommandBuffer& setViewport(const Rect2D& rect)
            {
                assert(m_Impl);
                m_Impl->setViewport(rect);
                return *this;
            }
            CommandBuffer& setScissor(const Rect2D& rect)
            {
                assert(m_Impl);
                m_Impl->setScissor(rect);
                return *this;
            }

            CommandBuffer& draw(const GeometryInfo& gi, const uint32_t numInstances = 1)
            {
                assert(m_Impl);
                s_DrawCalls.fetch_add(1, std::memory_order_relaxed);
                m_Impl->draw(gi, numInstances);
                return *this;
            }
            CommandBuffer& drawFullScreenTriangle()
            {
                assert(m_Impl);
                s_DrawCalls.fetch_add(1, std::memory_order_relaxed);
                m_Impl->drawFullScreenTriangle();
                return *this;
            }
            CommandBuffer& drawCube()
            {
                assert(m_Impl);
                s_DrawCalls.fetch_add(1, std::memory_order_relaxed);
                m_Impl->drawCube();
                return *this;
            }
            CommandBuffer& drawIndirect(const DrawIndirectInfo& dii)
            {
                assert(m_Impl);
                s_DrawCalls.fetch_add(1, std::memory_order_relaxed);
                m_Impl->drawIndirect(dii);
                return *this;
            }
            CommandBuffer&
            drawIndirectCount(const DrawIndirectInfo& dii, const Buffer& countBuffer, uint32_t countOffset)
            {
                assert(m_Impl);
                s_DrawCalls.fetch_add(1, std::memory_order_relaxed);
                m_Impl->drawIndirectCount(dii, countBuffer, countOffset);
                return *this;
            }
            CommandBuffer& drawMeshTask(const glm::uvec3& numTaskGroups)
            {
                assert(m_Impl);
                s_DrawCalls.fetch_add(1, std::memory_order_relaxed);
                m_Impl->drawMeshTask(numTaskGroups);
                return *this;
            }

            // ---

            CommandBuffer& clear(const Buffer& buffer, const uint32_t value = 0)
            {
                assert(m_Impl);
                m_Impl->clear(buffer, value);
                return *this;
            }
            // Texture image must be created with TRANSFER_DST.
            CommandBuffer& clear(Texture& texture, const ClearValue& clearValue)
            {
                assert(m_Impl);
                m_Impl->clear(texture, clearValue);
                return *this;
            }

            CommandBuffer& copyBuffer(const Buffer& src, Buffer& dst, const rhi::BufferCopy& copyRegion)
            {
                assert(m_Impl);
                s_CopyOps.fetch_add(1, std::memory_order_relaxed);
                m_Impl->copyBuffer(src, dst, copyRegion);
                return *this;
            }
            CommandBuffer& copyBuffer(const Buffer& src, Texture& dst)
            {
                assert(m_Impl);
                s_CopyOps.fetch_add(1, std::memory_order_relaxed);
                m_Impl->copyBuffer(src, dst);
                return *this;
            }
            // Inserts layout transition barrier for dst.
            CommandBuffer& copyBuffer(const Buffer& src, Texture& dst, std::span<const BufferImageCopy> copyRegions)
            {
                assert(m_Impl);
                s_CopyOps.fetch_add(1, std::memory_order_relaxed);
                m_Impl->copyBuffer(src, dst, copyRegions);
                return *this;
            }
            CommandBuffer& copyImage(const Texture& src, const Buffer& dst, const rhi::ImageAspect aspectMask)
            {
                assert(m_Impl);
                s_CopyOps.fetch_add(1, std::memory_order_relaxed);
                m_Impl->copyImage(src, dst, aspectMask);
                return *this;
            }

            CommandBuffer& update(Buffer& buffer, const uint64_t offset, const uint64_t size, const void* data)
            {
                assert(m_Impl);
                s_UpdateOps.fetch_add(1, std::memory_order_relaxed);
                m_Impl->update(buffer, offset, size, data);
                return *this;
            }

            // layerCount == 0 blits all layers shared by both textures.
            CommandBuffer& blit(Texture&          src,
                                Texture&          dst,
                                const TexelFilter filter,
                                uint32_t          srcMipLevel  = 0,
                                uint32_t          dstMipLevel  = 0,
                                uint32_t          srcBaseLayer = 0,
                                uint32_t          dstBaseLayer = 0,
                                uint32_t          layerCount   = 0)
            {
                assert(m_Impl);
                m_Impl->blit(src, dst, filter, srcMipLevel, dstMipLevel, srcBaseLayer, dstBaseLayer, layerCount);
                return *this;
            }

            CommandBuffer& generateMipmaps(Texture& texture, const TexelFilter filter = TexelFilter::eLinear)
            {
                assert(m_Impl);
                m_Impl->generateMipmaps(texture, filter);
                return *this;
            }

            // ---
            CommandBuffer& flushBarriers()
            {
                assert(m_Impl);
                m_Impl->flushBarriers();
                return *this;
            }

        private:
            explicit CommandBuffer(std::unique_ptr<ICommandBuffer> impl) : m_Impl(std::move(impl)) {}

        private:
            inline static std::atomic<uint64_t> s_DrawCalls {0};
            inline static std::atomic<uint64_t> s_DispatchCalls {0};
            inline static std::atomic<uint64_t> s_TraceRaysCalls {0};
            inline static std::atomic<uint64_t> s_CopyOps {0};
            inline static std::atomic<uint64_t> s_UpdateOps {0};

            void pushDebugGroup(const std::string_view label) const
            {
                assert(m_Impl);
                m_Impl->pushDebugGroup(label);
            }
            void popDebugGroup() const
            {
                assert(m_Impl);
                m_Impl->popDebugGroup();
            }

        private:
            std::unique_ptr<ICommandBuffer> m_Impl;
        };

        void prepareForAttachment(CommandBuffer&, const Texture&, const bool readOnly);
        void prepareForReading(CommandBuffer&, const Texture&, uint32_t mipLevel = 0, uint32_t layer = 0);
        void prepareForPresent(CommandBuffer&, const Texture&);
        void
        clearImageForComputing(CommandBuffer&, Texture&, const ClearValue& clearValue = ClearValue {glm::vec4(0.0f)});
        void prepareForComputing(CommandBuffer& cb, const Texture& texture);
        void prepareForRaytracing(CommandBuffer& cb, const Texture& texture);
        void prepareForComputing(CommandBuffer& cb, const Buffer& buffer);
        void prepareForDrawingIndirect(CommandBuffer& cb, const Buffer& buffer);
        void prepareForReading(CommandBuffer& cb, const Buffer& buffer);
    } // namespace rhi
} // namespace vultra

#define TRACY_GPU_ZONE_(TracyContext, CommandBufferHandle, Label) \
    ZoneScopedN(Label); \
    TracyGpuZone(TracyContext, CommandBufferHandle, Label)

#if defined(TRACY_ENABLE) && defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
#define TRACY_GPU_COMMAND_BUFFER_HANDLE(CommandBuffer) ::vultra::rhi::asVkHandle<VkCommandBuffer>((CommandBuffer).getHandle())
#else
#define TRACY_GPU_COMMAND_BUFFER_HANDLE(CommandBuffer) (CommandBuffer).getHandle()
#endif

#define TRACY_GPU_ZONE(CommandBuffer, Label) \
    TRACY_GPU_ZONE_(CommandBuffer.getTracyContext(), TRACY_GPU_COMMAND_BUFFER_HANDLE(CommandBuffer), Label)

#define TRACY_GPU_TRANSIENT_ZONE(CommandBuffer, Label) \
    ZoneTransientN(_tracy_zone, Label, true); \
    TracyGpuZoneTransient(CommandBuffer.getTracyContext(), \
                          _tracy_vk_zone, \
                          TRACY_GPU_COMMAND_BUFFER_HANDLE(CommandBuffer), \
                          Label, \
                          true)

#ifndef TRACKY_BIND_CMD_BUFFER
#define TRACKY_BIND_CMD_BUFFER(cmdBuf, renderPass, computePass) \
    do \
    { \
    } while (0)
#endif

#ifndef RHI_TRACKY_JOIN_
#define RHI_TRACKY_JOIN_(a, b) RHI_TRACKY_JOIN_INNER_(a, b)
#define RHI_TRACKY_JOIN_INNER_(a, b) a##b
#endif

#define RHI_BUILTIN_PROFILER_GPU_ZONE(CommandBuffer, Label) \
    ::vultra::rhi::BuiltinProfilerGpuScope RHI_TRACKY_JOIN_(builtinProfilerGpuScopeVar_, __LINE__)( \
        ::vultra::rhi::BuiltinProfilerGpuScopeContext { \
            .commandBufferHandle     = CommandBuffer.getHandle(), \
            .renderPassEncoderHandle = CommandBuffer.getCurrentRenderPassEncoderHandle(), \
            .computePassEncoderHandle = CommandBuffer.getCurrentComputePassEncoderHandle(), \
        }, \
        Label)

#define TRACKY_GPU_NEXT_FRAME(CommandBuffer) \
    TRACKY_BIND_CMD_BUFFER(CommandBuffer.getHandle(), CommandBuffer.getCurrentRenderPassEncoderHandle(), \
                           CommandBuffer.getCurrentComputePassEncoderHandle()); \
    TRACKY_NEXT_FRAME();

#define TRACKY_GPU_SCOPE(CommandBuffer, Label, ...) \
    TRACKY_BIND_CMD_BUFFER(CommandBuffer.getHandle(), CommandBuffer.getCurrentRenderPassEncoderHandle(), \
                           CommandBuffer.getCurrentComputePassEncoderHandle()); \
    TRACKY_SCOPE(Label, __VA_ARGS__);

#if defined(TRACKY_ENABLE) && TRACKY_ENABLE
#define TRACKY_GPU_ZONE(CmdBuf, Label) \
    struct RHI_TRACKY_JOIN_(TrackyBoundScopeType_, __LINE__) \
    { \
        ::vultra::rhi::CommandBuffer& cb; \
        const char* scopeLabel; \
        explicit RHI_TRACKY_JOIN_(TrackyBoundScopeType_, __LINE__)(::vultra::rhi::CommandBuffer& inCb, const char* inLabel) : cb(inCb), scopeLabel(inLabel) \
        { \
            TRACKY_BIND_CMD_BUFFER(cb.getHandle(), cb.getCurrentRenderPassEncoderHandle(), \
                                   cb.getCurrentComputePassEncoderHandle()); \
            ::tracky::scope_enter(scopeLabel, ::tracky::ExtraFlags(::tracky::EFlags::GPU)); \
        } \
        ~RHI_TRACKY_JOIN_(TrackyBoundScopeType_, __LINE__)() \
        { \
            TRACKY_BIND_CMD_BUFFER(cb.getHandle(), cb.getCurrentRenderPassEncoderHandle(), \
                                   cb.getCurrentComputePassEncoderHandle()); \
            ::tracky::scope_leave(::tracky::ExtraFlags(::tracky::EFlags::GPU)); \
        } \
    } RHI_TRACKY_JOIN_(trackyBoundScopeVar_, __LINE__)(CmdBuf, Label)
#else
#define TRACKY_GPU_ZONE(CmdBuf, Label) \
    do \
    { \
        (void)(CmdBuf); \
        (void)(Label); \
    } while (0)
#endif

#define RHI_GPU_ZONE(CommandBuffer, Label) \
    RHI_NAMED_DEBUG_MARKER(CommandBuffer, Label); \
    TRACY_GPU_TRANSIENT_ZONE(CommandBuffer, Label); \
    TRACKY_GPU_ZONE(CommandBuffer, Label); \
    RHI_BUILTIN_PROFILER_GPU_ZONE(CommandBuffer, Label)

#define FG_GPU_ZONE(CommandBuffer) \
    TRACY_GPU_ZONE(CommandBuffer, "FrameGraph::Execute"); \
    TRACKY_GPU_ZONE(CommandBuffer, "FrameGraph::Execute");
