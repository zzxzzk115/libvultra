#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class WebGPURenderDeviceAccess final
        {
        public:
            [[nodiscard]] static bool           isWebGPUBackend(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getInstanceHandle(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getAdapterHandle(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getDeviceHandle(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getQueueHandle(const RenderDevice&);
            static bool                         submitCommandBuffer(const RenderDevice&, std::uintptr_t commandBufferHandle);
            static void                         processEvents(const RenderDevice&);
        };
    } // namespace rhi
} // namespace vultra
