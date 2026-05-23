#pragma once

#include "vultra/core/rhi/render_device.hpp"

#include <algorithm>
#include <cstdint>
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
            pruneRetiredFrames();
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
            currentFrame().uniformBuffers.push_back(std::move(buffer));
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
            currentFrame().storageBuffers.push_back(std::move(buffer));
            return ptr;
        }

        void clear()
        {
            m_Frames.clear();
        }

    private:
        static constexpr uint64_t kReleaseDelayFrames = 4;

        struct FrameBucket
        {
            uint64_t                                         frameIndex {0};
            std::vector<std::unique_ptr<rhi::UniformBuffer>> uniformBuffers;
            std::vector<std::unique_ptr<rhi::StorageBuffer>> storageBuffers;
        };

        FrameBucket& currentFrame()
        {
            auto it = std::find_if(m_Frames.begin(), m_Frames.end(), [this](const FrameBucket& bucket) {
                return bucket.frameIndex == m_FrameIndex;
            });
            if (it == m_Frames.end())
                it = m_Frames.emplace(m_Frames.end(), FrameBucket {.frameIndex = m_FrameIndex});
            return *it;
        }

        void pruneRetiredFrames()
        {
            std::erase_if(m_Frames, [this](const FrameBucket& bucket) {
                return bucket.frameIndex + kReleaseDelayFrames < m_FrameIndex;
            });
        }

        uint64_t                 m_FrameIndex {0};
        std::vector<FrameBucket> m_Frames;
    };
} // namespace vultra
