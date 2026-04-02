#pragma once

#include "vultra/core/profiling/tracy_wrapper.hpp"
#include "vultra/core/rhi/interfaces/irender_device_backend.hpp"
#include "vultra/core/rhi/shader_compiler.hpp"
#include "vultra/core/rhi/structs/handles.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <set>
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
        class VulkanImGuiBackend;

        struct VulkanRenderDeviceBackend final : IRenderDeviceBackend
        {
            friend class VulkanImGuiBackend;

            [[nodiscard]] RenderBackendApi getBackendApi() const override { return RenderBackendApi::eVulkan; }

            [[nodiscard]] RenderDeviceFeatureFlagBits getFeatureFlag() const override { return m_FeatureFlag; }

            [[nodiscard]] RenderDeviceFeatureReport getFeatureReport() const override { return m_FeatureReport; }

            [[nodiscard]] RenderDeviceSyncCapabilities getSyncCapabilities() const override
            {
                return RenderDeviceSyncCapabilities {
                    .fence     = SyncPrimitiveSupport::eNative,
                    .semaphore = SyncPrimitiveSupport::eNative,
                };
            }

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

            std::set<std::string>       m_SupportedExtensions;
            RenderDeviceFeatureReport   m_FeatureReport {};
            RenderDeviceFeatureFlagBits m_FeatureFlag {RenderDeviceFeatureFlagBits::eNormal};
            std::string                 m_AppName;
            std::vector<const char*>    m_RequiredInstanceExtensions;
            bool                        m_UseKhrDynamicRendering {false};
            bool                        m_UseKhrSynchronization2 {false};

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
