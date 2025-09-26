#pragma once

#include <vulkan/vulkan.hpp>

#include <span>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class Buffer;
        class Texture;

        void upload(RenderDevice&,
                    const Buffer&                        srcStagingBuffer,
                    std::span<const vk::BufferImageCopy> copyRegions,
                    Texture&                             dst,
                    const bool                           generateMipmaps = false);

        uint32_t alignedSize(const uint32_t size, const uint32_t alignment);
    } // namespace rhi
} // namespace vultra