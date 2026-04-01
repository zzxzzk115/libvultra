#pragma once

#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    namespace rhi
    {
        // TODO: Split the Vulkan implementation out of RenderDevice into this backend type.
        using VulkanRenderDeviceBackend = RenderDevice;
    } // namespace rhi
} // namespace vultra
