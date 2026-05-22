#pragma once

#include "vultra/core/profiling/tracy_wrapper.hpp"
#include "vultra/core/rhi/interfaces/irender_device.hpp"
#include "vultra/core/rhi/shader_compiler.hpp"
#include "vultra/core/rhi/structs/handles.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <set>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    namespace openxr
    {
        class XRDevice;
    }

    namespace rhi
    {
        class VulkanImGui;

        struct VulkanRenderDevice final : IRenderDevice
        {
            friend class VulkanImGui;

            [[nodiscard]] RenderBackendApi getBackendApi() const override { return RenderBackendApi::eVulkan; }

            [[nodiscard]] RenderDeviceFeatureFlagBits getFeatureFlag() const override { return m_FeatureFlag; }

            [[nodiscard]] RenderDeviceFeatureReport getFeatureReport() const override { return m_FeatureReport; }

            [[nodiscard]] RenderDeviceLimits getLimits() const override { return m_Limits; }

            [[nodiscard]] RenderDeviceSyncCapabilities getSyncCapabilities() const override
            {
                return RenderDeviceSyncCapabilities {
                    .fence     = SyncPrimitiveSupport::eNative,
                    .semaphore = SyncPrimitiveSupport::eNative,
                };
            }

            [[nodiscard]] bool supportsSwapchain() const override { return true; }

            [[nodiscard]] std::string getName() const override
            {
                if (!m_PhysicalDevice)
                {
                    return "Vulkan";
                }
                const auto v = m_PhysicalDevice.getProperties().apiVersion;
                return std::format("Vulkan {}.{}.{}",
                                   VK_API_VERSION_MAJOR(v),
                                   VK_API_VERSION_MINOR(v),
                                   VK_API_VERSION_PATCH(v));
            }

            [[nodiscard]] PhysicalDeviceInfo getPhysicalDeviceInfo() const override
            {
                if (!m_PhysicalDevice)
                {
                    return PhysicalDeviceInfo {.vendorId = 0u, .deviceId = 0u, .deviceName = "Unknown Vulkan Device"};
                }

                const auto props = m_PhysicalDevice.getProperties();
                return PhysicalDeviceInfo {.vendorId = props.vendorID, .deviceId = props.deviceID, .deviceName = props.deviceName};
            }

            [[nodiscard]] openxr::XRDevice* getXRDevice() const override { return m_XRDevice; }
            void beginFrameGpuQuery(std::uintptr_t commandBufferHandle) override;
            void endFrameGpuQuery(std::uintptr_t commandBufferHandle) override;
            [[nodiscard]] double consumeGpuFrameMs() override;
            [[nodiscard]] uint64_t beginScopeGpuQuery(std::uintptr_t commandBufferHandle) override;
            void                   endScopeGpuQuery(std::uintptr_t commandBufferHandle, uint64_t scopeToken) override;
            [[nodiscard]] double   consumeScopeGpuMs(uint64_t scopeToken) override;

            void onMemoryAllocated(RenderMemoryKind kind, uint64_t bytes) override { m_MemoryTracker.add(kind, bytes); }
            void onMemoryFreed(RenderMemoryKind kind, uint64_t bytes) override { m_MemoryTracker.remove(kind, bytes); }
            [[nodiscard]] RenderDeviceMemoryStats getMemoryStats() const override { return m_MemoryTracker.snapshot(); }

            [[nodiscard]] std::array<float, 2> getLineWidthRange() const override;
            [[nodiscard]] float                getMaxSamplerAnisotropy() const override;
            [[nodiscard]] uint64_t             getFormatFeatureFlagsOptimal(PixelFormat) const override;

            std::set<std::string>       m_SupportedExtensions;
            RenderDeviceFeatureReport   m_FeatureReport {};
            RenderDeviceLimits          m_Limits {};
            RenderDeviceFeatureFlagBits m_FeatureFlag {RenderDeviceFeatureFlagBits::eNormal};
            std::string                 m_AppName;
            std::vector<const char*>    m_RequiredInstanceExtensions;
            bool                        m_UseKhrDynamicRendering {false};
            bool                        m_UseKhrSynchronization2 {false};
            bool                        m_EnableValidation {defaultRenderDiagnosticsEnabled()};
            bool                        m_EnableDebugMarkers {defaultRenderDiagnosticsEnabled()};

            vk::Instance               m_Instance {nullptr};
            vk::DebugUtilsMessengerEXT m_DebugMessenger {nullptr};
            vk::Device                 m_Device {nullptr};
            int                        m_GenericQueueFamilyIndex {-1};
            vk::Queue                  m_GenericQueue {nullptr};
            vk::PhysicalDevice         m_PhysicalDevice {nullptr};
            vma::Allocator             m_MemoryAllocator {nullptr};
            vk::CommandPool            m_CommandPool {nullptr};
            vk::PipelineCache          m_PipelineCache {nullptr};
            vk::DescriptorPool         m_DefaultDescriptorPool {nullptr};

            vk::PhysicalDeviceRayTracingPipelinePropertiesKHR  m_RayTracingPipelineProperties;
            vk::PhysicalDeviceAccelerationStructureFeaturesKHR m_AccelerationStructureFeatures;

            TracyGpuContext m_TracyContext {nullptr};

            vk::QueryPool           m_FrameTimeQueryPool {nullptr};
            uint32_t                m_FrameTimeSlotCount {0};
            uint32_t                m_FrameTimeNextSlot {0};
            int32_t                 m_ActiveFrameTimeSlot {-1};
            std::deque<uint32_t>    m_PendingFrameTimeSlots;
            double                  m_LastGpuFrameMs {-1.0};

            struct ScopeTimeQuerySlot
            {
                uint64_t token {0};
                bool     active {false};
                bool     pending {false};
                bool     resolved {false};
                double   ms {-1.0};
            };
            vk::QueryPool                              m_ScopeTimeQueryPool {nullptr};
            uint32_t                                   m_ScopeTimeSlotCount {0};
            uint32_t                                   m_ScopeTimeNextSlot {0};
            uint64_t                                   m_ScopeTimeNextToken {1};
            std::vector<ScopeTimeQuerySlot>            m_ScopeTimeSlots;
            std::unordered_map<uint64_t, uint32_t>     m_ScopeTimeTokenToSlot;
            float                   m_TimestampPeriodNs {1.0f};
            RenderDeviceMemoryTracker m_MemoryTracker;

            template<typename T>
            using Cache = std::unordered_map<size_t, T>;

            mutable Cache<SamplerHandle>   m_Samplers;
            Cache<vk::DescriptorSetLayout> m_DescriptorSetLayouts;
            Cache<vk::PipelineLayout>      m_PipelineLayouts;

            ShaderCompiler m_ShaderCompiler;

            openxr::XRDevice* m_XRDevice {nullptr};
        };
    } // namespace rhi
} // namespace vultra
