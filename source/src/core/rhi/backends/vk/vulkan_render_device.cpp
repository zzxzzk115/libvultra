#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/base/ranges.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_buffer.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_acceleration_structure_backend.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_compute_pipeline_backend.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_pipeline_backend.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_render_device_backend.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_render_device_backend.hpp"
#include "vultra/core/rhi/raytracing_pipeline.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_radix_sorter.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_command_buffer.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"
#include "vultra/function/openxr/xr_device.hpp"

#include <vshadersystem/reflect.hpp>

#include <glm/glm.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <exception>
#include <set>
#include <thread>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace std
{
    template<>
    struct hash<vk::ShaderStageFlags>
    {
        size_t operator()(const vk::ShaderStageFlags& v) const noexcept
        {
            return hash<VkShaderStageFlags>()(static_cast<VkShaderStageFlags>(v));
        }
    };

    template<>
    struct hash<vk::DescriptorBindingFlags>
    {
        size_t operator()(const vk::DescriptorBindingFlags& v) const noexcept
        {
            return hash<VkDescriptorBindingFlags>()(static_cast<VkDescriptorBindingFlags>(v));
        }
    };

    template<>
    struct hash<vk::DescriptorSetLayoutBinding>
    {
        auto operator()(const vk::DescriptorSetLayoutBinding& v) const noexcept
        {
            size_t h {0};
            hashCombine(h, v.binding, v.descriptorType, v.descriptorCount, v.stageFlags);
            return h;
        }
    };

    template<>
    struct hash<vultra::rhi::DescriptorSetLayoutBindingEx>
    {
        auto operator()(const vultra::rhi::DescriptorSetLayoutBindingEx& v) const noexcept
        {
            size_t h {0};
            hashCombine(h, v.binding, v.type, v.count, v.stageFlags, v.flags);
            return h;
        }
    };

    template<>
    struct hash<vultra::rhi::PushConstantRange>
    {
        auto operator()(const vultra::rhi::PushConstantRange& v) const noexcept
        {
            size_t h {0};
            hashCombine(h, v.offset, v.size, v.stageFlags);
            return h;
        }
    };

    template<>
    struct hash<vultra::rhi::SamplerInfo>
    {
        auto operator()(const vultra::rhi::SamplerInfo& v) const noexcept
        {
            size_t h {0};
            hashCombine(h,
                        v.magFilter,
                        v.minFilter,
                        v.mipmapMode,
                        v.addressModeS,
                        v.addressModeT,
                        v.addressModeR,
                        v.maxAnisotropy.has_value(),
                        v.maxAnisotropy ? *v.maxAnisotropy : 0.0f,
                        v.compareOp.has_value(),
                        v.compareOp ? *v.compareOp : vultra::rhi::CompareOp::eNever,
                        static_cast<int32_t>(v.minLod),
                        static_cast<int32_t>(v.maxLod),
                        static_cast<int32_t>(v.borderColor));
            return h;
        }
    };

} // namespace std

namespace
{
    [[nodiscard]] vk::ShaderModule createVulkanShaderModule(const vk::Device device, const vultra::rhi::SPIRV& spv)
    {
        vk::ShaderModule handle {nullptr};
        vk::ShaderModuleCreateInfo createInfo {};
        createInfo.codeSize = sizeof(uint32_t) * spv.size();
        createInfo.pCode    = spv.data();
        VK_CHECK(device.createShaderModule(&createInfo, nullptr, &handle),
                 "RenderDevice",
                 "Failed to create shader module");
        return handle;
    }

    [[nodiscard]] vk::IndexType toVkIndexType(const vultra::rhi::IndexType indexType)
    {
        switch (indexType)
        {
            case vultra::rhi::IndexType::eUInt16:
                return vk::IndexType::eUint16;
            case vultra::rhi::IndexType::eUInt32:
                return vk::IndexType::eUint32;

            default:
                assert(false);
                return vk::IndexType::eNoneKHR;
        }
    }

    [[nodiscard]] bool isRaytracingOrRayQueryEnabled(const vultra::rhi::RenderDeviceFeatureFlagBits featureFlag)
    {
        return HasFlagValues(featureFlag, vultra::rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline) ||
               HasFlagValues(featureFlag, vultra::rhi::RenderDeviceFeatureFlagBits::eRayQuery);
    }
} // namespace

namespace
{
    [[nodiscard]] std::string toStdString(const WGPUStringView str)
    {
        if (!str.data)
        {
            return {};
        }
        if (str.length == WGPU_STRLEN)
        {
            return std::string {str.data};
        }
        return std::string {str.data, str.length};
    }

    [[nodiscard]] const char* toWgpuRequestAdapterStatusString(const WGPURequestAdapterStatus status)
    {
        switch (status)
        {
            case WGPURequestAdapterStatus_Success:
                return "Success";
            case WGPURequestAdapterStatus_InstanceDropped:
                return "InstanceDropped";
            case WGPURequestAdapterStatus_Unavailable:
                return "Unavailable";
            case WGPURequestAdapterStatus_Error:
                return "Error";
            case WGPURequestAdapterStatus_Unknown:
                return "Unknown";
            default:
                return "Invalid";
        }
    }

    [[nodiscard]] const char* toWgpuRequestDeviceStatusString(const WGPURequestDeviceStatus status)
    {
        switch (status)
        {
            case WGPURequestDeviceStatus_Success:
                return "Success";
            case WGPURequestDeviceStatus_InstanceDropped:
                return "InstanceDropped";
            case WGPURequestDeviceStatus_Error:
                return "Error";
            case WGPURequestDeviceStatus_Unknown:
                return "Unknown";
            default:
                return "Invalid";
        }
    }

    struct AdapterRequestResult
    {
        bool                     completed {false};
        WGPURequestAdapterStatus status {WGPURequestAdapterStatus_Unknown};
        WGPUAdapter              adapter {nullptr};
        std::string              message;
    };

    struct DeviceRequestResult
    {
        bool                    completed {false};
        WGPURequestDeviceStatus status {WGPURequestDeviceStatus_Unknown};
        WGPUDevice              device {nullptr};
        std::string             message;
    };

    void onRequestAdapter(const WGPURequestAdapterStatus status,
                          const WGPUAdapter              adapter,
                          const WGPUStringView           message,
                          void*                          userdata1,
                          void*)
    {
        auto* result     = static_cast<AdapterRequestResult*>(userdata1);
        result->completed = true;
        result->status    = status;
        result->adapter   = adapter;
        result->message   = toStdString(message);
    }

    void onRequestDevice(const WGPURequestDeviceStatus status,
                         const WGPUDevice              device,
                         const WGPUStringView          message,
                         void*                         userdata1,
                         void*)
    {
        auto* result     = static_cast<DeviceRequestResult*>(userdata1);
        result->completed = true;
        result->status    = status;
        result->device    = device;
        result->message   = toStdString(message);
    }

    void onDeviceLost(const WGPUDevice*,
                      const WGPUDeviceLostReason reason,
                      const WGPUStringView       message,
                      void*,
                      void*)
    {
        VULTRA_CORE_ERROR("[RenderDevice] WebGPU device lost (reason={}): {}",
                          static_cast<int>(reason),
                          toStdString(message));
    }

    void onUncapturedError(const WGPUDevice*,
                           const WGPUErrorType type,
                           const WGPUStringView message,
                           void*,
                           void*)
    {
        VULTRA_CORE_ERROR("[RenderDevice] WebGPU uncaptured error (type={}): {}",
                          static_cast<int>(type),
                          toStdString(message));
    }

    template<typename Predicate>
    void waitForFuture(WGPUInstance instance, const WGPUFuture future, Predicate&& done)
    {
        WGPUFutureWaitInfo waitInfo {};
        waitInfo.future    = future;

        while (!done())
        {
            waitInfo.completed = false;
            const auto waitStatus = wgpuInstanceWaitAny(instance, 1, &waitInfo, 0);
            if (waitStatus != WGPUWaitStatus_Success && waitStatus != WGPUWaitStatus_TimedOut)
            {
                throw std::runtime_error(std::format("wgpuInstanceWaitAny failed with status {}", static_cast<int>(waitStatus)));
            }

            if (waitStatus == WGPUWaitStatus_TimedOut)
            {
                wgpuInstanceProcessEvents(instance);
                std::this_thread::yield();
            }
        }
    }

    [[nodiscard]] vultra::rhi::VulkanRenderDeviceBackend&
    backendOf(std::unique_ptr<vultra::rhi::IRenderDeviceBackend>& backend)
    {
        assert(backend);
        auto* vkBackend = dynamic_cast<vultra::rhi::VulkanRenderDeviceBackend*>(backend.get());
        assert(vkBackend && "RenderDevice backend is not Vulkan");
        return *vkBackend;
    }

    [[nodiscard]] const vultra::rhi::VulkanRenderDeviceBackend&
    backendOf(const std::unique_ptr<vultra::rhi::IRenderDeviceBackend>& backend)
    {
        assert(backend);
        auto* vkBackend = dynamic_cast<const vultra::rhi::VulkanRenderDeviceBackend*>(backend.get());
        assert(vkBackend && "RenderDevice backend is not Vulkan");
        return *vkBackend;
    }

    [[nodiscard]] vultra::rhi::WebGPURenderDeviceBackend&
    webgpuBackendOf(std::unique_ptr<vultra::rhi::IRenderDeviceBackend>& backend)
    {
        assert(backend);
        auto* webgpuBackend = dynamic_cast<vultra::rhi::WebGPURenderDeviceBackend*>(backend.get());
        assert(webgpuBackend && "RenderDevice backend is not WebGPU");
        return *webgpuBackend;
    }

    [[nodiscard]] const vultra::rhi::WebGPURenderDeviceBackend&
    webgpuBackendOf(const std::unique_ptr<vultra::rhi::IRenderDeviceBackend>& backend)
    {
        assert(backend);
        auto* webgpuBackend = dynamic_cast<const vultra::rhi::WebGPURenderDeviceBackend*>(backend.get());
        assert(webgpuBackend && "RenderDevice backend is not WebGPU");
        return *webgpuBackend;
    }
}

namespace
{
#if _DEBUG
    const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
#endif

    const char* requestLayers[] = {"VK_LAYER_KHRONOS_synchronization2"};

    VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                 vk::DebugUtilsMessageTypeFlagsEXT             messageType,
                                                 const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                 void*)
    {
        switch (messageSeverity)
        {
            case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
                VULTRA_CORE_TRACE("{} {}", vk::to_string(messageType), pCallbackData->pMessage);
                break;

            case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
                VULTRA_CORE_INFO("{} {}", vk::to_string(messageType), pCallbackData->pMessage);
                break;

            case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
                VULTRA_CORE_WARN("{} {}", vk::to_string(messageType), pCallbackData->pMessage);
                break;

            case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
                VULTRA_CORE_ERROR("{} {}", vk::to_string(messageType), pCallbackData->pMessage);
                break;
        }

        return VK_FALSE;
    }

    void setupDebugMessenger(vk::Instance instance, vk::DebugUtilsMessengerEXT& debugMessenger)
    {
        vk::DebugUtilsMessengerCreateInfoEXT createInfo {};
        createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                     vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                     vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                 vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                 vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;

#if VK_VERSION_1_4
        createInfo.pfnUserCallback = debugCallback;
#else
        createInfo.pfnUserCallback = reinterpret_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(debugCallback);
#endif

        debugMessenger = instance.createDebugUtilsMessengerEXT(createInfo);
    }

    [[nodiscard]] constexpr auto makeAllocationFlags(const vultra::rhi::AllocationHints hints)
    {
        vma::AllocationCreateFlags flags {0};
        if (HasFlagValues(hints, vultra::rhi::AllocationHints::eMinMemory))
        {
            flags |= vma::AllocationCreateFlagBits::eStrategyMinMemory;
        }
        if (HasFlagValues(hints, vultra::rhi::AllocationHints::eSequentialWrite))
        {
            flags |= vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped;
        }
        if (HasFlagValues(hints, vultra::rhi::AllocationHints::eRandomAccess))
        {
            flags |= vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped;
        }
        return flags;
    }

    [[nodiscard]] vultra::rhi::Buffer makeBuffer(const vma::Allocator             allocator,
                                                 const uint64_t                  size,
                                                 const vultra::rhi::BufferUsage  usage,
                                                 const vma::AllocationCreateFlags flags,
                                                 const vma::MemoryUsage          memoryUsage)
    {
        return vultra::rhi::Buffer {
            std::make_unique<vultra::rhi::VulkanBuffer>(allocator, size, usage, flags, memoryUsage)};
    }
} // namespace

namespace vultra
{
    namespace rhi
    {
        constexpr auto LOGTAG = "RenderDevice";

