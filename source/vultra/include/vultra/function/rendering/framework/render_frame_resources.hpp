#pragma once

#include "vultra/core/rhi/render_device.hpp"

#include <cstring>
#include <memory>
#include <vector>

namespace vultra
{
    class RenderFrameResources
    {
    public:
        void beginFrame(uint64_t frameIndex)
        {
            m_FrameIndex = frameIndex;
            clear();
        }

        template<typename T>
        rhi::UniformBuffer* uploadUniform(rhi::RenderDevice& rd, const T& value)
        {
            auto buffer = std::make_unique<rhi::UniformBuffer>(
                rd.createUniformBuffer(sizeof(T), rhi::AllocationHints::eSequentialWrite));
            auto* mapped = buffer->map();
            std::memcpy(mapped, &value, sizeof(T));
            buffer->flush().unmap();

            auto* ptr = buffer.get();
            m_UniformBuffers.push_back(std::move(buffer));
            return ptr;
        }

        template<typename T>
        rhi::StorageBuffer* uploadStorage(rhi::RenderDevice& rd, const T* data, size_t count)
        {
            auto buffer = std::make_unique<rhi::StorageBuffer>(
                rd.createStorageBuffer(sizeof(T) * count, rhi::AllocationHints::eSequentialWrite));
            auto* mapped = buffer->map();
            std::memcpy(mapped, data, sizeof(T) * count);
            buffer->flush().unmap();

            auto* ptr = buffer.get();
            m_StorageBuffers.push_back(std::move(buffer));
            return ptr;
        }

        void clear()
        {
            m_UniformBuffers.clear();
            m_StorageBuffers.clear();
        }

    private:
        uint64_t                                         m_FrameIndex {0};
        std::vector<std::unique_ptr<rhi::UniformBuffer>> m_UniformBuffers;
        std::vector<std::unique_ptr<rhi::StorageBuffer>> m_StorageBuffers;
    };
} // namespace vultra
