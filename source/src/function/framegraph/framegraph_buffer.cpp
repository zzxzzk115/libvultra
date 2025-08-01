#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/base/string_util.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/render_context.hpp"
#include "vultra/function/framegraph/transient_resources.hpp"

namespace vultra
{
    namespace framegraph
    {
        void FrameGraphBuffer::create(const Desc& desc, void* allocator)
        {
            buffer = static_cast<TransientResources*>(allocator)->acquireBuffer(desc);
        }

        void FrameGraphBuffer::destroy(const Desc& desc, void* allocator)
        {
            static_cast<TransientResources*>(allocator)->releaseBuffer(desc, buffer);
            buffer = nullptr;
        }

        void FrameGraphBuffer::preRead(const Desc& desc, uint32_t flags, void* ctx)
        {
            ZoneScopedN("B*");

            auto&             rc = *static_cast<RenderContext*>(ctx);
            rhi::BarrierScope dst {};

            const auto bindingInfo = decodeBindingInfo(flags);
            if (static_cast<bool>(bindingInfo.pipelineStage & PipelineStage::eTransfer))
            {
                dst.accessMask = rhi::Access::eTransferRead;
            }
            else
            {
                switch (desc.type)
                {
                    case BufferType::eIndexBuffer:
                        dst = {
                            .stageMask  = rhi::PipelineStages::eVertexInput,
                            .accessMask = rhi::Access::eIndexRead,
                        };
                        break;
                    case BufferType::eVertexBuffer:
                        dst = {
                            .stageMask  = rhi::PipelineStages::eVertexInput,
                            .accessMask = rhi::Access::eVertexAttributeRead,
                        };
                        break;
                    case BufferType::eUniformBuffer:
                        dst.accessMask = rhi::Access::eUniformRead;
                        break;
                    case BufferType::eStorageBuffer:
                        dst.accessMask = rhi::Access::eShaderStorageRead;
                        break;
                }

                const auto [set, binding] = bindingInfo.location;
                switch (desc.type)
                {
                    case BufferType::eUniformBuffer:
                        rc.resourceSet[set][binding] = rhi::bindings::UniformBuffer {.buffer = buffer};
                        break;
                    case BufferType::eStorageBuffer:
                        rc.resourceSet[set][binding] = rhi::bindings::StorageBuffer {.buffer = buffer};
                        break;
                    default:
                        break;
                }
            }
            dst.stageMask |= convert(bindingInfo.pipelineStage);
            rc.commandBuffer.getBarrierBuilder().bufferBarrier({.buffer = *buffer}, dst);
        }

        void FrameGraphBuffer::preWrite(const Desc& desc, uint32_t flags, void* ctx)
        {
            ZoneScopedN("+B");

            auto& rc                             = *static_cast<RenderContext*>(ctx);
            const auto [location, pipelineStage] = decodeBindingInfo(flags);

            rhi::BarrierScope dst {};
            if (static_cast<bool>(pipelineStage & PipelineStage::eTransfer))
            {
                dst = {
                    .stageMask  = rhi::PipelineStages::eTransfer,
                    .accessMask = rhi::Access::eTransferWrite,
                };
            }
            else
            {
                VULTRA_CUSTOM_ASSERT(desc.type == BufferType::eStorageBuffer);
                dst.stageMask |= convert(pipelineStage);
                dst.accessMask = rhi::Access::eShaderStorageRead | rhi::Access::eShaderStorageWrite;

                const auto [set, binding]    = location;
                rc.resourceSet[set][binding] = rhi::bindings::StorageBuffer {.buffer = buffer};
            }
            rc.commandBuffer.getBarrierBuilder().bufferBarrier({.buffer = *buffer}, dst);
        }

        std::string FrameGraphBuffer::toString(const Desc& desc)
        {
            return std::format("size: {}", util::formatBytes(desc.dataSize()));
        }
    } // namespace framegraph
} // namespace vultra