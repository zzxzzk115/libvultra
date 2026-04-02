#pragma once

#include "vultra/core/rhi/interfaces/irender_device_backend.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"

#include <webgpu/webgpu.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace vultra
{
    namespace rhi
    {
        class WebGPURenderDeviceBackend final : public IRenderDeviceBackend
        {
        public:
            WebGPURenderDeviceBackend() = default;

            [[nodiscard]] RenderBackendApi getBackendApi() const override { return RenderBackendApi::eWebGPU; }

            [[nodiscard]] RenderDeviceFeatureFlagBits getFeatureFlag() const override { return m_FeatureFlag; }

            [[nodiscard]] RenderDeviceFeatureReport getFeatureReport() const override { return m_FeatureReport; }

            [[nodiscard]] RenderDeviceSyncCapabilities getSyncCapabilities() const override
            {
                return RenderDeviceSyncCapabilities {
                    .fence     = SyncPrimitiveSupport::eEmulated,
                    .semaphore = SyncPrimitiveSupport::eEmulated,
                };
            }

            [[nodiscard]] std::string getName() const override { return "WebGPU"; }

            [[nodiscard]] PhysicalDeviceInfo getPhysicalDeviceInfo() const override
            {
                return PhysicalDeviceInfo {.vendorId = 0u, .deviceId = 0u, .deviceName = m_FeatureReport.deviceName};
            }

            [[nodiscard]] openxr::XRDevice* getXRDevice() const override { return nullptr; }

            RenderDeviceFeatureReport   m_FeatureReport {};
            RenderDeviceFeatureFlagBits m_FeatureFlag {RenderDeviceFeatureFlagBits::eNormal};
            std::string                 m_AppName;

            WGPUInstance m_Instance {nullptr};
            WGPUAdapter  m_Adapter {nullptr};
            WGPUDevice   m_Device {nullptr};
            WGPUQueue    m_Queue {nullptr};

            mutable std::uintptr_t                     m_NextSyncHandle {1};
            mutable std::unordered_map<std::uintptr_t, bool> m_EmulatedFences;
            mutable std::unordered_set<std::uintptr_t>       m_EmulatedSemaphores;
        };
    } // namespace rhi
} // namespace vultra
