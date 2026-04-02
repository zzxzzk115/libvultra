#include "vultra/core/rhi/interfaces/render_device_backend_access.hpp"

#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    namespace rhi
    {
        IRenderDeviceBackend* RenderDeviceBackendAccess::get(RenderDevice& rd) { return rd.m_Backend.get(); }

        const IRenderDeviceBackend* RenderDeviceBackendAccess::get(const RenderDevice& rd) { return rd.m_Backend.get(); }

        std::uintptr_t RenderDeviceBackendAccess::getDescriptorSetLayoutBackendHandle(
            const RenderDevice& rd, const DescriptorSetLayoutKey layoutKey)
        {
            return rd.getDescriptorSetLayoutBackendHandle(layoutKey);
        }
    } // namespace rhi
} // namespace vultra
