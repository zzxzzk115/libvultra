#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class IRenderDeviceBackend;
        struct DescriptorSetLayoutKey;

        class RenderDeviceBackendAccess final
        {
        public:
            [[nodiscard]] static IRenderDeviceBackend*       get(RenderDevice&);
            [[nodiscard]] static const IRenderDeviceBackend* get(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t              getDescriptorSetLayoutBackendHandle(const RenderDevice&,
                                                                                                  DescriptorSetLayoutKey);
        };
    } // namespace rhi
} // namespace vultra
