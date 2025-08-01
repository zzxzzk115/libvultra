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

    } // namespace rhi
} // namespace vultra