#include "vultra/core/rhi/interfaces/render_device_access.hpp"

#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    namespace rhi
    {
        IRenderDevice* RenderDeviceAccess::get(RenderDevice& rd) { return rd.m_Backend.get(); }

        const IRenderDevice* RenderDeviceAccess::get(const RenderDevice& rd) { return rd.m_Backend.get(); }

        std::uintptr_t RenderDeviceAccess::getDescriptorSetLayoutHandle(const RenderDevice&          rd,
                                                                        const DescriptorSetLayoutKey layoutKey)
        {
            return rd.getDescriptorSetLayoutHandle(layoutKey);
        }
    } // namespace rhi
} // namespace vultra
