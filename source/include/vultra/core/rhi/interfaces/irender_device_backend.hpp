#pragma once

#include "vultra/core/rhi/structs/render_device_structs.hpp"

namespace vultra
{
    namespace openxr
    {
        class XRDevice;
    }
}

namespace vultra
{
    namespace rhi
    {
        class IRenderDeviceBackend
        {
        public:
            virtual ~IRenderDeviceBackend() = default;

            [[nodiscard]] virtual RenderBackendApi            getBackendApi() const        = 0;
            [[nodiscard]] virtual RenderDeviceFeatureFlagBits getFeatureFlag() const       = 0;
            [[nodiscard]] virtual RenderDeviceFeatureReport   getFeatureReport() const     = 0;
            [[nodiscard]] virtual RenderDeviceSyncCapabilities getSyncCapabilities() const = 0;
            [[nodiscard]] virtual std::string                 getName() const              = 0;
            [[nodiscard]] virtual PhysicalDeviceInfo          getPhysicalDeviceInfo() const = 0;
            [[nodiscard]] virtual openxr::XRDevice*           getXRDevice() const          = 0;
        };
    } // namespace rhi
} // namespace vultra