        RenderDevice::RenderDevice(const RenderDeviceFeatureFlagBits featureFlag,
                                   const std::string_view            appName,
                                   const std::span<const char* const> requiredInstanceExtensions,
                                   const RenderBackendApi            backendApi)
        {
            switch (backendApi)
            {
                case RenderBackendApi::eAuto:
                case RenderBackendApi::eVulkan:
                    m_Backend = std::make_unique<VulkanRenderDeviceBackend>();
                    break;
                case RenderBackendApi::eWebGPU:
                {
                    m_Backend = std::make_unique<WebGPURenderDeviceBackend>();
                    auto& backend      = webgpuBackendOf(m_Backend);
                    backend.m_FeatureFlag = RenderDeviceFeatureFlagBits::eNormal;
                    backend.m_AppName     = appName;

                    WGPUInstanceDescriptor instanceDesc {};
                    instanceDesc.nextInChain = nullptr;
                    backend.m_Instance        = wgpuCreateInstance(&instanceDesc);
                    if (!backend.m_Instance)
                    {
                        throw std::runtime_error("Failed to create WebGPU instance");
                    }

                    WGPURequestAdapterOptions adapterOptions {};
                    adapterOptions.featureLevel = WGPUFeatureLevel_Core;
                    adapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;
                    adapterOptions.forceFallbackAdapter = false;
                    adapterOptions.backendType = WGPUBackendType_Undefined;
                    adapterOptions.compatibleSurface = nullptr;

                    AdapterRequestResult adapterResult {};
                    WGPURequestAdapterCallbackInfo adapterCallbackInfo {};
                    adapterCallbackInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
                    adapterCallbackInfo.callback  = onRequestAdapter;
                    adapterCallbackInfo.userdata1 = &adapterResult;
                    adapterCallbackInfo.userdata2 = nullptr;

                    const auto adapterFuture = wgpuInstanceRequestAdapter(backend.m_Instance, &adapterOptions, adapterCallbackInfo);
                    waitForFuture(backend.m_Instance, adapterFuture, [&adapterResult]() { return adapterResult.completed; });
                    if (adapterResult.status != WGPURequestAdapterStatus_Success || !adapterResult.adapter)
                    {
                        throw std::runtime_error(std::format("Failed to request WebGPU adapter ({}) {}",
                                                             toWgpuRequestAdapterStatusString(adapterResult.status),
                                                             adapterResult.message));
                    }
                    backend.m_Adapter = adapterResult.adapter;

                    WGPUDeviceDescriptor deviceDesc {};
                    deviceDesc.label.data   = backend.m_AppName.c_str();
                    deviceDesc.label.length = WGPU_STRLEN;
                    deviceDesc.requiredFeatureCount = 0;
                    deviceDesc.requiredFeatures     = nullptr;
                    deviceDesc.requiredLimits       = nullptr;
                    deviceDesc.defaultQueue.label.data = backend.m_AppName.c_str();
                    deviceDesc.defaultQueue.label.length = WGPU_STRLEN;
                    deviceDesc.deviceLostCallbackInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
                    deviceDesc.deviceLostCallbackInfo.callback  = onDeviceLost;
                    deviceDesc.deviceLostCallbackInfo.userdata1 = nullptr;
                    deviceDesc.deviceLostCallbackInfo.userdata2 = nullptr;
                    deviceDesc.uncapturedErrorCallbackInfo.callback  = onUncapturedError;
                    deviceDesc.uncapturedErrorCallbackInfo.userdata1 = nullptr;
                    deviceDesc.uncapturedErrorCallbackInfo.userdata2 = nullptr;

                    DeviceRequestResult deviceResult {};
                    WGPURequestDeviceCallbackInfo deviceCallbackInfo {};
                    deviceCallbackInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
                    deviceCallbackInfo.callback  = onRequestDevice;
                    deviceCallbackInfo.userdata1 = &deviceResult;
                    deviceCallbackInfo.userdata2 = nullptr;

                    const auto deviceFuture = wgpuAdapterRequestDevice(backend.m_Adapter, &deviceDesc, deviceCallbackInfo);
                    waitForFuture(backend.m_Instance, deviceFuture, [&deviceResult]() { return deviceResult.completed; });
                    if (deviceResult.status != WGPURequestDeviceStatus_Success || !deviceResult.device)
                    {
                        throw std::runtime_error(std::format("Failed to request WebGPU device ({}) {}",
                                                             toWgpuRequestDeviceStatusString(deviceResult.status),
                                                             deviceResult.message));
                    }
                    backend.m_Device = deviceResult.device;
                    backend.m_Queue  = wgpuDeviceGetQueue(backend.m_Device);
                    if (!backend.m_Queue)
                    {
                        throw std::runtime_error("Failed to get WebGPU queue");
                    }

                    WGPUAdapterInfo adapterInfo {};
                    if (wgpuAdapterGetInfo(backend.m_Adapter, &adapterInfo) == WGPUStatus_Success)
                    {
                        auto name = toStdString(adapterInfo.description);
                        if (name.empty())
                        {
                            name = toStdString(adapterInfo.device);
                        }
                        backend.m_FeatureReport.deviceName = std::move(name);
                        wgpuAdapterInfoFreeMembers(adapterInfo);
                    }
                    if (backend.m_FeatureReport.deviceName.empty())
                    {
                        backend.m_FeatureReport.deviceName = "WebGPU Adapter";
                    }

                    backend.m_FeatureReport.apiMajor = 0;
                    backend.m_FeatureReport.apiMinor = 0;
                    backend.m_FeatureReport.apiPatch = 0;
                    return;
                }
            }

            backendOf(m_Backend).m_FeatureFlag = featureFlag;
            backendOf(m_Backend).m_AppName     = appName;
            backendOf(m_Backend).m_RequiredInstanceExtensions.assign(requiredInstanceExtensions.begin(),
                                                requiredInstanceExtensions.end());

            if (HasFlagValues(featureFlag, RenderDeviceFeatureFlagBits::eXR))
            {
                createXRDevice();
            }

            createInstance();
            selectPhysicalDevice();
            findGenericQueue();
            createLogicalDevice();
            createMemoryAllocator();
            createCommandPool();
            createPipelineCache();
            createDefaultDescriptorPool();
            createTracyContext();
            createTracky();
        }

        RenderDevice::~RenderDevice()
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackendOf(m_Backend);
                if (backend.m_Queue)
                {
                    wgpuQueueRelease(backend.m_Queue);
                    backend.m_Queue = nullptr;
                }
                if (backend.m_Device)
                {
                    wgpuDeviceRelease(backend.m_Device);
                    backend.m_Device = nullptr;
                }
                if (backend.m_Adapter)
                {
                    wgpuAdapterRelease(backend.m_Adapter);
                    backend.m_Adapter = nullptr;
                }
                if (backend.m_Instance)
                {
                    wgpuInstanceRelease(backend.m_Instance);
                    backend.m_Instance = nullptr;
                }
                return;
            }

            if (backendOf(m_Backend).m_Device)
            {
                backendOf(m_Backend).m_Device.waitIdle();
            }

            for (auto [_, layout] : backendOf(m_Backend).m_DescriptorSetLayouts)
            {
                backendOf(m_Backend).m_Device.destroyDescriptorSetLayout(layout);
            }

            for (auto [_, layout] : backendOf(m_Backend).m_PipelineLayouts)
            {
                backendOf(m_Backend).m_Device.destroyPipelineLayout(layout);
            }

            for (auto [_, sampler] : backendOf(m_Backend).m_Samplers)
            {
                backendOf(m_Backend).m_Device.destroySampler(vk::Sampler {reinterpret_cast<VkSampler>(sampler.value)});
            }

            TracyGpuDestroy(backendOf(m_Backend).m_TracyContext);
            TRACKY_TEARDOWN();

            if (backendOf(m_Backend).m_MemoryAllocator)
            {
                backendOf(m_Backend).m_MemoryAllocator.destroy();
            }

            if (backendOf(m_Backend).m_Device)
            {
                backendOf(m_Backend).m_Device.destroyDescriptorPool(backendOf(m_Backend).m_DefaultDescriptorPool);
                backendOf(m_Backend).m_Device.destroyPipelineCache(backendOf(m_Backend).m_PipelineCache);
                backendOf(m_Backend).m_Device.destroyCommandPool(backendOf(m_Backend).m_CommandPool);
                backendOf(m_Backend).m_Device.destroy();
            }

            if (backendOf(m_Backend).m_Instance)
            {
#if _DEBUG
                if (backendOf(m_Backend).m_DebugMessenger)
                {
                    backendOf(m_Backend).m_Instance.destroyDebugUtilsMessengerEXT(backendOf(m_Backend).m_DebugMessenger);
                }
#endif
                backendOf(m_Backend).m_Instance.destroy();
            }

