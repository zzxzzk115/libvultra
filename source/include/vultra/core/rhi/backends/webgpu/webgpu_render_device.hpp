#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/interfaces/irender_device.hpp"
#include "vultra/core/rhi/structs/handles.hpp"
#include "vultra/core/rhi/structs/pipeline_layout_structs.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#else
struct WGPUInstanceImpl;
struct WGPUAdapterImpl;
struct WGPUDeviceImpl;
struct WGPUQueueImpl;
struct WGPUTextureImpl;
struct WGPUTextureViewImpl;
struct WGPUCommandEncoderImpl;
struct WGPUComputePassEncoderImpl;
struct WGPURenderPassEncoderImpl;
struct WGPUCommandBufferImpl;
struct WGPUComputePipelineImpl;
struct WGPURenderPipelineImpl;
struct WGPUBindGroupImpl;
struct WGPUBindGroupLayoutImpl;
struct WGPUSamplerImpl;
using WGPUInstance = WGPUInstanceImpl*;
using WGPUAdapter  = WGPUAdapterImpl*;
using WGPUDevice   = WGPUDeviceImpl*;
using WGPUQueue    = WGPUQueueImpl*;
using WGPUTexture  = WGPUTextureImpl*;
using WGPUTextureView = WGPUTextureViewImpl*;
using WGPUCommandEncoder = WGPUCommandEncoderImpl*;
using WGPUComputePassEncoder = WGPUComputePassEncoderImpl*;
using WGPURenderPassEncoder = WGPURenderPassEncoderImpl*;
using WGPUCommandBuffer = WGPUCommandBufferImpl*;
using WGPUComputePipeline = WGPUComputePipelineImpl*;
using WGPURenderPipeline = WGPURenderPipelineImpl*;
using WGPUBindGroup = WGPUBindGroupImpl*;
using WGPUBindGroupLayout = WGPUBindGroupLayoutImpl*;
using WGPUSampler = WGPUSamplerImpl*;
#endif

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class WebGPURenderDevice final : public IRenderDevice
        {
        public:
            explicit WebGPURenderDevice(std::string_view appName);
            ~WebGPURenderDevice() override;

            WebGPURenderDevice(const WebGPURenderDevice&) = delete;
            WebGPURenderDevice(WebGPURenderDevice&&) noexcept = delete;
            WebGPURenderDevice& operator=(const WebGPURenderDevice&) = delete;
            WebGPURenderDevice& operator=(WebGPURenderDevice&&) noexcept = delete;

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

            [[nodiscard]] bool supportsSwapchain() const override { return true; }

            [[nodiscard]] std::string getName() const override { return "WebGPU"; }

            [[nodiscard]] PhysicalDeviceInfo getPhysicalDeviceInfo() const override
            {
                return PhysicalDeviceInfo {.vendorId = 0u, .deviceId = 0u, .deviceName = m_FeatureReport.deviceName};
            }

            [[nodiscard]] openxr::XRDevice* getXRDevice() const override { return nullptr; }
            [[nodiscard]] std::array<float, 2> getLineWidthRange() const override { return {1.0f, 1.0f}; }
            [[nodiscard]] float                getMaxSamplerAnisotropy() const override { return 1.0f; }
            [[nodiscard]] uint64_t             getFormatFeatureFlagsOptimal(PixelFormat) const override;

            RenderDeviceFeatureReport   m_FeatureReport {};
            RenderDeviceFeatureFlagBits m_FeatureFlag {RenderDeviceFeatureFlagBits::eNormal};
            std::string                 m_AppName;

            WGPUInstance m_Instance {nullptr};
            WGPUAdapter  m_Adapter {nullptr};
            WGPUDevice   m_Device {nullptr};
            WGPUQueue    m_Queue {nullptr};
            bool         m_SupportsTextureCompressionBC {false};
            bool         m_SupportsTimestampQuery {false};

            mutable std::uintptr_t                     m_NextSyncHandle {1};
            mutable std::unordered_map<std::uintptr_t, bool> m_EmulatedFences;
            mutable std::unordered_set<std::uintptr_t>       m_EmulatedSemaphores;
            mutable std::unordered_map<std::size_t, SamplerHandle> m_Samplers;
            mutable std::uintptr_t                               m_NextSamplerHandle {1};
            mutable std::uintptr_t                               m_NextBufferHandle {1};

            mutable std::unordered_map<std::size_t, WGPUBindGroupLayout> m_DescriptorSetLayouts;
            mutable std::unordered_map<std::size_t, WGPUPipelineLayout>  m_PipelineLayouts;
            mutable std::unordered_map<std::size_t, std::vector<DescriptorSetLayoutBindingEx>> m_DescriptorSetLayoutBindings;
            mutable WGPUBindGroupLayout m_EmptyDescriptorSetLayout {nullptr};
        };
    } // namespace rhi
} // namespace vultra
