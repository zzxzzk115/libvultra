#pragma once

#include "vultra/core/base/scoped_enum_flags.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap7.html#synchronization-access-types
        enum class Access : VkAccessFlags2
        {
            eNone = VK_ACCESS_2_NONE,

            eIndexRead                   = VK_ACCESS_2_INDEX_READ_BIT,
            eVertexAttributeRead         = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
            eUniformRead                 = VK_ACCESS_2_UNIFORM_READ_BIT,
            eShaderRead                  = VK_ACCESS_2_SHADER_READ_BIT,
            eShaderWrite                 = VK_ACCESS_2_SHADER_WRITE_BIT,
            eColorAttachmentRead         = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            eColorAttachmentWrite        = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            eDepthStencilAttachmentRead  = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            eDepthStencilAttachmentWrite = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            eTransferRead                = VK_ACCESS_2_TRANSFER_READ_BIT,
            eTransferWrite               = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            eMemoryRead                  = VK_ACCESS_2_MEMORY_READ_BIT,
            eMemoryWrite                 = VK_ACCESS_2_MEMORY_WRITE_BIT,
            eShaderStorageRead           = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            eShaderStorageWrite          = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,

            // Raytracing
            eAccelerationStructureRead  = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            eAccelerationStructureWrite = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        };
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::Access> : std::true_type
{};