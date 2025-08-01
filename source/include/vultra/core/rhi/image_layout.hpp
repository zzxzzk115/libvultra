#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap12.html#VkImageLayout
        enum class ImageLayout
        {
            eUndefined = VK_IMAGE_LAYOUT_UNDEFINED,

            eGeneral    = VK_IMAGE_LAYOUT_GENERAL,
            eAttachment = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            eReadOnly   = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,

            eTransferSrc = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            eTransferDst = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,

            ePresent = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };
    } // namespace rhi
} // namespace vultra