            if (backendOf(m_Backend).m_XRDevice)
            {
                delete backendOf(m_Backend).m_XRDevice;
                backendOf(m_Backend).m_XRDevice = nullptr;
            }
        }

        RenderDeviceFeatureFlagBits RenderDevice::getFeatureFlag() const
        {
            return m_Backend->getFeatureFlag();
        }

        RenderDeviceFeatureReport RenderDevice::getFeatureReport() const
        {
            return m_Backend->getFeatureReport();
        }

        RenderDeviceSyncCapabilities RenderDevice::getSyncCapabilities() const
        {
            return m_Backend->getSyncCapabilities();
        }

        openxr::XRDevice* RenderDevice::getXRDevice() const
        {
            return m_Backend->getXRDevice();
        }

        std::string RenderDevice::getName() const
        {
            return m_Backend->getName();
        }

        PhysicalDeviceInfo RenderDevice::getPhysicalDeviceInfo() const
        {
            return m_Backend->getPhysicalDeviceInfo();
        }

        std::array<float, 2> RenderDevice::getLineWidthRange() const
        {
            assert(backendOf(m_Backend).m_PhysicalDevice);
            const auto& limits = backendOf(m_Backend).m_PhysicalDevice.getProperties().limits;
            return {limits.lineWidthRange[0], limits.lineWidthRange[1]};
        }

        float RenderDevice::getMaxSamplerAnisotropy() const
        {
            assert(backendOf(m_Backend).m_PhysicalDevice);
            return backendOf(m_Backend).m_PhysicalDevice.getProperties().limits.maxSamplerAnisotropy;
        }

        uint64_t RenderDevice::getFormatFeatureFlagsOptimal(const PixelFormat pixelFormat) const
        {
            assert(backendOf(m_Backend).m_PhysicalDevice);
            vk::FormatProperties props {};
            backendOf(m_Backend).m_PhysicalDevice.getFormatProperties(toVk(pixelFormat), &props);
            return static_cast<uint64_t>(static_cast<VkFormatFeatureFlags>(props.optimalTilingFeatures));
        }

        Swapchain RenderDevice::createSwapchain(os::Window&             window,
                                                const SwapchainFormat   format,
                                                const VerticalSync      vsync) const
        {
            assert(backendOf(m_Backend).m_Device);

            return Swapchain {
                reinterpret_cast<std::uintptr_t>(static_cast<VkInstance>(backendOf(m_Backend).m_Instance)),
                reinterpret_cast<std::uintptr_t>(static_cast<VkPhysicalDevice>(backendOf(m_Backend).m_PhysicalDevice)),
                reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(backendOf(m_Backend).m_Device)),
                &window,
                format,
                vsync,
            };
        }

        FenceHandle RenderDevice::createFence(const bool signaled) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackendOf(m_Backend);
                const auto handle = backend.m_NextSyncHandle++;
                backend.m_EmulatedFences[handle] = signaled;
                return FenceHandle {handle};
            }

            assert(backendOf(m_Backend).m_Device);
            vk::FenceCreateInfo createInfo {};
            createInfo.flags = signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlags(0u);
            vk::Fence fence {nullptr};
            VK_CHECK(backendOf(m_Backend).m_Device.createFence(&createInfo, nullptr, &fence), LOGTAG, "Failed to create fence");
            return FenceHandle {reinterpret_cast<std::uintptr_t>(static_cast<VkFence>(fence))};
        }

        SemaphoreHandle RenderDevice::createSemaphore()
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackendOf(m_Backend);
                const auto handle = backend.m_NextSyncHandle++;
                backend.m_EmulatedSemaphores.insert(handle);
                return SemaphoreHandle {handle};
            }

            assert(backendOf(m_Backend).m_Device);
            vk::SemaphoreCreateInfo createInfo {};
            createInfo.flags = vk::SemaphoreCreateFlags(0);
            vk::Semaphore semaphore {nullptr};
            VK_CHECK(backendOf(m_Backend).m_Device.createSemaphore(&createInfo, nullptr, &semaphore), LOGTAG, "Failed to create semaphore");
            return SemaphoreHandle {reinterpret_cast<std::uintptr_t>(static_cast<VkSemaphore>(semaphore))};
        }

        Buffer RenderDevice::createStagingBuffer(const uint64_t size, const void* data) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            Buffer stagingBuffer = makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                                              size,
                                              BufferUsage::eTransferSrc,
                                              makeAllocationFlags(AllocationHints::eSequentialWrite),
                                              vma::MemoryUsage::eAutoPreferHost);

            if (data)
            {
                auto* mappedPtr = stagingBuffer.map();
                std::memcpy(mappedPtr, data, size);
                stagingBuffer.unmap();
            }
            return stagingBuffer;
        }

        VertexBuffer RenderDevice::createVertexBuffer(const Buffer::Stride  stride,
                                                      const uint64_t        vertexCount,
                                                      const AllocationHints allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            BufferUsage usage = BufferUsage::eVertexBuffer | BufferUsage::eTransferDst;

            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                usage |= BufferUsage::eShaderDeviceAddress;
            }

            if (isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag))
            {
                usage |= BufferUsage::eAccelerationBuildInput;
            }

            return VertexBuffer {
                makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                           stride * vertexCount,
                           usage,
                           makeAllocationFlags(allocationHint),
                           vma::MemoryUsage::eAutoPreferDevice),
                stride,
            };
        }

        IndexBuffer RenderDevice::createIndexBuffer(IndexType             indexType,
                                                    const uint64_t        indexCount,
                                                    const AllocationHints allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            BufferUsage usage = BufferUsage::eIndexBuffer | BufferUsage::eTransferDst;

            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                usage |= BufferUsage::eShaderDeviceAddress;
            }

            if (isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag))
            {
                usage |= BufferUsage::eAccelerationBuildInput;
            }

            const auto indexStride = indexType == IndexType::eUInt16 ? 2 : 4;

            return IndexBuffer {
                makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                           indexStride * indexCount,
                           usage,
                           makeAllocationFlags(allocationHint),
                           vma::MemoryUsage::eAutoPreferDevice),
                indexType,
            };
        }

        UniformBuffer RenderDevice::createUniformBuffer(const uint64_t        size,
                                                        const AllocationHints allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            return UniformBuffer {
                makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                           size,
                           BufferUsage::eUniformBuffer | BufferUsage::eTransferDst,
                           makeAllocationFlags(allocationHint),
                           vma::MemoryUsage::eAutoPreferDevice),
            };
        }

        StorageBuffer RenderDevice::createStorageBuffer(const uint64_t        size,
                                                        const AllocationHints allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            BufferUsage usage = BufferUsage::eStorageBuffer | BufferUsage::eTransferDst;

            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                usage |= BufferUsage::eShaderDeviceAddress;
            }

            if (isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag))
            {
                usage |= BufferUsage::eAccelerationBuildInput;
            }

            return StorageBuffer {
                makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                           size,
                           usage,
                           makeAllocationFlags(allocationHint),
                           vma::MemoryUsage::eAutoPreferDevice),
            };
        }

        StorageBuffer RenderDevice::createStorageBufferWithUsage(const uint64_t       size,
                                                                 const BufferUsage    extraUsage,
                                                                 const AllocationHints allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            BufferUsage usage = BufferUsage::eStorageBuffer | BufferUsage::eTransferDst | extraUsage;

            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                usage |= BufferUsage::eShaderDeviceAddress;
            }

            if (isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag))
            {
                usage |= BufferUsage::eAccelerationBuildInput;
            }

            return StorageBuffer {
                makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                           size,
                           usage,
                           makeAllocationFlags(allocationHint),
                           vma::MemoryUsage::eAutoPreferDevice),
            };
        }

        DrawIndirectBuffer RenderDevice::createDrawIndirectBufferByCount(const uint32_t         commandCount,
                                                                         const DrawIndirectType type,
                                                                         const AllocationHints  allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            BufferUsage usage = BufferUsage::eStorageBuffer | BufferUsage::eTransferDst | BufferUsage::eIndirectBuffer;

            const auto stride = type == DrawIndirectType::eIndexed ? sizeof(vk::DrawIndexedIndirectCommand) :
                                                                     sizeof(vk::DrawIndirectCommand);

            return DrawIndirectBuffer {makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                                                 commandCount * stride,
                                                 usage,
                                                 makeAllocationFlags(allocationHint),
                                                 vma::MemoryUsage::eCpuToGpu),
                                       type};
        }

        DrawIndirectBuffer RenderDevice::createDrawIndirectBufferBySize(const uint64_t         size,
                                                                        const DrawIndirectType type,
                                                                        const AllocationHints  allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            BufferUsage usage = BufferUsage::eStorageBuffer | BufferUsage::eTransferDst | BufferUsage::eIndirectBuffer;

            return DrawIndirectBuffer {makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                                                 size,
                                                 usage,
                                                 makeAllocationFlags(allocationHint),
                                                 vma::MemoryUsage::eCpuToGpu),
                                       type};
        }

        DescriptorSetLayoutKey
        RenderDevice::createDescriptorSetLayout(const std::vector<DescriptorSetLayoutBindingEx>& bindings)
        {
            assert(backendOf(m_Backend).m_Device);

            std::size_t hash {0};
            for (const auto& b : bindings)
                hashCombine(hash, b);

            if (const auto it = backendOf(m_Backend).m_DescriptorSetLayouts.find(hash); it != backendOf(m_Backend).m_DescriptorSetLayouts.cend())
            {
                return DescriptorSetLayoutKey {hash};
            }

            std::vector<vk::DescriptorSetLayoutBinding> vkBindings;
            vkBindings.reserve(bindings.size());
            for (const auto& b : bindings)
            {
                vkBindings.push_back(vk::DescriptorSetLayoutBinding {
                    b.binding,
                    toVk(b.type),
                    b.count,
                    toVk(b.stageFlags)});
            }

            std::vector<vk::DescriptorBindingFlags> vkFlags;
            vkFlags.reserve(bindings.size());
            for (const auto& b : bindings)
                vkFlags.push_back(static_cast<vk::DescriptorBindingFlags>(b.flags));

            vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo {};
            flagsInfo.bindingCount  = static_cast<uint32_t>(vkFlags.size());
            flagsInfo.pBindingFlags = vkFlags.data();

            vk::DescriptorSetLayoutCreateInfo createInfo {};
            createInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
            createInfo.pBindings    = vkBindings.data();
            createInfo.pNext        = &flagsInfo;
#if __APPLE__
            createInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
#endif

            vk::DescriptorSetLayout descriptorSetLayout {nullptr};
            VK_CHECK(backendOf(m_Backend).m_Device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout),
                     LOGTAG,
                     "Failed to create descriptor set layout");

            const auto& [inserted, _] = backendOf(m_Backend).m_DescriptorSetLayouts.emplace(hash, descriptorSetLayout);
            return DescriptorSetLayoutKey {hash};
        }

        std::uintptr_t RenderDevice::getDescriptorSetLayoutBackendHandle(const DescriptorSetLayoutKey layoutKey) const
        {
            assert(layoutKey);
            if (const auto it = backendOf(m_Backend).m_DescriptorSetLayouts.find(layoutKey.value);
                it != backendOf(m_Backend).m_DescriptorSetLayouts.end())
            {
                return reinterpret_cast<std::uintptr_t>(static_cast<VkDescriptorSetLayout>(it->second));
            }
            return 0;
        }

        PipelineLayout RenderDevice::createPipelineLayout(const PipelineLayoutInfo& layoutInfo)
        {
            assert(backendOf(m_Backend).m_Device);

            std::size_t                           hash {0};
            std::vector<DescriptorSetLayoutKey>   descriptorSetLayoutKeys(kMinNumDescriptorSets);
            std::vector<vk::DescriptorSetLayout>  descriptorSetLayouts(kMinNumDescriptorSets);
            std::vector<vk::PushConstantRange>    vkPushConstantRanges;
            vkPushConstantRanges.reserve(layoutInfo.pushConstantRanges.size());

            for (const auto& [set, bindings] : vultra::enumerate(layoutInfo.descriptorSets))
            {
                for (const auto& binding : bindings)
                {
                    hashCombine(hash, set, binding);
                }
                descriptorSetLayoutKeys[set] = createDescriptorSetLayout(bindings);
                descriptorSetLayouts[set] = vk::DescriptorSetLayout {
                    reinterpret_cast<VkDescriptorSetLayout>(
                        getDescriptorSetLayoutBackendHandle(descriptorSetLayoutKeys[set]))};
            }
            for (const auto& range : layoutInfo.pushConstantRanges)
            {
                hashCombine(hash, range);
                vk::PushConstantRange vkRange {};
                vkRange.offset     = range.offset;
                vkRange.size       = range.size;
                vkRange.stageFlags = toVk(range.stageFlags);
                vkPushConstantRanges.push_back(vkRange);
            }

            if (const auto it = backendOf(m_Backend).m_PipelineLayouts.find(hash); it != backendOf(m_Backend).m_PipelineLayouts.cend())
            {
                return PipelineLayout {reinterpret_cast<std::uintptr_t>(static_cast<VkPipelineLayout>(it->second)),
                                       std::move(descriptorSetLayoutKeys)};
            }

            vk::PipelineLayoutCreateInfo createInfo {};
            createInfo.setLayoutCount         = static_cast<uint32_t>(descriptorSetLayouts.size());
            createInfo.pSetLayouts            = descriptorSetLayouts.data();
            createInfo.pushConstantRangeCount = static_cast<uint32_t>(vkPushConstantRanges.size());
            createInfo.pPushConstantRanges    = vkPushConstantRanges.data();

            vk::PipelineLayout handle {nullptr};
            VK_CHECK(backendOf(m_Backend).m_Device.createPipelineLayout(&createInfo, nullptr, &handle),
                     LOGTAG,
                     "Failed to create pipeline layout");

            const auto& [inserted, _] = backendOf(m_Backend).m_PipelineLayouts.emplace(hash, handle);
            return PipelineLayout {reinterpret_cast<std::uintptr_t>(static_cast<VkPipelineLayout>(inserted->second)),
                                   std::move(descriptorSetLayoutKeys)};
        }

        Texture RenderDevice::createTexture2D(const Extent2D    extent,
                                              const PixelFormat format,
                                              const uint32_t    numMipLevels,
                                              const uint32_t    numLayers,
                                              const ImageUsage  usageFlags) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);
            const auto allocatorHandle =
                reinterpret_cast<std::uintptr_t>(static_cast<VmaAllocator>(backendOf(m_Backend).m_MemoryAllocator));

            return Texture {
                allocatorHandle,
                Texture::CreateInfo {
                    .extent       = extent,
                    .depth        = 0,
                    .pixelFormat  = format,
                    .numMipLevels = numMipLevels,
                    .numLayers    = numLayers,
                    .numFaces     = 1,
                    .usageFlags   = usageFlags,
                },
            };
        }

        Texture RenderDevice::createTexture3D(const Extent2D    extent,
                                              const uint32_t    depth,
                                              const PixelFormat format,
                                              const uint32_t    numMipLevels,
                                              const ImageUsage  usageFlags) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);
            const auto allocatorHandle =
                reinterpret_cast<std::uintptr_t>(static_cast<VmaAllocator>(backendOf(m_Backend).m_MemoryAllocator));

            return Texture {
                allocatorHandle,
                Texture::CreateInfo {
                    .extent       = extent,
                    .depth        = depth,
                    .pixelFormat  = format,
                    .numMipLevels = numMipLevels,
                    .numLayers    = 0,
                    .numFaces     = 1,
                    .usageFlags   = usageFlags,
                },
            };
        }

        Texture RenderDevice::createCubemap(const uint32_t    size,
                                            const PixelFormat format,
                                            const uint32_t    numMipLevels,
                                            const uint32_t    numLayers,
                                            const ImageUsage  usageFlags) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);
            const auto allocatorHandle =
                reinterpret_cast<std::uintptr_t>(static_cast<VmaAllocator>(backendOf(m_Backend).m_MemoryAllocator));

            return Texture {
                allocatorHandle,
                Texture::CreateInfo {
                    .extent       = {size, size},
                    .depth        = 0,
                    .pixelFormat  = format,
                    .numMipLevels = numMipLevels,
                    .numLayers    = numLayers,
                    .numFaces     = 6,
                    .usageFlags   = usageFlags,
                },
            };
        }

        RenderDevice& RenderDevice::setupSampler(Texture& texture, SamplerInfo samplerInfo)
        {
            assert(texture && HasFlagValues(texture.getUsageFlags(), ImageUsage::eSampled));

            if ((getFormatFeatureFlagsOptimal(texture.getPixelFormat()) &
                 static_cast<uint64_t>(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) == 0)
            {
                samplerInfo.minFilter  = TexelFilter::eNearest;
                samplerInfo.magFilter  = TexelFilter::eNearest;
                samplerInfo.mipmapMode = MipmapMode::eNearest;
            }
            texture.setSampler(getSampler(samplerInfo));
            return *this;
        }

        Sampler RenderDevice::getSampler(const SamplerInfo& samplerInfo)
        {
            const auto hash = std::hash<SamplerInfo> {}(samplerInfo);

            auto it = backendOf(m_Backend).m_Samplers.find(hash);
            if (it == backendOf(m_Backend).m_Samplers.cend())
            {
                it = backendOf(m_Backend).m_Samplers.emplace(hash, createSampler(samplerInfo)).first;
                VULTRA_CORE_TRACE("[RenderDevice] Created Sampler {}", hash);
            }

            return Sampler {samplerInfo};
        }

        SamplerHandle RenderDevice::getSamplerHandle(const Sampler& sampler) const
        {
            assert(sampler);
            const auto hash = std::hash<SamplerInfo> {}(sampler.info());

            auto it = backendOf(m_Backend).m_Samplers.find(hash);
            if (it == backendOf(m_Backend).m_Samplers.cend())
            {
                it = backendOf(m_Backend).m_Samplers.emplace(hash, createSampler(sampler.info())).first;
            }

            return it->second;
        }

        ShaderCompiler::Result
        RenderDevice::compile(const ShaderType                                                   shaderType,
                              const std::string_view                                             code,
                              const std::string_view                                             entryPointName,
                              const std::unordered_map<std::string, std::optional<std::string>>& defines) const
        {
            return backendOf(m_Backend).m_ShaderCompiler.compile(shaderType, code, entryPointName, defines);
        }

        ShaderModule
        RenderDevice::createShaderModule(const ShaderType       shaderType,
                                         const std::string_view code,
                                         const std::string_view entryPointName,
                                         const std::unordered_map<std::string, std::optional<std::string>>& defines,
                                         ShaderReflection* reflection) const
        {
            if (auto spv = compile(shaderType, code, entryPointName, defines); spv)
            {
                return createShaderModule(*spv, reflection);
            }
            else
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to compile shader: {}", spv.error());
                return {};
            }
        }

        ShaderModule RenderDevice::createShaderModule(SPIRV spv, ShaderReflection* reflection) const
        {
            assert(backendOf(m_Backend).m_Device != nullptr);
            if (reflection)
            {
                auto rr = vshadersystem::reflect_spirv(spv);
                if (rr.isOk())
                {
                    reflection->accumulate(rr.value());
                }
                else
                {
                    VULTRA_CORE_ERROR("[RenderDevice] Failed to reflect SPIR-V: {}", rr.error().message);
                }
            }
            return ShaderModule {std::move(spv)};
        }

        ComputePipeline RenderDevice::createComputePipeline(const ShaderStageInfo&        shaderStageInfo,
                                                            std::optional<PipelineLayout> pipelineLayout)
        {
            auto reflection = pipelineLayout ? std::nullopt : std::make_optional<ShaderReflection>();

            const auto shaderModule = createShaderModule(ShaderType::eCompute,
                                                         shaderStageInfo.code,
                                                         shaderStageInfo.entryPointName,
                                                         shaderStageInfo.defines,
                                                         reflection ? std::addressof(reflection.value()) : nullptr);
            if (!shaderModule)
                return {};

            if (reflection)
                pipelineLayout = reflectPipelineLayout(*this, *reflection);
            assert(*pipelineLayout);

            const auto shaderModuleHandle = createVulkanShaderModule(backendOf(m_Backend).m_Device, shaderModule.getSpirv());

            vk::ComputePipelineCreateInfo createInfo {};
            createInfo.stage  = vk::PipelineShaderStageCreateInfo {{}, vk::ShaderStageFlagBits::eCompute, shaderModuleHandle, "main"};
            createInfo.layout = vk::PipelineLayout {reinterpret_cast<VkPipelineLayout>(pipelineLayout->getHandle())};

            auto [result, computePipeline] = backendOf(m_Backend).m_Device.createComputePipeline(backendOf(m_Backend).m_PipelineCache, createInfo, nullptr);
            backendOf(m_Backend).m_Device.destroyShaderModule(shaderModuleHandle);
            if (result != vk::Result::eSuccess)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to create compute pipeline: {}", vk::to_string(result));
                throw std::runtime_error("Failed to create compute pipeline");
            }

            return ComputePipeline {
                std::move(pipelineLayout.value()),
                reflection ? reflection->localSize.value() : glm::uvec3 {},
                reinterpret_cast<std::uintptr_t>(static_cast<VkPipeline>(computePipeline)),
                std::make_unique<VulkanPipelineBackend>(
                    reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(backendOf(m_Backend).m_Device))),
                std::make_unique<VulkanComputePipelineBackend>(
                    reinterpret_cast<std::uintptr_t>(static_cast<VkPipeline>(computePipeline)),
                    reflection ? reflection->localSize.value() : glm::uvec3 {}),
            };
        }

        ComputePipeline RenderDevice::createComputePipelineBuiltin(const SPIRV&                  spv,
                                                                   std::optional<PipelineLayout> pipelineLayout)
        {
            auto reflection = pipelineLayout ? std::nullopt : std::make_optional<ShaderReflection>();

            const auto shaderModule =
                createShaderModule(spv, reflection ? std::addressof(reflection.value()) : nullptr);
            if (!shaderModule)
                return {};

            if (reflection)
                pipelineLayout = reflectPipelineLayout(*this, *reflection);
            assert(*pipelineLayout);

            vk::ComputePipelineCreateInfo createInfo {};
            const auto shaderModuleHandle = createVulkanShaderModule(backendOf(m_Backend).m_Device, shaderModule.getSpirv());
            createInfo.stage              = vk::PipelineShaderStageCreateInfo {
                                 {}, vk::ShaderStageFlagBits::eCompute, shaderModuleHandle, "main"};
            createInfo.layout = vk::PipelineLayout {reinterpret_cast<VkPipelineLayout>(pipelineLayout->getHandle())};

            auto [result, computePipeline] = backendOf(m_Backend).m_Device.createComputePipeline(backendOf(m_Backend).m_PipelineCache, createInfo, nullptr);
            backendOf(m_Backend).m_Device.destroyShaderModule(shaderModuleHandle);
            if (result != vk::Result::eSuccess)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to create compute pipeline: {}", vk::to_string(result));
                throw std::runtime_error("Failed to create compute pipeline");
            }

            return ComputePipeline {
                std::move(pipelineLayout.value()),
                reflection ? reflection->localSize.value() : glm::uvec3 {},
                reinterpret_cast<std::uintptr_t>(static_cast<VkPipeline>(computePipeline)),
                std::make_unique<VulkanPipelineBackend>(
                    reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(backendOf(m_Backend).m_Device))),
                std::make_unique<VulkanComputePipelineBackend>(
                    reinterpret_cast<std::uintptr_t>(static_cast<VkPipeline>(computePipeline)),
                    reflection ? reflection->localSize.value() : glm::uvec3 {}),
            };
        }

        RadixSorter RenderDevice::createRadixSorter(const uint32_t maxElementCount)
        {
            return RadixSorter::create(std::make_unique<VulkanRadixSorter>(*this, maxElementCount));
        }

        RenderDevice& RenderDevice::upload(Buffer& buffer, const uint64_t offset, const uint64_t size, const void* data)
        {
            assert(buffer && data);
            assert(backendOf(m_Backend).m_Device);

            auto* mappedMemory = std::bit_cast<std::byte*>(buffer.map());
            std::memcpy(mappedMemory + offset, data, size);
            buffer.flush().unmap();
            return *this;
        }

        RenderDevice& RenderDevice::uploadS(Buffer& buffer, const uint64_t offset, const uint64_t size, const void* data)
        {
            assert(buffer && data);
            assert(backendOf(m_Backend).m_Device);

            // NOTE: Synchronous upload path. This submits immediately and waits for completion.
            // Keep this for initialization/setup/debug utilities only.
            // Do not call from framegraph/pass execution paths.

            auto stagingBuffer = createStagingBuffer(size, data);

            return execute(
                [&](CommandBuffer& cb) { cb.copyBuffer(stagingBuffer, buffer, rhi::BufferCopy {0, offset, size}); },
                true);
        }

        RenderDevice& RenderDevice::uploadDrawIndirect(DrawIndirectBuffer&                     buffer,
                                                       const std::vector<DrawIndirectCommand>& commands)
        {
            if (commands.empty())
            {
                return *this;
            }

            assert(buffer);
            assert(backendOf(m_Backend).m_Device);

            const uint32_t maxCommands = buffer.getSize();

            assert(commands.size() <= maxCommands);

            std::byte* dst = static_cast<std::byte*>(buffer.map());

            const auto type   = buffer.getDrawIndirectType();
            const auto stride = buffer.getStride();

            for (uint32_t i = 0; i < commands.size(); ++i)
            {
                const DrawIndirectCommand& cmd = commands[i];
                std::byte*                 ptr = dst + i * stride;

                if (type == DrawIndirectType::eIndexed)
                {
                    vk::DrawIndexedIndirectCommand vkCmd {};
                    vkCmd.indexCount    = cmd.count;
                    vkCmd.instanceCount = cmd.instanceCount;
                    vkCmd.firstIndex    = cmd.first;
                    vkCmd.vertexOffset  = cmd.vertexOffset;
                    vkCmd.firstInstance = cmd.firstInstance;

                    std::memcpy(ptr, &vkCmd, sizeof(vkCmd));
                }
                else
                {
                    vk::DrawIndirectCommand vkCmd {};
                    vkCmd.vertexCount   = cmd.count;
                    vkCmd.instanceCount = cmd.instanceCount;
                    vkCmd.firstVertex   = cmd.first;
                    vkCmd.firstInstance = cmd.firstInstance;

                    std::memcpy(ptr, &vkCmd, sizeof(vkCmd));
                }
            }

            buffer.flush().unmap();

            return *this;
        }

        RenderDevice& RenderDevice::destroy(FenceHandle& fence)
        {
            assert(static_cast<bool>(fence));

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackendOf(m_Backend);
                backend.m_EmulatedFences.erase(fence.value);
                fence = {};
                return *this;
            }

            assert(backendOf(m_Backend).m_Device);
            backendOf(m_Backend).m_Device.destroyFence(vk::Fence {reinterpret_cast<VkFence>(fence.value)});
            fence = {};
            return *this;
        }

        RenderDevice& RenderDevice::destroy(SemaphoreHandle& semaphore)
        {
            assert(static_cast<bool>(semaphore));

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackendOf(m_Backend);
                backend.m_EmulatedSemaphores.erase(semaphore.value);
                semaphore = {};
                return *this;
            }

            assert(backendOf(m_Backend).m_Device);
            backendOf(m_Backend).m_Device.destroySemaphore(vk::Semaphore {reinterpret_cast<VkSemaphore>(semaphore.value)});
            semaphore = {};
            return *this;
        }

        void RenderDevice::createXRDevice()
        {
            assert(HasFlagValues(backendOf(m_Backend).m_FeatureFlag, RenderDeviceFeatureFlagBits::eXR));

            try
            {
                backendOf(m_Backend).m_XRDevice = new openxr::XRDevice(openxr::XRDeviceFeatureFlagBits::eVR, backendOf(m_Backend).m_AppName);
            }
            catch (const std::exception& e)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to initialize OpenXR device: {}", e.what());
                backendOf(m_Backend).m_XRDevice    = nullptr;
                backendOf(m_Backend).m_FeatureFlag = backendOf(m_Backend).m_FeatureFlag & ~RenderDeviceFeatureFlagBits::eXR;
            }
        }

        void RenderDevice::createInstance()
        {
            // Setup dynamic loader
#if VK_VERSION_1_4
            static const vk::detail::DynamicLoader GlobalDynamicLoader;
#else
            static const vk::DynamicLoader GlobalDynamicLoader;
#endif
            auto vkGetInstanceProcAddr =
                GlobalDynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
            VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

            vk::ApplicationInfo appInfo {};
            appInfo.pApplicationName   = backendOf(m_Backend).m_AppName.c_str();
            appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.pEngineName        = "Vultra";
            appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
            appInfo.apiVersion         = vk::ApiVersion13;

            vk::InstanceCreateInfo createInfo {};
            createInfo.pApplicationInfo = &appInfo;

#ifdef __APPLE__
            createInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

            std::vector<const char*> requiredExtensions;
            std::vector<const char*> extensions;
            bool                     enableDebugUtils = false;

            requiredExtensions.assign(backendOf(m_Backend).m_RequiredInstanceExtensions.begin(), backendOf(m_Backend).m_RequiredInstanceExtensions.end());

#if _DEBUG
            requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

#ifdef __APPLE__
            requiredExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            requiredExtensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
#endif

            std::unordered_map<const char*, bool> extensionMap;
            auto                                  availableExtensions = vk::enumerateInstanceExtensionProperties();
            for (const auto& requiredExtension : requiredExtensions)
            {
                extensionMap.insert({requiredExtension, false});

                for (const auto& availableExtension : availableExtensions)
                {
                    if (std::strcmp(availableExtension.extensionName, requiredExtension) == 0)
                    {
                        extensions.push_back(requiredExtension);
                        extensionMap[requiredExtension] = true;
#if _DEBUG
                        if (std::strcmp(requiredExtension, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
                        {
                            enableDebugUtils = true;
                        }
#endif
                    }
                }
            }

            for (const auto& [extension, found] : extensionMap)
            {
                if (!found)
                {
#if _DEBUG
                    if (std::strcmp(extension, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
                    {
                        VULTRA_CORE_WARN("[RenderDevice] Optional extension unavailable: {}", extension);
                        continue;
                    }
#endif
                    VULTRA_CORE_ERROR("[RenderDevice] Cannot find required extension: {}", extension);
                    throw std::runtime_error("Missing required extension");
                }
            }

            std::vector<const char*> enabledLayers;
            std::set<const char*>    layerSet;
            bool                     found  = false;
            auto                     layers = vk::enumerateInstanceLayerProperties();
            for (auto& layer : layers)
            {
                VULTRA_CORE_TRACE("[RenderDevice] Found layer: {} \"{}\" {}-{}",
                                  layer.layerName.data(),
                                  layer.description.data(),
                                  layer.implementationVersion,
                                  layer.specVersion);

                for (const auto& requestLayer : requestLayers)
                {
                    if (strcmp(requestLayer, layer.layerName) == 0)
                    {
                        if (layerSet.count(requestLayer) == 0)
                        {
                            enabledLayers.push_back(layer.layerName);
                            VULTRA_CORE_TRACE("[RenderDevice] Enabling layer: {} \"{}\" {}-{}",
                                              layer.layerName.data(),
                                              layer.description.data(),
                                              layer.implementationVersion,
                                              layer.specVersion);
                            layerSet.insert(requestLayer);
                        }
                    }
                }
#if _DEBUG
                if (strcmp(validationLayers[0], layer.layerName) == 0)
                {
                    if (!found)
                    {
                        found = true;
                        enabledLayers.push_back(layer.layerName);
                    }
                }
#endif
            }

#if _DEBUG
            if (!found)
            {
                VULTRA_CORE_WARN("[RenderDevice] Validation layer unavailable, continuing without it");
            }
#endif

            createInfo.enabledLayerCount   = static_cast<uint32_t>(enabledLayers.size());
            createInfo.ppEnabledLayerNames = enabledLayers.data();
            createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef __APPLE__
            // If layer settings are defined, then activate the sample's required layer settings during instance
            // creation.
            // Layer settings are typically used to activate specific features of a layer, such as the Validation
            // Layer's printf feature, or to configure specific capabilities of drivers such as MoltenVK on macOS and/or
            // iOS.
            std::vector<vk::LayerSettingEXT> enabledLayerSettings;

            // Configure MoltenVK to use Metal argument buffers (needed for descriptor indexing)
            vk::LayerSettingEXT layerSetting;
            layerSetting.pLayerName   = "MoltenVK";
            layerSetting.pSettingName = "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS";
            layerSetting.type         = vk::LayerSettingTypeEXT::eBool32;
            layerSetting.valueCount   = 1;

            // Make this static so layer setting reference remains valid after leaving constructor scope
            static const vk::Bool32 layerSettingOn = VK_TRUE;
            layerSetting.pValues                   = &layerSettingOn;
            enabledLayerSettings.push_back(layerSetting);

            vk::LayerSettingsCreateInfoEXT layerSettingsCreateInfo {};
            if (enabledLayerSettings.size() > 0)
            {
                layerSettingsCreateInfo.settingCount = static_cast<uint32_t>(enabledLayerSettings.size());
                layerSettingsCreateInfo.pSettings    = enabledLayerSettings.data();
                layerSettingsCreateInfo.pNext        = createInfo.pNext;
                createInfo.pNext                     = &layerSettingsCreateInfo;
            }
#endif

            // If enable OpenXR feature, then let OpenXR create the vulkan instance.
            if (HasFlagValues(backendOf(m_Backend).m_FeatureFlag, RenderDeviceFeatureFlagBits::eXR))
            {
                VkInstance           vkInstanceC;
                VkInstanceCreateInfo createInfoC(createInfo);

                XrVulkanInstanceCreateInfoKHR xrVulkanInstanceCreateInfo {};
                xrVulkanInstanceCreateInfo.type                   = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
                xrVulkanInstanceCreateInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
                xrVulkanInstanceCreateInfo.systemId               = backendOf(m_Backend).m_XRDevice->m_XrSystemId;
                xrVulkanInstanceCreateInfo.vulkanCreateInfo       = &createInfoC;
                VkResult vkResult;

                bool ok = true;
                if (XR_FAILED(backendOf(m_Backend).m_XRDevice->xrCreateVulkanInstanceKHR(
                        backendOf(m_Backend).m_XRDevice->m_XrInstance, &xrVulkanInstanceCreateInfo, &vkInstanceC, &vkResult)))
                {
                    ok = false;
                }
                if (vkResult != VK_SUCCESS)
                {
                    ok = false;
                }

                if (!ok)
                {
                    VULTRA_CORE_ERROR("[RenderDevice] Failed to create Instance with OpenXR");
                    throw std::runtime_error("Failed to create Instance with OpenXR");
                }

                backendOf(m_Backend).m_Instance = vkInstanceC;
            }
            else
            {
                VK_CHECK(
                    vk::createInstance(&createInfo, nullptr, &backendOf(m_Backend).m_Instance), LOGTAG, "Failed to create Vulkan instance");
            }

            VULKAN_HPP_DEFAULT_DISPATCHER.init(backendOf(m_Backend).m_Instance);

#if _DEBUG
            if (enableDebugUtils)
            {
                setupDebugMessenger(backendOf(m_Backend).m_Instance, backendOf(m_Backend).m_DebugMessenger);
            }
#endif
        }

        void RenderDevice::selectPhysicalDevice()
        {
            // If OpenXR is enabled, then retrieve the physical device from OpenXR.
            bool useOpenXR = backendOf(m_Backend).m_XRDevice != nullptr && backendOf(m_Backend).m_XRDevice->m_XrInstance != XR_NULL_HANDLE;

            if (useOpenXR)
            {
                VULTRA_CORE_INFO("[RenderDevice] Selecting physical device from OpenXR");
                backendOf(m_Backend).m_FeatureReport.flags |= RenderDeviceFeatureReportFlagBits::eXR;

                // Retrieve the physical device from OpenXR
                VkPhysicalDevice physicalDevice = nullptr;

                XrVulkanGraphicsDeviceGetInfoKHR vulkanGraphicsDeviceGetInfo {};
                vulkanGraphicsDeviceGetInfo.type           = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
                vulkanGraphicsDeviceGetInfo.systemId       = backendOf(m_Backend).m_XRDevice->m_XrSystemId;
                vulkanGraphicsDeviceGetInfo.vulkanInstance = backendOf(m_Backend).m_Instance;

                if (XR_FAILED(backendOf(m_Backend).m_XRDevice->xrGetVulkanGraphicsDevice2KHR(
                        backendOf(m_Backend).m_XRDevice->m_XrInstance, &vulkanGraphicsDeviceGetInfo, &physicalDevice)))
                {
                    VULTRA_CORE_ERROR("[RenderDevice] Failed to get Vulkan graphics device from OpenXR");
                    throw std::runtime_error("Failed to get Vulkan graphics device from OpenXR");
                }
                backendOf(m_Backend).m_PhysicalDevice = physicalDevice;
            }
            // If OpenXR is not enabled, then select a physical device from the Vulkan instance.
            else
            {
                VULTRA_CORE_INFO("[RenderDevice] Selecting physical device");
                auto               physicalDevices = backendOf(m_Backend).m_Instance.enumeratePhysicalDevices();
                vk::PhysicalDevice bestDevice      = nullptr;
                int                bestScore       = 0;

                for (const auto& device : physicalDevices)
                {
                    vk::PhysicalDeviceProperties properties = device.getProperties();

                    int score = 0;
                    if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
                    {
                        score += 1000;
                    }

                    score += properties.limits.maxImageDimension2D;

                    if (score > bestScore)
                    {
                        bestDevice = device;
                        bestScore  = score;
                    }
                }

                if (!bestDevice)
                {
                    VULTRA_CORE_ERROR("[RenderDevice] Failed to find a suitable GPU!");
                    throw std::runtime_error("Failed to find a suitable GPU");
                }

                backendOf(m_Backend).m_PhysicalDevice = bestDevice;
            }

            // Query supported extensions
            auto extensions = backendOf(m_Backend).m_PhysicalDevice.enumerateDeviceExtensionProperties();
            for (auto& ext : extensions)
            {
                backendOf(m_Backend).m_SupportedExtensions.insert(ext.extensionName);
            }

            const bool supportsAccelerationStructure =
                backendOf(m_Backend).m_SupportedExtensions.count(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) > 0;
            const bool supportsRayTracingPipeline =
                backendOf(m_Backend).m_SupportedExtensions.count(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) > 0;
            const bool supportsRayQuery   = backendOf(m_Backend).m_SupportedExtensions.count(VK_KHR_RAY_QUERY_EXTENSION_NAME) > 0;
            const bool supportsMeshShader = backendOf(m_Backend).m_SupportedExtensions.count(VK_EXT_MESH_SHADER_EXTENSION_NAME) > 0;
            const bool supportsMultiDraw  = backendOf(m_Backend).m_SupportedExtensions.count(VK_EXT_MULTI_DRAW_EXTENSION_NAME) > 0;
            const bool supportsFragmentShaderInterlock =
                backendOf(m_Backend).m_SupportedExtensions.count(VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME) > 0;

            // Query properties with a conservative pNext chain based on advertised support.
            vk::PhysicalDeviceProperties2                     properties2 {};
            vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties {};
            rayTracingPipelineProperties.sType = vk::StructureType::ePhysicalDeviceRayTracingPipelinePropertiesKHR;

            if (supportsRayTracingPipeline)
            {
                properties2.pNext = &rayTracingPipelineProperties;
            }

            backendOf(m_Backend).m_PhysicalDevice.getProperties2(&properties2);
            if (supportsRayTracingPipeline)
            {
                backendOf(m_Backend).m_RayTracingPipelineProperties = rayTracingPipelineProperties;
            }
            else
            {
                backendOf(m_Backend).m_RayTracingPipelineProperties = vk::PhysicalDeviceRayTracingPipelinePropertiesKHR {};
            }

            // Query supported features
            vk::PhysicalDeviceFeatures2        features2 {};
            vk::PhysicalDeviceVulkan11Features vk11 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
            vk::PhysicalDeviceVulkan12Features vk12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            vk::PhysicalDeviceVulkan13Features vk13 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            vk::PhysicalDeviceAccelerationStructureFeaturesKHR accel {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
            vk::PhysicalDeviceRayQueryFeaturesKHR rayQuery {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
            vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracing {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
            vk::PhysicalDeviceMeshShaderFeaturesEXT mesh {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
            vk::PhysicalDeviceMultiDrawFeaturesEXT  multidraw {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT};
            vk::PhysicalDeviceFragmentShaderInterlockFeaturesEXT fragmentShaderInterlock {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};

            vk::BaseOutStructure* featureChainTail   = nullptr;
            auto                  appendFeatureChain = [&](auto& featureStruct) {
                if (featureChainTail != nullptr)
                {
                    featureChainTail->pNext = reinterpret_cast<vk::BaseOutStructure*>(&featureStruct);
                }
                else
                {
                    features2.pNext = &featureStruct;
                }
                featureChainTail = reinterpret_cast<vk::BaseOutStructure*>(&featureStruct);
            };

            appendFeatureChain(vk13);
            appendFeatureChain(vk12);
            appendFeatureChain(vk11);
            if (supportsAccelerationStructure)
            {
                appendFeatureChain(accel);
            }
            if (supportsRayQuery)
            {
                appendFeatureChain(rayQuery);
            }
            if (supportsRayTracingPipeline)
            {
                appendFeatureChain(rayTracing);
            }
            if (supportsMeshShader)
            {
                appendFeatureChain(mesh);
            }
            if (supportsMultiDraw)
            {
                appendFeatureChain(multidraw);
            }
            if (supportsFragmentShaderInterlock)
            {
                appendFeatureChain(fragmentShaderInterlock);
            }
            backendOf(m_Backend).m_PhysicalDevice.getFeatures2(&features2);
            if (supportsAccelerationStructure)
            {
                backendOf(m_Backend).m_AccelerationStructureFeatures = accel;
            }
            else
            {
                backendOf(m_Backend).m_AccelerationStructureFeatures = vk::PhysicalDeviceAccelerationStructureFeaturesKHR {};
            }
            // Fill feature report
            auto       props = backendOf(m_Backend).m_PhysicalDevice.getProperties();
            const bool supportsDynamicRendering =
                vk13.dynamicRendering == VK_TRUE ||
                backendOf(m_Backend).m_SupportedExtensions.count(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) > 0;
            const bool supportsSynchronization2 =
                vk13.synchronization2 == VK_TRUE ||
                backendOf(m_Backend).m_SupportedExtensions.count(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) > 0;
            const bool useVulkan13CoreFeatures =
                VK_API_VERSION_MAJOR(props.apiVersion) > 1 ||
                (VK_API_VERSION_MAJOR(props.apiVersion) == 1 && VK_API_VERSION_MINOR(props.apiVersion) >= 3);
            backendOf(m_Backend).m_FeatureReport.deviceName = props.deviceName.data();
            backendOf(m_Backend).m_FeatureReport.apiMajor   = VK_API_VERSION_MAJOR(props.apiVersion);
            backendOf(m_Backend).m_FeatureReport.apiMinor   = VK_API_VERSION_MINOR(props.apiVersion);
            backendOf(m_Backend).m_FeatureReport.apiPatch   = VK_API_VERSION_PATCH(props.apiVersion);
            backendOf(m_Backend).m_UseKhrDynamicRendering   = supportsDynamicRendering && !useVulkan13CoreFeatures &&
                                       backendOf(m_Backend).m_SupportedExtensions.count(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) > 0;
            backendOf(m_Backend).m_UseKhrSynchronization2 = supportsSynchronization2 && !useVulkan13CoreFeatures &&
                                       backendOf(m_Backend).m_SupportedExtensions.count(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) > 0;

            auto& flags = backendOf(m_Backend).m_FeatureReport.flags;

            auto add = [&](RenderDeviceFeatureReportFlagBits bit, const char* ext, bool featureSupported = true) {
                if (backendOf(m_Backend).m_SupportedExtensions.count(ext) && featureSupported)
                    flags |= bit;
                else
                    VULTRA_CORE_WARN("[RenderDevice] Extension or feature not supported: {}", ext);
            };

            add(RenderDeviceFeatureReportFlagBits::eRayTracingPipeline,
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                rayTracing.rayTracingPipeline);
            add(RenderDeviceFeatureReportFlagBits::eRayQuery, VK_KHR_RAY_QUERY_EXTENSION_NAME, rayQuery.rayQuery);
            add(RenderDeviceFeatureReportFlagBits::eAccelerationStructure,
                VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                accel.accelerationStructure);
            add(RenderDeviceFeatureReportFlagBits::eMeshShader, VK_EXT_MESH_SHADER_EXTENSION_NAME, mesh.meshShader);
            add(RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress,
                VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                vk12.bufferDeviceAddress && vk12.bufferDeviceAddressCaptureReplay && vk12.scalarBlockLayout &&
                    vk12.storageBuffer8BitAccess);
            add(RenderDeviceFeatureReportFlagBits::eDescriptorIndexing,
                VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                vk12.descriptorIndexing && vk12.shaderSampledImageArrayNonUniformIndexing &&
                    vk12.runtimeDescriptorArray && vk12.descriptorBindingPartiallyBound &&
                    vk12.descriptorBindingVariableDescriptorCount && vk12.descriptorBindingUpdateUnusedWhilePending);
            add(RenderDeviceFeatureReportFlagBits::eDrawIndirectCount,
                VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME,
                vk12.drawIndirectCount);
            add(RenderDeviceFeatureReportFlagBits::eMultiDraw, VK_EXT_MULTI_DRAW_EXTENSION_NAME, multidraw.multiDraw);
            add(RenderDeviceFeatureReportFlagBits::eDrawParameters,
                VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
                vk11.shaderDrawParameters);
            if (vk11.multiview)
                flags |= RenderDeviceFeatureReportFlagBits::eMultiview;
            else
                VULTRA_CORE_WARN("[RenderDevice] Extension or feature not supported: multiview");
            add(RenderDeviceFeatureReportFlagBits::eFragmentShaderInterlock,
                VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME,
                fragmentShaderInterlock.fragmentShaderPixelInterlock);
            if (supportsDynamicRendering)
            {
                flags |= RenderDeviceFeatureReportFlagBits::eDynamicRendering;
            }
            else
            {
                VULTRA_CORE_WARN("[RenderDevice] Extension or feature not supported: dynamic rendering");
            }
            if (supportsSynchronization2)
            {
                flags |= RenderDeviceFeatureReportFlagBits::eSynchronization2;
            }
            else
            {
                VULTRA_CORE_WARN("[RenderDevice] Extension or feature not supported: synchronization2");
            }

            // Summarize selected device
            VULTRA_CORE_INFO("[RenderDevice] Selected GPU: {}", props.deviceName.data());
            VULTRA_CORE_INFO(
                "  Vulkan API: {}.{}.{}", backendOf(m_Backend).m_FeatureReport.apiMajor, backendOf(m_Backend).m_FeatureReport.apiMinor, backendOf(m_Backend).m_FeatureReport.apiPatch);
            VULTRA_CORE_INFO("[RenderDevice] Supported features:");

#define PRINT_FEATURE(f) \
    VULTRA_CORE_INFO("   {:<30} {}", \
                     std::string_view(#f).substr(1), \
                     HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::f) ? "yes" : "no")
            VULTRA_CORE_INFO("[RenderDevice] Feature support report:");
            PRINT_FEATURE(eXR);
            PRINT_FEATURE(eRayTracingPipeline);
            PRINT_FEATURE(eRayQuery);
            PRINT_FEATURE(eAccelerationStructure);
            PRINT_FEATURE(eMeshShader);
            PRINT_FEATURE(eBufferDeviceAddress);
            PRINT_FEATURE(eDescriptorIndexing);
            PRINT_FEATURE(eDrawIndirectCount);
            PRINT_FEATURE(eMultiDraw);
            PRINT_FEATURE(eDrawParameters);
            PRINT_FEATURE(eMultiview);
            PRINT_FEATURE(eFragmentShaderInterlock);
            PRINT_FEATURE(eDynamicRendering);
            PRINT_FEATURE(eSynchronization2);
#undef PRINT_FEATURE

            // === Assign & Check Feature Flags ===
            auto availableFeatureFlag = RenderDeviceFeatureFlagBits::eNormal;
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eAccelerationStructure) &&
                HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eRayTracingPipeline))
            {
                availableFeatureFlag |= RenderDeviceFeatureFlagBits::eRayTracingPipeline;
            }
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eAccelerationStructure) &&
                HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eRayQuery))
            {
                availableFeatureFlag |= RenderDeviceFeatureFlagBits::eRayQuery;
            }
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eMeshShader))
            {
                availableFeatureFlag |= RenderDeviceFeatureFlagBits::eMeshShader;
            }
            if (useOpenXR)
            {
                availableFeatureFlag |= RenderDeviceFeatureFlagBits::eXR;
            }

            if (!HasFlagValues(availableFeatureFlag, backendOf(m_Backend).m_FeatureFlag) &&
                backendOf(m_Backend).m_FeatureFlag != RenderDeviceFeatureFlagBits::eNormal)
            {
                throw std::runtime_error("Requested features are not supported by the physical device");
            }
        }

        void RenderDevice::findGenericQueue()
        {
            constexpr vk::QueueFlags requiredQueueFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;

            uint32_t count = 0;
            backendOf(m_Backend).m_PhysicalDevice.getQueueFamilyProperties(&count, nullptr);
            assert(count > 0);
            std::vector<vk::QueueFamilyProperties> queueFamilies(count);
            backendOf(m_Backend).m_PhysicalDevice.getQueueFamilyProperties(&count, queueFamilies.data());
            for (uint32_t i = 0; i < count; ++i)
            {
                if ((queueFamilies[i].queueFlags & requiredQueueFlags) == requiredQueueFlags)
                {
                    backendOf(m_Backend).m_GenericQueueFamilyIndex = i;
                    VULTRA_CORE_INFO("[RenderDevice] Selected generic queue family {} with flags: {}",
                                     i,
                                     vk::to_string(queueFamilies[i].queueFlags));
                    break;
                }
            }

            if (backendOf(m_Backend).m_GenericQueueFamilyIndex == -1)
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    VULTRA_CORE_WARN(
                        "[RenderDevice] Queue family {} flags: {}", i, vk::to_string(queueFamilies[i].queueFlags));
                }
                VULTRA_CORE_ERROR("[RenderDevice] Failed to find a queue family supporting both graphics and compute!");
                throw std::runtime_error("Failed to find a valid queue family index");
            }
        }

        void RenderDevice::createLogicalDevice()
        {
            constexpr float           queuePriority = 1.0f;
            vk::DeviceQueueCreateInfo queueCreateInfo {};
            queueCreateInfo.queueFamilyIndex = backendOf(m_Backend).m_GenericQueueFamilyIndex;
            queueCreateInfo.queueCount       = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;

            const auto physicalDeviceFeatures  = backendOf(m_Backend).m_PhysicalDevice.getFeatures();
            const bool useVulkan13CoreFeatures = !backendOf(m_Backend).m_UseKhrDynamicRendering && !backendOf(m_Backend).m_UseKhrSynchronization2;
            // === Base extensions ===
            std::vector<const char*> extensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            };

            if (!useVulkan13CoreFeatures &&
                HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eSynchronization2))
            {
                extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
            }
            if (!useVulkan13CoreFeatures &&
                HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eDynamicRendering))
            {
                extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            }

            // NVIDIA's vk_gaussian_splatting sorter backend (vrdx) uses push descriptors.
            extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);

            // === Feature structs ===
            vk::PhysicalDeviceFeatures2        deviceFeatures2 {};
            std::vector<vk::BaseOutStructure*> featureChain;

            vk::PhysicalDeviceFeatures enabledFeatures {};
            enabledFeatures.samplerAnisotropy = physicalDeviceFeatures.samplerAnisotropy;
