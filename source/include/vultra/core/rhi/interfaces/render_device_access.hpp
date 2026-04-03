#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class IRenderDevice;
        struct DescriptorSetLayoutKey;

        class RenderDeviceAccess final
        {
        public:
            [[nodiscard]] static IRenderDevice*       get(RenderDevice&);
            [[nodiscard]] static const IRenderDevice* get(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t              getDescriptorSetLayoutHandle(const RenderDevice&,
                                                                                                  DescriptorSetLayoutKey);
        };
    } // namespace rhi
} // namespace vultra