#ifndef __APPLE__
            enabledFeatures.geometryShader            = physicalDeviceFeatures.geometryShader;
            enabledFeatures.shaderImageGatherExtended = physicalDeviceFeatures.shaderImageGatherExtended;
            enabledFeatures.shaderInt64               = physicalDeviceFeatures.shaderInt64;
#endif
            deviceFeatures2.features = enabledFeatures;

#ifdef __APPLE__
            extensions.push_back("VK_KHR_portability_subset");
#endif

            vk::PhysicalDeviceVulkan13Features            vk13Features {};
            vk::PhysicalDeviceDynamicRenderingFeatures    vkDynamicRenderingFeatures {};
            vk::PhysicalDeviceSynchronization2FeaturesKHR vkSync2Features {};
            if (useVulkan13CoreFeatures)
            {
                if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eDynamicRendering))
                {
                    vk13Features.dynamicRendering = VK_TRUE;
                }
                if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eSynchronization2))
                {
                    vk13Features.synchronization2 = VK_TRUE;
                }
                if (vk13Features.dynamicRendering == VK_TRUE || vk13Features.synchronization2 == VK_TRUE)
                {
                    featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vk13Features));
                }
            }
            else
            {
                if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eDynamicRendering))
                {
                    vkDynamicRenderingFeatures.dynamicRendering = VK_TRUE;
                    featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vkDynamicRenderingFeatures));
                }
                if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eSynchronization2))
                {
                    vkSync2Features.synchronization2 = VK_TRUE;
                    featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vkSync2Features));
                }
            }
            // Vulkan 1.1 features
            vk::PhysicalDeviceVulkan11Features vk11Features {};
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eDrawParameters))
            {
                vk11Features.shaderDrawParameters = VK_TRUE;
            }
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eMultiview))
            {
                vk11Features.multiview = VK_TRUE;
            }
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vk11Features));

            // Vulkan 1.2 features
            vk::PhysicalDeviceVulkan12Features vk12Features {};
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                vk12Features.bufferDeviceAddress = VK_TRUE;
#ifdef VULTRA_ENABLE_RENDERDOC
                vk12Features.bufferDeviceAddressCaptureReplay = VK_TRUE;
#endif

                vk12Features.scalarBlockLayout       = VK_TRUE;
                vk12Features.storageBuffer8BitAccess = VK_TRUE;
            }
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eDescriptorIndexing))
            {
                vk12Features.descriptorIndexing                        = VK_TRUE;
                vk12Features.descriptorBindingVariableDescriptorCount  = VK_TRUE;
                vk12Features.descriptorBindingPartiallyBound           = VK_TRUE;
                vk12Features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
                vk12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
                vk12Features.runtimeDescriptorArray                    = VK_TRUE;
            }
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eDrawIndirectCount))
            {
                vk12Features.drawIndirectCount = VK_TRUE;
            }
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vk12Features));

            // Multi-draw
            vk::PhysicalDeviceMultiDrawFeaturesEXT multidraw {};
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eMultiDraw))
            {
                multidraw.multiDraw = VK_TRUE;
                featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&multidraw));
            }

            // Ray Tracing & Ray Query
            vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures {};
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eAccelerationStructure))
            {
                extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
                extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
                accelerationStructureFeatures.accelerationStructure = VK_TRUE;
                featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&accelerationStructureFeatures));
            }

            vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures {};
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eRayQuery))
            {
                extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
                rayQueryFeatures.rayQuery = VK_TRUE;
                featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&rayQueryFeatures));
            }

            vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures {};
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eRayTracingPipeline))
            {
                extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
                rayTracingFeatures.rayTracingPipeline = VK_TRUE;
                featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&rayTracingFeatures));
            }

            // Mesh Shaders
            vk::PhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures {};
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eMeshShader))
            {
                extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
                meshShaderFeatures.meshShader = VK_TRUE;
                meshShaderFeatures.taskShader = VK_TRUE;
                featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&meshShaderFeatures));
            }

            vk::PhysicalDeviceFragmentShaderInterlockFeaturesEXT fragmentShaderInterlockFeatures {};
            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eFragmentShaderInterlock))
            {
                extensions.push_back(VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME);
                fragmentShaderInterlockFeatures.fragmentShaderPixelInterlock = VK_TRUE;
                featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&fragmentShaderInterlockFeatures));
            }

            // === Link chain ===
            vk::BaseOutStructure* prev = nullptr;
            for (auto* f : featureChain)
            {
                if (prev)
                    prev->pNext = f;
                else
                    deviceFeatures2.pNext = f;
                prev = f;
            }
            if (prev)
                prev->pNext = nullptr;

            // === Filter extensions ===
            std::vector<const char*> filteredExtensions;
            for (const auto* ext : extensions)
            {
                if (backendOf(m_Backend).m_SupportedExtensions.count(ext))
                    filteredExtensions.push_back(ext);
                else
                    VULTRA_CORE_WARN("[RenderDevice] Skipping unsupported extension: {}", ext);
            }

            // === Device Creation ===
            vk::DeviceCreateInfo createInfo {};
            createInfo.queueCreateInfoCount    = 1;
            createInfo.pQueueCreateInfos       = &queueCreateInfo;
            createInfo.pNext                   = &deviceFeatures2;
            createInfo.enabledExtensionCount   = static_cast<uint32_t>(filteredExtensions.size());
            createInfo.ppEnabledExtensionNames = filteredExtensions.data();

            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eXR))
            {
                VULTRA_CORE_INFO("[RenderDevice] Creating Vulkan device via OpenXR runtime");

                VkDeviceCreateInfo          deviceCreateInfoC(createInfo);
                XrVulkanDeviceCreateInfoKHR xrVulkanDeviceCreateInfo {};
                xrVulkanDeviceCreateInfo.type                   = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
                xrVulkanDeviceCreateInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
                xrVulkanDeviceCreateInfo.systemId               = backendOf(m_Backend).m_XRDevice->m_XrSystemId;
                xrVulkanDeviceCreateInfo.vulkanCreateInfo       = &deviceCreateInfoC;
                xrVulkanDeviceCreateInfo.vulkanPhysicalDevice   = backendOf(m_Backend).m_PhysicalDevice;

                VkResult vkResult = VK_SUCCESS;
                VkDevice device   = nullptr;

                if (XR_FAILED(backendOf(m_Backend).m_XRDevice->xrCreateVulkanDeviceKHR(
                        backendOf(m_Backend).m_XRDevice->m_XrInstance, &xrVulkanDeviceCreateInfo, &device, &vkResult)) ||
                    vkResult != VK_SUCCESS)
                {
                    VULTRA_CORE_ERROR("[RenderDevice] OpenXR Vulkan device creation failed (VkResult: {})",
                                      vk::to_string(static_cast<vk::Result>(vkResult)));
                    VULTRA_CORE_WARN("[RenderDevice] Falling back to standard Vulkan device creation");
                    VK_CHECK(backendOf(m_Backend).m_PhysicalDevice.createDevice(&createInfo, nullptr, &backendOf(m_Backend).m_Device),
                             LOGTAG,
                             "Failed to create logical device");
                }
                else
                {
                    backendOf(m_Backend).m_Device = device;
                }
            }
            else
            {
                VULTRA_CORE_INFO("[RenderDevice] Creating standalone Vulkan device (non-XR)");
                VK_CHECK(backendOf(m_Backend).m_PhysicalDevice.createDevice(&createInfo, nullptr, &backendOf(m_Backend).m_Device),
                         LOGTAG,
                         "Failed to create logical device");
            }

            // === Get Generic Queue (for both graphics & compute) ===
            backendOf(m_Backend).m_Device.getQueue(backendOf(m_Backend).m_GenericQueueFamilyIndex, 0, &backendOf(m_Backend).m_GenericQueue);
        }

        void RenderDevice::createMemoryAllocator()
        {
            // https://github.com/YaaZ/VulkanMemoryAllocator-Hpp/issues/11#issuecomment-1237511514
            const vma::VulkanFunctions functions = vma::functionsFromDispatcher(VULKAN_HPP_DEFAULT_DISPATCHER);

            vma::AllocatorCreateInfo allocatorInfo {};
            allocatorInfo.physicalDevice   = backendOf(m_Backend).m_PhysicalDevice;
            allocatorInfo.device           = backendOf(m_Backend).m_Device;
            allocatorInfo.instance         = backendOf(m_Backend).m_Instance;
            allocatorInfo.pVulkanFunctions = &functions;

            if (HasFlagValues(backendOf(m_Backend).m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                allocatorInfo.flags |= vma::AllocatorCreateFlagBits::eBufferDeviceAddress;
            }

            vma::Allocator allocator;
            VK_CHECK(vma::createAllocator(&allocatorInfo, &allocator), LOGTAG, "Failed to create memory allocator");

            backendOf(m_Backend).m_MemoryAllocator = allocator;
        }

        void RenderDevice::createCommandPool()
        {
            vk::CommandPoolCreateInfo createInfo {};
            createInfo.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
            createInfo.queueFamilyIndex = backendOf(m_Backend).m_GenericQueueFamilyIndex;
            VK_CHECK(backendOf(m_Backend).m_Device.createCommandPool(&createInfo, nullptr, &backendOf(m_Backend).m_CommandPool),
                     LOGTAG,
                     "Failed to create command pool");
        }

        void RenderDevice::createPipelineCache()
        {
            vk::PipelineCacheCreateInfo createInfo {};
            VK_CHECK(backendOf(m_Backend).m_Device.createPipelineCache(&createInfo, nullptr, &backendOf(m_Backend).m_PipelineCache),
                     LOGTAG,
                     "Failed to create pipeline cache");
        }

        void RenderDevice::createDefaultDescriptorPool()
        {
            std::vector<vk::DescriptorPoolSize> poolSizes {
                {vk::DescriptorType::eSampler, 100},
                {vk::DescriptorType::eCombinedImageSampler, 100},
                {vk::DescriptorType::eSampledImage, 100},
                {vk::DescriptorType::eStorageImage, 100},
                {vk::DescriptorType::eUniformBuffer, 100},
                {vk::DescriptorType::eStorageBuffer, 100},
                {vk::DescriptorType::eInputAttachment, 100},
            };

            if (isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag))
            {
                poolSizes.emplace_back(vk::DescriptorType::eStorageBufferDynamic, 100);
                poolSizes.emplace_back(vk::DescriptorType::eAccelerationStructureKHR, 100);
            }

            vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
            descriptorPoolCreateInfo.setPoolSizes(poolSizes);
            descriptorPoolCreateInfo.setMaxSets(100);
            descriptorPoolCreateInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
            backendOf(m_Backend).m_DefaultDescriptorPool = backendOf(m_Backend).m_Device.createDescriptorPool(descriptorPoolCreateInfo);
        }

        void RenderDevice::createTracyContext()
        {
#ifdef TRACY_ENABLE
            const auto cmdBuffer = vk::CommandBuffer {reinterpret_cast<VkCommandBuffer>(allocateCommandBuffer())};
            backendOf(m_Backend).m_TracyContext =
                TracyVkContext(backendOf(m_Backend).m_PhysicalDevice,
                               backendOf(m_Backend).m_Device,
                               backendOf(m_Backend).m_GenericQueue,
                               cmdBuffer);
            backendOf(m_Backend).m_Device.freeCommandBuffers(backendOf(m_Backend).m_CommandPool, 1, &cmdBuffer);
            const auto deviceName = getPhysicalDeviceInfo().deviceName;
            TracyVkContextName(backendOf(m_Backend).m_TracyContext, deviceName.data(), static_cast<uint16_t>(deviceName.length()));
#endif
        }

        // NOLINTBEGIN
        void RenderDevice::createTracky()
        {
#ifdef __APPLE__
            const float timestampPeriodNs = backendOf(m_Backend).m_PhysicalDevice.getProperties().limits.timestampPeriod;
            TRACKY_STARTUP(backendOf(m_Backend).m_Device, 4 * 1024, timestampPeriodNs);
#else
            const float timestampPeriodNs = backendOf(m_Backend).m_PhysicalDevice.getProperties().limits.timestampPeriod;
            TRACKY_STARTUP(backendOf(m_Backend).m_Device, 64 * 1024, timestampPeriodNs);
#endif
        }
        // NOLINTEND

        std::uintptr_t RenderDevice::allocateCommandBuffer() const
        {
            assert(backendOf(m_Backend).m_Device);

            vk::CommandBufferAllocateInfo allocateInfo {};
            allocateInfo.commandPool        = backendOf(m_Backend).m_CommandPool;
            allocateInfo.level              = vk::CommandBufferLevel::ePrimary;
            allocateInfo.commandBufferCount = 1;

            vk::CommandBuffer commandBuffer {nullptr};
            VK_CHECK(backendOf(m_Backend).m_Device.allocateCommandBuffers(&allocateInfo, &commandBuffer),
                     LOGTAG,
                     "Failed to allocate command buffer");
            return reinterpret_cast<std::uintptr_t>(static_cast<VkCommandBuffer>(commandBuffer));
        }

        SamplerHandle RenderDevice::createSampler(const SamplerInfo& samplerInfo) const
        {
            vk::SamplerCreateInfo samplerCreateInfo {};
            samplerCreateInfo.magFilter               = toVk(samplerInfo.magFilter);
            samplerCreateInfo.minFilter               = toVk(samplerInfo.minFilter);
            samplerCreateInfo.mipmapMode              = toVk(samplerInfo.mipmapMode);
            samplerCreateInfo.addressModeU            = toVk(samplerInfo.addressModeS);
            samplerCreateInfo.addressModeV            = toVk(samplerInfo.addressModeT);
            samplerCreateInfo.addressModeW            = toVk(samplerInfo.addressModeR);
            samplerCreateInfo.minLod                  = samplerInfo.minLod;
            samplerCreateInfo.maxLod                  = samplerInfo.maxLod;
            samplerCreateInfo.mipLodBias              = 0.0f;
            samplerCreateInfo.borderColor             = toVk(samplerInfo.borderColor);
            samplerCreateInfo.unnormalizedCoordinates = false;
            samplerCreateInfo.compareEnable           = samplerInfo.compareOp.has_value();
            samplerCreateInfo.compareOp = toVk(samplerInfo.compareOp.value_or(CompareOp::eLess));
            samplerCreateInfo.anisotropyEnable = samplerInfo.maxAnisotropy.has_value();
            samplerCreateInfo.maxAnisotropy =
                samplerInfo.maxAnisotropy ?
                    glm::clamp(*samplerInfo.maxAnisotropy, 1.0f, getMaxSamplerAnisotropy()) :
                    0.0f;

            vk::Sampler sampler {nullptr};
            VK_CHECK(backendOf(m_Backend).m_Device.createSampler(&samplerCreateInfo, nullptr, &sampler), LOGTAG, "Failed to create sampler");

            return SamplerHandle {reinterpret_cast<std::uintptr_t>(static_cast<VkSampler>(sampler))};
        }

        CommandBuffer RenderDevice::createCommandBuffer() const
        {
            const auto fenceHandle = createFence();
            const vk::Fence fence {reinterpret_cast<VkFence>(fenceHandle.value)};
            return CommandBuffer {std::make_unique<VulkanCommandBuffer>(backendOf(m_Backend).m_Device,
                                                                         backendOf(m_Backend).m_CommandPool,
                                                                         vk::CommandBuffer {
                                                                             reinterpret_cast<VkCommandBuffer>(allocateCommandBuffer())},
                                                                         backendOf(m_Backend).m_TracyContext,
                                                                         fence,
                                                                         this,
                                                                         backendOf(m_Backend).m_UseKhrDynamicRendering,
                                                                         backendOf(m_Backend).m_UseKhrSynchronization2,
                                                                         isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag))};
        }

        RenderDevice& RenderDevice::execute(const std::function<void(CommandBuffer&)>& f, bool oneTime)
        {
            auto cb = createCommandBuffer();
            cb.begin();
            // TRACKY_GPU_NEXT_FRAME(cb);
            {
                TRACY_GPU_ZONE(cb, "ExecuteCommandBuffer");
                // TRACKY_GPU_ZONE(cb, "ExecuteCommandBuffer");
                std::invoke(f, cb);
            }
            return execute(cb, JobInfo {}, oneTime);
        }

        RenderDevice& RenderDevice::execute(CommandBuffer& cb, const JobInfo& jobInfo, bool oneTime)
        {
            cb.flushBarriers();
            TracyGpuCollect(backendOf(m_Backend).m_TracyContext, reinterpret_cast<VkCommandBuffer>(cb.getHandle()));
            cb.end();
            cb.submit(jobInfo, oneTime);

            return *this;
        }

        RenderDevice& RenderDevice::present(Swapchain& swapchain, const SemaphoreHandle wait)
        {
            ZoneScopedN("RHI::Present");

            assert(swapchain);
            assert(backendOf(m_Backend).m_GenericQueue);

            vk::PresentInfoKHR presentInfo {};
            const vk::Semaphore waitSemaphore {reinterpret_cast<VkSemaphore>(wait.value)};
            presentInfo.waitSemaphoreCount = static_cast<bool>(wait) ? 1u : 0u;
            presentInfo.pWaitSemaphores    = static_cast<bool>(wait) ? &waitSemaphore : nullptr;
            presentInfo.swapchainCount     = 1;
            const auto swapchainHandle     = vk::SwapchainKHR {
                reinterpret_cast<VkSwapchainKHR>(swapchain.getHandle())};
            const auto imageIndex = swapchain.getCurrentBufferIndex();
            presentInfo.pSwapchains   = &swapchainHandle;
            presentInfo.pImageIndices = &imageIndex;

            auto result = backendOf(m_Backend).m_GenericQueue.presentKHR(&presentInfo);
            switch (result)
            {
                case vk::Result::eSuboptimalKHR:
                case vk::Result::eErrorOutOfDateKHR:
                    swapchain.recreate();
                    [[fallthrough]];
                case vk::Result::eSuccess:
                    break;

                default:
                    assert(false);
            }

            return *this;
        }

        RenderDevice& RenderDevice::wait(const FenceHandle fence)
        {
            assert(static_cast<bool>(fence));

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackendOf(m_Backend);
                if (const auto it = backend.m_EmulatedFences.find(fence.value); it != backend.m_EmulatedFences.end())
                {
                    it->second = true;
                }
                return *this;
            }

            assert(backendOf(m_Backend).m_Device);
            const vk::Fence vkFence {reinterpret_cast<VkFence>(fence.value)};
            VK_CHECK(backendOf(m_Backend).m_Device.waitForFences(1, &vkFence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
                     LOGTAG,
                     "Failed to wait for fence");
            return reset(fence);
        }

        RenderDevice& RenderDevice::reset(const FenceHandle fence)
        {
            assert(static_cast<bool>(fence));

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackendOf(m_Backend);
                if (const auto it = backend.m_EmulatedFences.find(fence.value); it != backend.m_EmulatedFences.end())
                {
                    it->second = false;
                }
                return *this;
            }

            assert(backendOf(m_Backend).m_Device);
            const vk::Fence vkFence {reinterpret_cast<VkFence>(fence.value)};
            VK_CHECK(backendOf(m_Backend).m_Device.resetFences(1, &vkFence), LOGTAG, "Failed to reset fence");
            return *this;
        }

        RenderDevice& RenderDevice::waitIdle()
        {
            assert(backendOf(m_Backend).m_Device);
            backendOf(m_Backend).m_Device.waitIdle();
            return *this;
        }

        bool RenderDevice::saveTextureToFile(const Texture&         texture,
                                             const std::string&     filePath,
                                             const rhi::ImageAspect imageAspect)
        {
            auto cb = createCommandBuffer();
            cb.begin();

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .dstStage  = rhi::PipelineStages::eTransfer,
                    .dstAccess = rhi::Access::eTransferRead,
                });

            auto stagingBuffer = createStagingBuffer(texture.getSize());

            cb.copyImage(texture, stagingBuffer, imageAspect);

            // Wait for the copy to complete
            cb.getBarrierBuilder().bufferBarrier({.buffer = stagingBuffer},
                                                 {
                                                     .dstStage  = rhi::PipelineStages::eTransfer,
                                                     .dstAccess = rhi::Access::eTransferRead,
                                                 });

            execute(cb, JobInfo {});
            backendOf(m_Backend).m_GenericQueue.waitIdle();

            // map the staging buffer
            std::byte* data      = new std::byte[texture.getSize()];
            auto*      mappedPtr = stagingBuffer.map();
            if (!mappedPtr)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to map staging buffer for texture saving");
                return false;
            }

            // copy the data from the mapped staging buffer to the data pointer
            std::memcpy(data, mappedPtr, texture.getSize());

            // if it's BGRA, convert it to RGBA
            // normally for the swapchain
            if (texture.getPixelFormat() == rhi::PixelFormat::eBGRA8_UNorm)
            {
                for (size_t i = 0; i < texture.getSize(); i += 4)
                {
                    std::swap(data[i], data[i + 2]);
                }
            }

            // use stb_image to save the texture to a file
            if (!stbi_write_png(filePath.c_str(),
                                texture.getExtent().width,
                                texture.getExtent().height,
                                getBytesPerPixel(texture.getPixelFormat()),
                                data,
                                getBytesPerPixel(texture.getPixelFormat()) * texture.getExtent().width))
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to save texture to file: {}", filePath);
                return false;
            }

            // unmap the staging buffer
            stagingBuffer.unmap();

            // delete data
            delete[] static_cast<std::byte*>(data);

            return true;
        }

        AccelerationStructure
        RenderDevice::createAccelerationStructure(AccelerationStructureType           type,
                                                  AccelerationStructureBuildSizesInfo buildSizesInfo) const
        {
            assert(backendOf(m_Backend).m_Device);

            auto buffer = createAccelerationStructureBuffer(buildSizesInfo.accelerationStructureSize);

            vk::AccelerationStructureCreateInfoKHR createInfo {};
            switch (type)
            {
                case AccelerationStructureType::eTopLevel:
                    createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
                    break;
                case AccelerationStructureType::eBottomLevel:
                    createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
                    break;
                case AccelerationStructureType::eGeneric:
                    createInfo.type = vk::AccelerationStructureTypeKHR::eGeneric;
                    break;
            }
            createInfo.size   = buildSizesInfo.accelerationStructureSize;
            createInfo.buffer = vk::Buffer {reinterpret_cast<VkBuffer>(buffer.getHandle())};
            createInfo.offset = 0;

            vk::AccelerationStructureKHR handle {nullptr};
            VK_CHECK(backendOf(m_Backend).m_Device.createAccelerationStructureKHR(&createInfo, nullptr, &handle),
                     LOGTAG,
                     "Failed to create acceleration structure");

            vk::AccelerationStructureDeviceAddressInfoKHR addressInfo {};
            addressInfo.accelerationStructure = handle;
            DeviceAddress deviceAddress {
                backendOf(m_Backend).m_Device.getAccelerationStructureAddressKHR(addressInfo)};

            return AccelerationStructure {
                std::make_unique<VulkanAccelerationStructureBackend>(
                    backendOf(m_Backend).m_Device, handle, deviceAddress, type, std::move(buildSizesInfo), std::move(buffer))};
        }

        AccelerationStructure RenderDevice::createBuildSingleGeometryBLAS(DeviceAddress vertexBufferAddress,
                                                                          DeviceAddress indexBufferAddress,
                                                                          DeviceAddress transformBufferAddress,
                                                                          uint32_t vertexStride,
                                                                          uint32_t vertexCount,
                                                                          uint32_t indexCount)
        {
            VULTRA_CORE_ASSERT(isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag),
                               "[RenderDevice] Raytracing Pipeline feature is not enabled!");
            VULTRA_CORE_ASSERT(backendOf(m_Backend).m_AccelerationStructureFeatures.accelerationStructure,
                               "[RenderDevice] Acceleration Structure feature is not enabled!");

            // Describe the geometry
            vk::AccelerationStructureGeometryTrianglesDataKHR triangles {};
            triangles.vertexFormat                = vk::Format::eR32G32B32Sfloat;
            triangles.vertexData.deviceAddress    = vertexBufferAddress.value;
            triangles.vertexStride                = vertexStride;
            triangles.indexType                   = vk::IndexType::eUint32;
            triangles.indexData.deviceAddress     = indexBufferAddress.value;
            triangles.transformData.deviceAddress = transformBufferAddress.value;
            triangles.maxVertex                   = vertexCount - 1;

            vk::AccelerationStructureGeometryKHR geometry {};
            geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
            geometry.flags        = vk::GeometryFlagBitsKHR::eOpaque;
            geometry.geometry.setTriangles(triangles);

            // Get build sizes
            vk::AccelerationStructureBuildRangeInfoKHR buildRangeInfo {};
            buildRangeInfo.primitiveCount  = indexCount / 3;
            buildRangeInfo.primitiveOffset = 0;
            buildRangeInfo.firstVertex     = 0;
            buildRangeInfo.transformOffset = 0;

            vk::AccelerationStructureBuildGeometryInfoKHR buildGeometryInfo {};
            buildGeometryInfo.type          = vk::AccelerationStructureTypeKHR::eBottomLevel;
            buildGeometryInfo.flags         = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            buildGeometryInfo.mode          = vk::BuildAccelerationStructureModeKHR::eBuild;
            buildGeometryInfo.geometryCount = 1;
            buildGeometryInfo.pGeometries   = &geometry;

            auto buildSizes = backendOf(m_Backend).m_Device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometryInfo, buildRangeInfo.primitiveCount);

            // Assign RHI build sizes info
            AccelerationStructureBuildSizesInfo buildSizesInfo {};
            buildSizesInfo.accelerationStructureSize = buildSizes.accelerationStructureSize;
            buildSizesInfo.buildScratchSize          = buildSizes.buildScratchSize;
            buildSizesInfo.updateScratchSize         = buildSizes.updateScratchSize;

            // Create the BLAS
            auto blas = createAccelerationStructure(AccelerationStructureType::eBottomLevel, buildSizesInfo);

            // Create a scratch buffer
            auto scratchBuffer = createScratchBuffer(buildSizes.buildScratchSize);

            // Fill geometry info
            buildGeometryInfo.dstAccelerationStructure =
                vk::AccelerationStructureKHR {reinterpret_cast<VkAccelerationStructureKHR>(blas.getHandle())};
            buildGeometryInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer).value;
            std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> buildRangeInfos = {&buildRangeInfo};

            // Build the BLAS using a one-time command buffer
            execute(
                [&](CommandBuffer& cb) {
                    vk::CommandBuffer {reinterpret_cast<VkCommandBuffer>(cb.getHandle())}.buildAccelerationStructuresKHR(
                        1,
                        &buildGeometryInfo,
                        buildRangeInfos.data());
                },
                true);

            return blas;
        }

        AccelerationStructure RenderDevice::createBuildRenderMeshBLAS(std::vector<RenderSubMesh>& subMeshes)
        {
            VULTRA_CORE_ASSERT(isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag),
                               "[RenderDevice] Raytracing Pipeline feature is not enabled!");
            VULTRA_CORE_ASSERT(backendOf(m_Backend).m_AccelerationStructureFeatures.accelerationStructure,
                               "[RenderDevice] Acceleration Structure feature is not enabled!");

            VULTRA_CORE_ASSERT(!subMeshes.empty(), "[RenderDevice] Cannot build BLAS from empty subMeshes!");

            std::vector<vk::AccelerationStructureGeometryKHR>       geometries;
            std::vector<vk::AccelerationStructureBuildRangeInfoKHR> buildRanges;
            geometries.reserve(subMeshes.size());
            buildRanges.reserve(subMeshes.size());

            // Describe the geometry, one for each sub-mesh
            for (auto& sm : subMeshes)
            {
                vk::AccelerationStructureGeometryTrianglesDataKHR triangles {};
                triangles.vertexFormat                = vk::Format::eR32G32B32Sfloat;
                triangles.vertexData.deviceAddress    = sm.vertexBufferAddress.value;
                triangles.vertexStride                = sm.vertexStride;
                triangles.indexType                   = toVkIndexType(sm.indexType);
                triangles.indexData.deviceAddress     = sm.indexBufferAddress.value;
                triangles.transformData.deviceAddress = sm.transformBufferAddress.value;
                triangles.maxVertex                   = sm.vertexCount - 1;

                vk::AccelerationStructureGeometryKHR geometry {};
                geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
                if (sm.opaque)
                {
                    geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
                }
                geometry.geometry.setTriangles(triangles);
                geometries.push_back(geometry);

                vk::AccelerationStructureBuildRangeInfoKHR range {};
                range.primitiveCount  = sm.indexCount / 3;
                range.primitiveOffset = 0;
                range.firstVertex     = 0;
                range.transformOffset = 0;
                buildRanges.push_back(range);
            }

            // Get build sizes
            vk::AccelerationStructureBuildGeometryInfoKHR buildGeometryInfo {};
            buildGeometryInfo.type          = vk::AccelerationStructureTypeKHR::eBottomLevel;
            buildGeometryInfo.flags         = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            buildGeometryInfo.mode          = vk::BuildAccelerationStructureModeKHR::eBuild;
            buildGeometryInfo.geometryCount = static_cast<uint32_t>(geometries.size());
            buildGeometryInfo.pGeometries   = geometries.data();

            std::vector<uint32_t> primitiveCounts;
            primitiveCounts.reserve(buildRanges.size());
            for (auto& r : buildRanges)
                primitiveCounts.push_back(r.primitiveCount);

            auto buildSizes = backendOf(m_Backend).m_Device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometryInfo, primitiveCounts);

            // Assign RHI build sizes info
            AccelerationStructureBuildSizesInfo buildSizesInfo {};
            buildSizesInfo.accelerationStructureSize = buildSizes.accelerationStructureSize;
            buildSizesInfo.buildScratchSize          = buildSizes.buildScratchSize;
            buildSizesInfo.updateScratchSize         = buildSizes.updateScratchSize;

            // Create the BLAS
            auto blas = createAccelerationStructure(AccelerationStructureType::eBottomLevel, buildSizesInfo);

            // Create a scratch buffer
            auto scratchBuffer = createScratchBuffer(buildSizes.buildScratchSize);

            // Fill geometry info
            buildGeometryInfo.dstAccelerationStructure =
                vk::AccelerationStructureKHR {reinterpret_cast<VkAccelerationStructureKHR>(blas.getHandle())};
            buildGeometryInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer).value;

            std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> buildRangePtrs;
            buildRangePtrs.reserve(buildRanges.size());
            for (auto& r : buildRanges)
                buildRangePtrs.push_back(&r);

            // Build the BLAS using a one-time command buffer
            execute(
                [&](CommandBuffer& cb) {
                    vk::CommandBuffer {reinterpret_cast<VkCommandBuffer>(cb.getHandle())}.buildAccelerationStructuresKHR(
                        1,
                        &buildGeometryInfo,
                        buildRangePtrs.data());
                },
                true);

            return blas;
        }

        AccelerationStructure RenderDevice::createBuildSingleInstanceTLAS(const AccelerationStructure& referenceBLAS,
                                                                          const glm::mat4&             transform)
        {
            VULTRA_CORE_ASSERT(isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag),
                               "[RenderDevice] Raytracing Pipeline feature is not enabled!");
            VULTRA_CORE_ASSERT(backendOf(m_Backend).m_AccelerationStructureFeatures.accelerationStructure,
                               "[RenderDevice] Acceleration Structure feature is not enabled!");

            vk::TransformMatrixKHR vkTransform {};
            glm::mat4              rowMajor = glm::transpose(transform);
            memcpy(&vkTransform, &rowMajor, sizeof(vk::TransformMatrixKHR));

            vk::AccelerationStructureInstanceKHR instance {};
            instance.transform                              = vkTransform;
            instance.instanceCustomIndex                    = 0;
            instance.mask                                   = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = static_cast<VkGeometryInstanceFlagsKHR>(
                static_cast<uint32_t>(RayTracingInstanceFlags::eTriangleFacingCullDisable));
            instance.accelerationStructureReference = referenceBLAS.getDeviceAddress().value;

            auto  instancesBuffer = createInstancesBuffer(1);
            void* mapped          = instancesBuffer.map();
            memcpy(mapped, &instance, sizeof(vk::AccelerationStructureInstanceKHR));
            instancesBuffer.unmap();

            vk::DeviceOrHostAddressConstKHR instanceData {};
            instanceData.deviceAddress = getBufferDeviceAddress(instancesBuffer).value;

            vk::AccelerationStructureGeometryKHR geometry {};
            geometry.geometryType = vk::GeometryTypeKHR::eInstances;
            geometry.flags        = vk::GeometryFlagBitsKHR::eOpaque;
            geometry.geometry.instances.sType =
                vk::StructureType::eAccelerationStructureGeometryInstancesDataKHR; // Must be set explicitly
            geometry.geometry.instances.arrayOfPointers = VK_FALSE;
            geometry.geometry.instances.data            = instanceData;

            vk::AccelerationStructureBuildRangeInfoKHR rangeInfo {};
            rangeInfo.primitiveCount = 1;

            vk::AccelerationStructureBuildGeometryInfoKHR buildInfo {};
            buildInfo.type          = vk::AccelerationStructureTypeKHR::eTopLevel;
            buildInfo.flags         = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            buildInfo.mode          = vk::BuildAccelerationStructureModeKHR::eBuild;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries   = &geometry;

            auto buildSizes = backendOf(m_Backend).m_Device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, rangeInfo.primitiveCount);

            AccelerationStructureBuildSizesInfo sizesInfo {};
            sizesInfo.accelerationStructureSize = buildSizes.accelerationStructureSize;
            sizesInfo.buildScratchSize          = buildSizes.buildScratchSize;
            sizesInfo.updateScratchSize         = buildSizes.updateScratchSize;

            auto tlas          = createAccelerationStructure(AccelerationStructureType::eTopLevel, sizesInfo);
            auto scratchBuffer = createScratchBuffer(buildSizes.buildScratchSize);

            buildInfo.dstAccelerationStructure =
                vk::AccelerationStructureKHR {reinterpret_cast<VkAccelerationStructureKHR>(tlas.getHandle())};
            buildInfo.scratchData.deviceAddress                             = getBufferDeviceAddress(scratchBuffer).value;
            std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> ranges = {&rangeInfo};

            execute(
                [&](CommandBuffer& cb) {
                    vk::CommandBuffer {reinterpret_cast<VkCommandBuffer>(cb.getHandle())}.buildAccelerationStructuresKHR(
                        1,
                        &buildInfo,
                        ranges.data());
                },
                true);

            return tlas;
        }

        AccelerationStructure
        RenderDevice::createBuildMultipleInstanceTLAS(const std::vector<RayTracingInstance>& instances)
        {
            VULTRA_CORE_ASSERT(isRaytracingOrRayQueryEnabled(backendOf(m_Backend).m_FeatureFlag),
                               "[RenderDevice] Raytracing Pipeline feature is not enabled!");
            VULTRA_CORE_ASSERT(backendOf(m_Backend).m_AccelerationStructureFeatures.accelerationStructure,
                               "[RenderDevice] Acceleration Structure feature is not enabled!");

            VULTRA_CORE_ASSERT(!instances.empty(), "[RenderDevice] Cannot build TLAS from empty instances!");

            std::vector<vk::AccelerationStructureInstanceKHR> vkInstances;
            vkInstances.reserve(instances.size());

            for (const auto& inst : instances)
            {
                vk::TransformMatrixKHR vkTransform {};
                glm::mat4              rowMajor = glm::transpose(inst.transform);
                memcpy(&vkTransform, &rowMajor, sizeof(vk::TransformMatrixKHR));

                vk::AccelerationStructureInstanceKHR vkInstance {};
                vkInstance.transform                              = vkTransform;
                vkInstance.instanceCustomIndex                    = inst.instanceID;
                vkInstance.mask                                   = inst.mask;
                vkInstance.instanceShaderBindingTableRecordOffset = inst.sbtRecordOffset;
                vkInstance.flags = static_cast<VkGeometryInstanceFlagsKHR>(static_cast<uint32_t>(inst.flags));
                vkInstance.accelerationStructureReference         = inst.blas->getDeviceAddress().value;

                vkInstances.push_back(vkInstance);
            }

            auto  instancesBuffer = createInstancesBuffer(static_cast<uint32_t>(vkInstances.size()));
            void* mapped          = instancesBuffer.map();
            memcpy(mapped, vkInstances.data(), sizeof(vk::AccelerationStructureInstanceKHR) * vkInstances.size());
            instancesBuffer.unmap();

            vk::DeviceOrHostAddressConstKHR instanceData {};
            instanceData.deviceAddress = getBufferDeviceAddress(instancesBuffer).value;

            vk::AccelerationStructureGeometryKHR geometry {};
            geometry.geometryType = vk::GeometryTypeKHR::eInstances;
            geometry.flags        = vk::GeometryFlagBitsKHR::eOpaque;
            geometry.geometry.instances.sType =
                vk::StructureType::eAccelerationStructureGeometryInstancesDataKHR; // Must be set explicitly
            geometry.geometry.instances.arrayOfPointers = VK_FALSE;
            geometry.geometry.instances.data            = instanceData;

            vk::AccelerationStructureBuildRangeInfoKHR rangeInfo {};
            rangeInfo.primitiveCount = static_cast<uint32_t>(vkInstances.size());

            vk::AccelerationStructureBuildGeometryInfoKHR buildInfo {};
            buildInfo.type          = vk::AccelerationStructureTypeKHR::eTopLevel;
            buildInfo.flags         = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            buildInfo.mode          = vk::BuildAccelerationStructureModeKHR::eBuild;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries   = &geometry;

            auto buildSizes = backendOf(m_Backend).m_Device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, rangeInfo.primitiveCount);

            AccelerationStructureBuildSizesInfo sizesInfo {};
            sizesInfo.accelerationStructureSize = buildSizes.accelerationStructureSize;
            sizesInfo.buildScratchSize          = buildSizes.buildScratchSize;
            sizesInfo.updateScratchSize         = buildSizes.updateScratchSize;

            auto tlas          = createAccelerationStructure(AccelerationStructureType::eTopLevel, sizesInfo);
            auto scratchBuffer = createScratchBuffer(buildSizes.buildScratchSize);

            buildInfo.dstAccelerationStructure =
                vk::AccelerationStructureKHR {reinterpret_cast<VkAccelerationStructureKHR>(tlas.getHandle())};
            buildInfo.scratchData.deviceAddress                             = getBufferDeviceAddress(scratchBuffer).value;
            std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> ranges = {&rangeInfo};

            execute(
                [&](CommandBuffer& cb) {
                    vk::CommandBuffer {reinterpret_cast<VkCommandBuffer>(cb.getHandle())}.buildAccelerationStructuresKHR(
                        1,
                        &buildInfo,
                        ranges.data());
                },
                true);

            return tlas;
        }

        ScratchBuffer RenderDevice::createScratchBuffer(uint64_t size, AllocationHints allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            Buffer buffer = makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                                       size,
                                       BufferUsage::eStorageBuffer | BufferUsage::eShaderDeviceAddress,
                                       makeAllocationFlags(allocationHint),
                                       vma::MemoryUsage::eAutoPreferDevice);

            DeviceAddress bufferAddress = getBufferDeviceAddress(buffer);

            return ScratchBuffer {std::move(buffer), bufferAddress};
        }

        InstanceBuffer RenderDevice::createInstancesBuffer(uint32_t instanceCount, AllocationHints allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            return InstanceBuffer {makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                                              instanceCount * sizeof(VkAccelerationStructureInstanceKHR),
                                              BufferUsage::eShaderDeviceAddress |
                                                  BufferUsage::eAccelerationBuildInput,
                                              makeAllocationFlags(allocationHint),
                                              vma::MemoryUsage::eCpuToGpu)}; // Host visible & coherent for easy
                                                                               // mapping
        }

        TransformBuffer RenderDevice::createTransformBuffer(AllocationHints allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            return TransformBuffer {makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                                               sizeof(vk::TransformMatrixKHR),
                                               BufferUsage::eShaderDeviceAddress |
                                                   BufferUsage::eAccelerationBuildInput,
                                               makeAllocationFlags(allocationHint),
                                               vma::MemoryUsage::eCpuToGpu)}; // Host visible & coherent for easy
                                                                               // mapping
        }

        ShaderBindingTable RenderDevice::createShaderBindingTable(const rhi::RayTracingPipeline& pipeline,
                                                                  AllocationHints                allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            const auto& props = backendOf(m_Backend).m_RayTracingPipelineProperties;

            const uint32_t handleSize        = props.shaderGroupHandleSize;
            const uint32_t handleSizeAligned = alignedSize(handleSize, props.shaderGroupHandleAlignment);

            const uint32_t raygenCount   = pipeline.getRaygenGroupCount();
            const uint32_t missCount     = pipeline.getMissGroupCount();
            const uint32_t hitCount      = pipeline.getHitGroupCount();
            const uint32_t callableCount = pipeline.getCallableGroupCount();

            const uint32_t raygenSize   = handleSizeAligned * raygenCount;
            const uint32_t missSize     = handleSizeAligned * missCount;
            const uint32_t hitSize      = handleSizeAligned * hitCount;
            const uint32_t callableSize = handleSizeAligned * callableCount;

            const auto baseAlign = props.shaderGroupBaseAlignment;

            auto alignedOffset = [](uint32_t offset, uint32_t alignment) {
                return (offset + alignment - 1) & ~(alignment - 1);
            };

            uint32_t raygenOffset   = 0;
            uint32_t missOffset     = alignedOffset(raygenOffset + raygenSize, baseAlign);
            uint32_t hitOffset      = alignedOffset(missOffset + missSize, baseAlign);
            uint32_t callableOffset = alignedOffset(hitOffset + hitSize, baseAlign);

            const uint32_t sbtSize = callableOffset + callableSize;

            // Chunk buffer
            Buffer sbtBuffer = makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                                          sbtSize,
                                          BufferUsage::eShaderBindingTable | BufferUsage::eShaderDeviceAddress |
                                              BufferUsage::eTransferDst,
                                          makeAllocationFlags(allocationHint),
                                          vma::MemoryUsage::eCpuToGpu); // Host visible & coherent for easy mapping

            std::vector<uint8_t> handles(sbtSize);
            VK_CHECK(backendOf(m_Backend).m_Device.getRayTracingShaderGroupHandlesKHR(
                         vk::Pipeline {reinterpret_cast<VkPipeline>(pipeline.getHandle())},
                         0,
                         pipeline.getGroupCount(),
                         handles.size(),
                         handles.data()),
                     "RenderDevice",
                     "Failed to get shader group handles");

            auto* dst = static_cast<uint8_t*>(sbtBuffer.map());

            auto copyHandles = [&](uint32_t groupBaseIndex, uint32_t count, uint32_t dstOffset) {
                for (uint32_t i = 0; i < count; ++i)
                {
                    const uint8_t* src = handles.data() + (groupBaseIndex + i) * handleSize;
                    memcpy(dst + dstOffset + i * handleSizeAligned, src, handleSize);
                }
            };

            uint32_t groupBase = 0;
            copyHandles(groupBase, raygenCount, raygenOffset);
            groupBase += raygenCount;
            copyHandles(groupBase, missCount, missOffset);
            groupBase += missCount;
            copyHandles(groupBase, hitCount, hitOffset);
            groupBase += hitCount;
            copyHandles(groupBase, callableCount, callableOffset);

            sbtBuffer.unmap();

            // Fill in StrideDeviceAddressRegion
            auto makeRegion = [&](uint32_t offset, uint32_t count) -> StrideDeviceAddressRegion {
                if (count == 0)
                    return {};
                return getSbtEntryStrideDeviceAddressRegion(sbtBuffer, count, DeviceAddress {offset});
            };

            ShaderBindingTable::Regions regions {
                .raygen = makeRegion(raygenOffset, raygenCount),
                .miss   = makeRegion(missOffset, missCount),
                .hit    = makeRegion(hitOffset, hitCount),
                .callable =
                    callableCount > 0 ? std::make_optional(makeRegion(callableOffset, callableCount)) : std::nullopt,
            };

            return ShaderBindingTable {std::move(sbtBuffer), std::move(regions)};
        }

        DeviceAddress RenderDevice::getBufferDeviceAddress(const Buffer& buffer) const
        {
            assert(backendOf(m_Backend).m_Device);
            vk::BufferDeviceAddressInfo bufferDeviceAddressInfo {};
            bufferDeviceAddressInfo.buffer = vk::Buffer {reinterpret_cast<VkBuffer>(buffer.getHandle())};
            return DeviceAddress {backendOf(m_Backend).m_Device.getBufferAddress(bufferDeviceAddressInfo)};
        }

        RayTracingPipelineProperties RenderDevice::getRayTracingPipelineProperties() const
        {
            return RayTracingPipelineProperties {
                backendOf(m_Backend).m_RayTracingPipelineProperties.shaderGroupHandleSize,
                backendOf(m_Backend).m_RayTracingPipelineProperties.maxRayRecursionDepth,
                backendOf(m_Backend).m_RayTracingPipelineProperties.maxShaderGroupStride,
                backendOf(m_Backend).m_RayTracingPipelineProperties.shaderGroupBaseAlignment,
                backendOf(m_Backend).m_RayTracingPipelineProperties.shaderGroupHandleCaptureReplaySize,
                backendOf(m_Backend).m_RayTracingPipelineProperties.maxRayDispatchInvocationCount,
                backendOf(m_Backend).m_RayTracingPipelineProperties.shaderGroupHandleAlignment,
                backendOf(m_Backend).m_RayTracingPipelineProperties.maxRayHitAttributeSize,
            };
        }

        AccelerationStructureBuffer
        RenderDevice::createAccelerationStructureBuffer(uint64_t size, AllocationHints allocationHint) const
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            return AccelerationStructureBuffer {makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                                                            size,
                                                            BufferUsage::eAccelerationStorage |
                                                                BufferUsage::eShaderDeviceAddress,
                                                            makeAllocationFlags(allocationHint),
                                                            vma::MemoryUsage::eAutoPreferDevice)};
        }

        DeviceAddress RenderDevice::getAccelerationStructureDeviceAddress(const AccelerationStructure& accel) const
        {
            assert(backendOf(m_Backend).m_Device);
            vk::AccelerationStructureDeviceAddressInfoKHR addressInfo {};
            addressInfo.accelerationStructure =
                vk::AccelerationStructureKHR {reinterpret_cast<VkAccelerationStructureKHR>(accel.getHandle())};
            return DeviceAddress {backendOf(m_Backend).m_Device.getAccelerationStructureAddressKHR(addressInfo)};
        }

        StrideDeviceAddressRegion RenderDevice::getSbtEntryStrideDeviceAddressRegion(const Buffer& sbt,
                                                                                     uint32_t      handleCount,
                                                                                     DeviceAddress offset) const
        {
            const uint32_t handleSizeAligned = alignedSize(backendOf(m_Backend).m_RayTracingPipelineProperties.shaderGroupHandleSize,
                                                           backendOf(m_Backend).m_RayTracingPipelineProperties.shaderGroupHandleAlignment);

            return StrideDeviceAddressRegion {
                .deviceAddress = DeviceAddress {getBufferDeviceAddress(sbt).value + offset.value},
                .stride        = handleSizeAligned,
                .size          = handleCount * handleSizeAligned,
            };
        }

        Ref<rhi::Buffer> RenderDevice::createBindlessStorageBuffer(AllocationHints allocationHint)
        {
            assert(backendOf(m_Backend).m_MemoryAllocator);

            // Bindless ownership is managed at higher level (e.g., resource::GpuScene).
            // Provide a minimal device-local storage buffer handle.
            Buffer buffer = makeBuffer(backendOf(m_Backend).m_MemoryAllocator,
                                       1,
                                       BufferUsage::eStorageBuffer | BufferUsage::eShaderDeviceAddress,
                                       makeAllocationFlags(allocationHint),
                                       vma::MemoryUsage::eAutoPreferDevice);

            return createRef<rhi::Buffer>(std::move(buffer));
        }

        Ref<rhi::Texture> RenderDevice::createDefaultWhite1x1Texture2D()
        {
            uint32_t whitePixel = 0xFFFFFFFF; // RGBA8 white

            auto texture = Texture::Builder {}
                               .setExtent({1, 1})
                               .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                               .setUsageFlags(ImageUsage::eSampled | ImageUsage::eTransferDst)
                               .setupOptimalSampler(true)
                               .build(*this);

            // Upload the white pixel using a staging buffer
            auto stagingBuffer = createStagingBuffer(sizeof(whitePixel));
            rhi::upload(*this, stagingBuffer, {}, texture, false);

            return createRef<rhi::Texture>(std::move(texture));
        }
    } // namespace rhi
} // namespace vultra
