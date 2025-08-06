#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/base/ranges.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/vk/macro.hpp"
#include "vultra/function/openxr/xr_device.hpp"

#include <SDL3/SDL_vulkan.h>
#include <cpptrace/cpptrace.hpp>
#include <glm/common.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <set>

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
    struct hash<vk::PushConstantRange>
    {
        auto operator()(const vk::PushConstantRange& v) const noexcept
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
#ifdef VULTRA_ENABLE_VK_VALIDATION_STACK_TRACE
                cpptrace::generate_trace().print();
#endif
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
        if (static_cast<bool>(hints & vultra::rhi::AllocationHints::eMinMemory))
        {
            flags |= vma::AllocationCreateFlagBits::eStrategyMinMemory;
        }
        if (static_cast<bool>(hints & vultra::rhi::AllocationHints::eSequentialWrite))
        {
            flags |= vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped;
        }
        if (static_cast<bool>(hints & vultra::rhi::AllocationHints::eRandomAccess))
        {
            flags |= vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped;
        }
        return flags;
    }
} // namespace

namespace vultra
{
    namespace rhi
    {
        constexpr auto LOGTAG = "RenderDevice";

        RenderDevice::RenderDevice(const RenderDeviceFeatureFlagBits featureFlag, std::string_view appName) :
            m_FeatureFlag(featureFlag), m_AppName(appName)
        {
            if (static_cast<bool>(featureFlag & RenderDeviceFeatureFlagBits::eOpenXR))
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
            if (m_Device)
            {
                m_Device.waitIdle();
            }

            for (auto [_, layout] : m_DescriptorSetLayouts)
            {
                m_Device.destroyDescriptorSetLayout(layout);
            }

            for (auto [_, layout] : m_PipelineLayouts)
            {
                m_Device.destroyPipelineLayout(layout);
            }

            for (auto [_, sampler] : m_Samplers)
            {
                m_Device.destroySampler(sampler);
            }

            TracyVkDestroy(m_TracyContext);
            TRACKY_TEARDOWN();

            if (m_MemoryAllocator)
            {
                m_MemoryAllocator.destroy();
            }

            if (m_Device)
            {
                m_Device.destroyDescriptorPool(m_DefaultDescriptorPool);
                m_Device.destroyPipelineCache(m_PipelineCache);
                m_Device.destroyCommandPool(m_CommandPool);
                m_Device.destroy();
            }

            if (m_Instance)
            {
#if _DEBUG
                m_Instance.destroyDebugUtilsMessengerEXT(m_DebugMessenger);
#endif
                m_Instance.destroy();
            }

            if (static_cast<bool>(m_FeatureFlag & RenderDeviceFeatureFlagBits::eOpenXR))
            {
                delete m_XRDevice;
            }
        }

        RenderDeviceFeatureFlagBits RenderDevice::getFeatureFlag() const { return m_FeatureFlag; }

        std::string RenderDevice::getName() const
        {
            const auto v = m_PhysicalDevice.getProperties().apiVersion;
            return fmt::format(
                "Vulkan {}.{}.{}", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
        }

        PhysicalDeviceInfo RenderDevice::getPhysicalDeviceInfo() const
        {
            return PhysicalDeviceInfo {
                .vendorId   = m_PhysicalDevice.getProperties().vendorID,
                .deviceId   = m_PhysicalDevice.getProperties().deviceID,
                .deviceName = m_PhysicalDevice.getProperties().deviceName,
            };
        }

        vk::PhysicalDeviceLimits RenderDevice::getDeviceLimits() const
        {
            assert(m_PhysicalDevice);
            return m_PhysicalDevice.getProperties().limits;
        }

        vk::PhysicalDeviceFeatures RenderDevice::getDeviceFeatures() const
        {
            assert(m_PhysicalDevice);
            return m_PhysicalDevice.getFeatures();
        }

        vk::FormatProperties RenderDevice::getFormatProperties(const PixelFormat pixelFormat) const
        {
            assert(m_PhysicalDevice);
            vk::FormatProperties props {};
            m_PhysicalDevice.getFormatProperties(static_cast<vk::Format>(pixelFormat), &props);
            return props;
        }

        Swapchain RenderDevice::createSwapchain(os::Window&             window,
                                                const Swapchain::Format format,
                                                const VerticalSync      vsync) const
        {
            assert(m_Device);

            return Swapchain {
                m_Instance,
                m_PhysicalDevice,
                m_Device,
                &window,
                format,
                vsync,
            };
        }

        vk::Fence RenderDevice::createFence(const bool signaled) const
        {
            assert(m_Device);
            vk::FenceCreateInfo createInfo {};
            createInfo.flags = signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlags(0u);
            vk::Fence fence {nullptr};
            VK_CHECK(m_Device.createFence(&createInfo, nullptr, &fence), LOGTAG, "Failed to create fence");
            return fence;
        }

        vk::Semaphore RenderDevice::createSemaphore()
        {
            assert(m_Device);
            vk::SemaphoreCreateInfo createInfo {};
            createInfo.flags = vk::SemaphoreCreateFlags(0);
            vk::Semaphore semaphore {nullptr};
            VK_CHECK(m_Device.createSemaphore(&createInfo, nullptr, &semaphore), LOGTAG, "Failed to create semaphore");
            return semaphore;
        }

        Buffer RenderDevice::createStagingBuffer(const vk::DeviceSize size, const void* data) const
        {
            assert(m_MemoryAllocator);

            Buffer stagingBuffer {
                m_MemoryAllocator,
                size,
                vk::BufferUsageFlagBits::eTransferSrc,
                makeAllocationFlags(AllocationHints::eSequentialWrite),
                vma::MemoryUsage::eAutoPreferHost,
            };

            if (data)
            {
                auto* mappedPtr = stagingBuffer.map();
                std::memcpy(mappedPtr, data, size);
                stagingBuffer.unmap();
            }
            return stagingBuffer;
        }

        VertexBuffer RenderDevice::createVertexBuffer(const Buffer::Stride  stride,
                                                      const vk::DeviceSize  capacity,
                                                      const AllocationHints allocationHint) const
        {
            assert(m_MemoryAllocator);

            return VertexBuffer {
                Buffer {
                    m_MemoryAllocator,
                    stride * capacity,
                    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                    makeAllocationFlags(allocationHint),
                    vma::MemoryUsage::eAutoPreferDevice,
                },
                stride,
            };
        }

        IndexBuffer RenderDevice::createIndexBuffer(IndexType             indexType,
                                                    const vk::DeviceSize  capacity,
                                                    const AllocationHints allocationHint) const
        {
            assert(m_MemoryAllocator);

            return IndexBuffer {
                Buffer {
                    m_MemoryAllocator,
                    static_cast<uint8_t>(indexType) * capacity,
                    vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                    makeAllocationFlags(allocationHint),
                    vma::MemoryUsage::eAutoPreferDevice,
                },
                indexType,
            };
        }

        UniformBuffer RenderDevice::createUniformBuffer(const vk::DeviceSize  size,
                                                        const AllocationHints allocationHint) const
        {
            assert(m_MemoryAllocator);

            return UniformBuffer {
                Buffer {
                    m_MemoryAllocator,
                    size,
                    vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
                    makeAllocationFlags(allocationHint),
                    vma::MemoryUsage::eAutoPreferDevice,
                },
            };
        }

        StorageBuffer RenderDevice::createStorageBuffer(const vk::DeviceSize  size,
                                                        const AllocationHints allocationHint) const
        {
            assert(m_MemoryAllocator);

            return StorageBuffer {
                Buffer {
                    m_MemoryAllocator,
                    size,
                    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                    makeAllocationFlags(allocationHint),
                    vma::MemoryUsage::eAutoPreferDevice,
                },
            };
        }

        ScratchBuffer RenderDevice::createScratchBuffer(vk::DeviceSize size, AllocationHints allocationHint) const
        {
            assert(m_MemoryAllocator);

            return ScratchBuffer {
                Buffer {
                    m_MemoryAllocator,
                    size,
                    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                    makeAllocationFlags(allocationHint),
                    vma::MemoryUsage::eAutoPreferDevice,
                },
            };
        }

        AccelerationStructureBuffer
        RenderDevice::createAccelerationStructureBuffer(vk::DeviceSize size, AllocationHints allocationHint) const
        {
            assert(m_MemoryAllocator);

            return AccelerationStructureBuffer {
                Buffer {
                    m_MemoryAllocator,
                    size,
                    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                        vk::BufferUsageFlagBits::eShaderDeviceAddress,
                    makeAllocationFlags(allocationHint),
                    vma::MemoryUsage::eAutoPreferDevice,
                },
            };
        }

        std::pair<std::size_t, vk::DescriptorSetLayout>
        RenderDevice::createDescriptorSetLayout(const std::vector<vk::DescriptorSetLayoutBinding>& bindings)
        {
            assert(m_Device);

            std::size_t hash {0};
            for (const auto& b : bindings)
                hashCombine(hash, b);

            if (const auto it = m_DescriptorSetLayouts.find(hash); it != m_DescriptorSetLayouts.cend())
            {
                return {hash, it->second};
            }

            vk::DescriptorSetLayoutCreateInfo createInfo {};
            createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            createInfo.pBindings    = bindings.data();

            vk::DescriptorSetLayout descriptorSetLayout {nullptr};
            VK_CHECK(m_Device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout),
                     LOGTAG,
                     "Failed to create descriptor set layout");

            const auto& [inserted, _] = m_DescriptorSetLayouts.emplace(hash, descriptorSetLayout);
            return {hash, inserted->second};
        }

        PipelineLayout RenderDevice::createPipelineLayout(const PipelineLayoutInfo& layoutInfo)
        {
            assert(m_Device);

            std::size_t                          hash {0};
            std::vector<vk::DescriptorSetLayout> descriptorSetLayouts(kMinNumDescriptorSets);

            for (const auto& [set, bindings] : vultra::enumerate(layoutInfo.descriptorSets))
            {
                for (const auto& binding : bindings)
                {
                    hashCombine(hash, set, binding);
                }
                auto [_, handle]          = createDescriptorSetLayout(bindings);
                descriptorSetLayouts[set] = handle;
            }
            for (const auto& range : layoutInfo.pushConstantRanges)
                hashCombine(hash, range);

            if (const auto it = m_PipelineLayouts.find(hash); it != m_PipelineLayouts.cend())
            {
                return PipelineLayout {it->second, std::move(descriptorSetLayouts)};
            }

            vk::PipelineLayoutCreateInfo createInfo {};
            createInfo.setLayoutCount         = static_cast<uint32_t>(descriptorSetLayouts.size());
            createInfo.pSetLayouts            = descriptorSetLayouts.data();
            createInfo.pushConstantRangeCount = static_cast<uint32_t>(layoutInfo.pushConstantRanges.size());
            createInfo.pPushConstantRanges    = layoutInfo.pushConstantRanges.data();

            vk::PipelineLayout handle {nullptr};
            VK_CHECK(m_Device.createPipelineLayout(&createInfo, nullptr, &handle),
                     LOGTAG,
                     "Failed to create pipeline layout");

            const auto& [inserted, _] = m_PipelineLayouts.emplace(hash, handle);
            return PipelineLayout {inserted->second, std::move(descriptorSetLayouts)};
        }

        Texture RenderDevice::createTexture2D(const Extent2D    extent,
                                              const PixelFormat format,
                                              const uint32_t    numMipLevels,
                                              const uint32_t    numLayers,
                                              const ImageUsage  usageFlags) const
        {
            assert(m_MemoryAllocator);

            return Texture {
                m_MemoryAllocator,
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
            assert(m_MemoryAllocator);

            return Texture {
                m_MemoryAllocator,
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
            assert(m_MemoryAllocator);

            return Texture {
                m_MemoryAllocator,
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
            assert(texture && static_cast<bool>(texture.getUsageFlags() & ImageUsage::eSampled));

            if (const auto props = getFormatProperties(texture.getPixelFormat());
                !static_cast<bool>(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
            {
                samplerInfo.minFilter  = TexelFilter::eNearest;
                samplerInfo.magFilter  = TexelFilter::eNearest;
                samplerInfo.mipmapMode = MipmapMode::eNearest;
            }
            texture.setSampler(getSampler(samplerInfo));
            return *this;
        }

        vk::Sampler RenderDevice::getSampler(const SamplerInfo& samplerInfo)
        {
            const auto hash = std::hash<SamplerInfo> {}(samplerInfo);

            auto it = m_Samplers.find(hash);
            if (it == m_Samplers.cend())
            {
                it = m_Samplers.emplace(hash, createSampler(samplerInfo)).first;
                VULTRA_CORE_TRACE("[RenderDevice] Created Sampler {}", hash);
            }

            return it->second;
        }

        ShaderCompiler::Result
        RenderDevice::compile(const ShaderType                                                   shaderType,
                              const std::string_view                                             code,
                              const std::string_view                                             entryPointName,
                              const std::unordered_map<std::string, std::optional<std::string>>& defines) const
        {
            return m_ShaderCompiler.compile(shaderType, code, entryPointName, defines);
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
            assert(m_Device != nullptr);

            ShaderModule shaderModule {m_Device, spv};
            if (reflection)
                reflection->accumulate(std::move(spv));
            return shaderModule;
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

            vk::ComputePipelineCreateInfo createInfo {};
            createInfo.stage = vk::PipelineShaderStageCreateInfo {
                {}, vk::ShaderStageFlagBits::eCompute, static_cast<vk::ShaderModule>(shaderModule), "main"};
            createInfo.layout = pipelineLayout->getHandle();

            auto [result, computePipeline] = m_Device.createComputePipeline(m_PipelineCache, createInfo, nullptr);
            if (result != vk::Result::eSuccess)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to create compute pipeline: {}", vk::to_string(result));
                throw std::runtime_error("Failed to create compute pipeline");
            }

            return ComputePipeline {
                m_Device,
                std::move(pipelineLayout.value()),
                reflection ? reflection->localSize.value() : glm::uvec3 {},
                computePipeline,
            };
        }

        RenderDevice&
        RenderDevice::upload(Buffer& buffer, const vk::DeviceSize offset, const vk::DeviceSize size, const void* data)
        {
            assert(buffer && data);
            assert(m_Device);

            auto* mappedMemory = std::bit_cast<std::byte*>(buffer.map());
            std::memcpy(mappedMemory + offset, data, size);
            buffer.flush().unmap();
            return *this;
        }

        RenderDevice& RenderDevice::destroy(vk::Fence& fence)
        {
            assert(fence);
            assert(m_Device);

            m_Device.destroyFence(fence);
            fence = nullptr;
            return *this;
        }

        RenderDevice& RenderDevice::destroy(vk::Semaphore& semaphore)
        {
            assert(semaphore);
            assert(m_Device);

            m_Device.destroySemaphore(semaphore);
            semaphore = nullptr;
            return *this;
        }

        void RenderDevice::createXRDevice()
        {
            assert(static_cast<bool>(m_FeatureFlag & RenderDeviceFeatureFlagBits::eOpenXR));

            m_XRDevice = new openxr::XRDevice(openxr::XRDeviceFeatureFlagBits::eVR, m_AppName);
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
            appInfo.pApplicationName   = m_AppName.c_str();
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

            uint32_t    sdlExtensionCount = 0;
            const auto* sdlExtensions     = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
            requiredExtensions.reserve(sdlExtensionCount);
            for (uint32_t i = 0; i < sdlExtensionCount; ++i)
            {
                requiredExtensions.push_back(sdlExtensions[i]);
            }

#if _DEBUG
            requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

#ifdef __APPLE__
            requiredExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
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
                    }
                }
            }

            for (const auto& [extension, found] : extensionMap)
            {
                if (!found)
                {
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
                VULTRA_CORE_ERROR("[RenderDevice] Cannot find Validation Layer");
                throw std::runtime_error("Missing required layer");
            }
#endif

#ifndef VULTRA_ENABLE_RENDERDOC
            // If RenderDoc is enabled, we don't need to enable those layers.
            createInfo.enabledLayerCount   = static_cast<uint32_t>(enabledLayers.size());
            createInfo.ppEnabledLayerNames = enabledLayers.data();
#endif
            createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();

            // If enable OpenXR feature, then let OpenXR create the vulkan instance.
            if (static_cast<bool>(m_FeatureFlag & RenderDeviceFeatureFlagBits::eOpenXR))
            {
                VkInstance           vkInstanceC;
                VkInstanceCreateInfo createInfoC(createInfo);

                XrVulkanInstanceCreateInfoKHR xrVulkanInstanceCreateInfo {};
                xrVulkanInstanceCreateInfo.type                   = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
                xrVulkanInstanceCreateInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
                xrVulkanInstanceCreateInfo.systemId               = m_XRDevice->m_XrSystemId;
                xrVulkanInstanceCreateInfo.vulkanCreateInfo       = &createInfoC;
                VkResult vkResult;

                bool ok = true;
                if (XR_FAILED(m_XRDevice->xrCreateVulkanInstanceKHR(
                        m_XRDevice->m_XrInstance, &xrVulkanInstanceCreateInfo, &vkInstanceC, &vkResult)))
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

                m_Instance = vkInstanceC;
            }
            else
            {
                VK_CHECK(
                    vk::createInstance(&createInfo, nullptr, &m_Instance), LOGTAG, "Failed to create Vulkan instance");
            }

            VULKAN_HPP_DEFAULT_DISPATCHER.init(m_Instance);

#if _DEBUG
            setupDebugMessenger(m_Instance, m_DebugMessenger);
#endif
        }

        void RenderDevice::selectPhysicalDevice()
        {
            if (static_cast<bool>(m_FeatureFlag & RenderDeviceFeatureFlagBits::eOpenXR))
            {
                // Retrieve the physical device from OpenXR
                VkPhysicalDevice physicalDevice = nullptr;

                XrVulkanGraphicsDeviceGetInfoKHR vulkanGraphicsDeviceGetInfo {};
                vulkanGraphicsDeviceGetInfo.type           = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
                vulkanGraphicsDeviceGetInfo.systemId       = m_XRDevice->m_XrSystemId;
                vulkanGraphicsDeviceGetInfo.vulkanInstance = m_Instance;

                if (XR_FAILED(m_XRDevice->xrGetVulkanGraphicsDevice2KHR(
                        m_XRDevice->m_XrInstance, &vulkanGraphicsDeviceGetInfo, &physicalDevice)))
                {
                    VULTRA_CORE_ERROR("[RenderDevice] Failed to get Vulkan graphics device from OpenXR");
                    throw std::runtime_error("Failed to get Vulkan graphics device from OpenXR");
                }
                m_PhysicalDevice = physicalDevice;
                return;
            }

            auto               physicalDevices = m_Instance.enumeratePhysicalDevices();
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

            m_PhysicalDevice = bestDevice;
        }

        void RenderDevice::findGenericQueue()
        {
            constexpr vk::QueueFlags genericQueueFlags =
                vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;

            uint32_t count = 0;
            m_PhysicalDevice.getQueueFamilyProperties(&count, nullptr);
            assert(count > 0);
            std::vector<vk::QueueFamilyProperties> queueFamilies(count);
            m_PhysicalDevice.getQueueFamilyProperties(&count, queueFamilies.data());
            for (uint32_t i = 0; i < count; ++i)
            {
                if ((queueFamilies[i].queueFlags & genericQueueFlags) == genericQueueFlags)
                {
                    m_GenericQueueFamilyIndex = i;
                    break;
                }
            }

            if (m_GenericQueueFamilyIndex == -1)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to find a valid queue family index!");
                throw std::runtime_error("Failed to find a valid queue family index");
            }
        }

        void RenderDevice::createLogicalDevice()
        {
            constexpr float           queuePriority = 1.0f;
            vk::DeviceQueueCreateInfo queueCreateInfo {};
            queueCreateInfo.queueFamilyIndex = m_GenericQueueFamilyIndex;
            queueCreateInfo.queueCount       = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;

            std::vector extensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            };

            vk::PhysicalDeviceFeatures2        deviceFeatures2 {};
            std::vector<vk::BaseOutStructure*> featureChain;

            // Internal features
            // TODO: Default features
            vk::PhysicalDeviceFeatures enabledFeatures {};
            enabledFeatures.samplerAnisotropy = true;
#ifndef __APPLE__
            enabledFeatures.geometryShader            = true;
            enabledFeatures.shaderImageGatherExtended = true;
#endif
            deviceFeatures2.features = enabledFeatures;

#ifdef __APPLE__
            extensions.push_back("VK_KHR_portability_subset");

            // Dynamic Rendering & Synchronization2
            vk::PhysicalDeviceDynamicRenderingFeatures vkDynamicRenderingFeatures {};
            vkDynamicRenderingFeatures.dynamicRendering = true;
            vk::PhysicalDeviceSynchronization2FeaturesKHR vkSync2Features {};
            vkSync2Features.synchronization2 = true;
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vkDynamicRenderingFeatures));
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vkSync2Features));
#else
            // Dynamic Rendering & Synchronization2
            vk::PhysicalDeviceVulkan13Features vk13Features {};
            vk13Features.dynamicRendering = true;
            vk13Features.synchronization2 = true;
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vk13Features));
#endif

            // Raytracing
            vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures {};
            rayTracingFeatures.rayTracingPipeline = true;
            vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures {};
            if (HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eRaytracingPipeline))
            {
                extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
                extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
                extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

                featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&rayTracingFeatures));
                accelerationStructureFeatures.accelerationStructure = true;
                featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&accelerationStructureFeatures));
            }

            // Mesh Shader
            vk::PhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures {};
            meshShaderFeatures.meshShader = true;
            if (HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eMeshShader))
            {
                extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);

                featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&meshShaderFeatures));
            }

            // OpenXR
            if (HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eOpenXR))
            {
                // Add OpenXR specific features here
            }

            // Link the feature chain
            vk::BaseOutStructure* previous = nullptr;
            for (auto& feature : featureChain)
            {
                if (previous)
                {
                    previous->pNext = feature;
                }
                else
                {
                    deviceFeatures2.pNext = feature;
                }
                previous = feature;
            }

            // Check if extensions are supported
            const auto supportedExtensions = m_PhysicalDevice.enumerateDeviceExtensionProperties();
            for (const auto& extension : extensions)
            {
                bool found = false;
                for (const auto& supportedExtension : supportedExtensions)
                {
                    if (strcmp(extension, supportedExtension.extensionName) == 0)
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    VULTRA_CORE_ERROR("[RenderDevice] Failed to find required extension: {}", extension);
                    throw std::runtime_error("Missing required extension");
                }
            }

            vk::DeviceCreateInfo createInfo {};
            createInfo.queueCreateInfoCount    = 1;
            createInfo.pQueueCreateInfos       = &queueCreateInfo;
            createInfo.pNext                   = &deviceFeatures2;
            createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();

            // If enable OpenXR feature, then let OpenXR create the logical device.
            if (static_cast<bool>(m_FeatureFlag & RenderDeviceFeatureFlagBits::eOpenXR))
            {
                VkDeviceCreateInfo deviceCreateInfoC(createInfo);

                XrVulkanDeviceCreateInfoKHR xrVulkanDeviceCreateInfo {};
                xrVulkanDeviceCreateInfo.type                   = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
                xrVulkanDeviceCreateInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
                xrVulkanDeviceCreateInfo.systemId               = m_XRDevice->m_XrSystemId;
                xrVulkanDeviceCreateInfo.vulkanCreateInfo       = &deviceCreateInfoC;
                xrVulkanDeviceCreateInfo.vulkanPhysicalDevice   = m_PhysicalDevice;

                VkResult vkResult;
                VkDevice device = nullptr;

                bool ok = true;
                if (XR_FAILED(m_XRDevice->xrCreateVulkanDeviceKHR(
                        m_XRDevice->m_XrInstance, &xrVulkanDeviceCreateInfo, &device, &vkResult)))
                {
                    ok = false;
                }

                if (vkResult != VK_SUCCESS)
                {
                    ok = false;
                }

                if (!ok)
                {
                    VULTRA_CORE_ERROR("[RenderDevice] Failed to create Vulkan device from OpenXR");
                    throw std::runtime_error("Failed to create Vulkan device from OpenXR");
                }

                m_Device = device;
            }
            else
            {
                VK_CHECK(m_PhysicalDevice.createDevice(&createInfo, nullptr, &m_Device),
                         LOGTAG,
                         "Failed to create logical device");
            }

            // Get the generic queue
            m_Device.getQueue(m_GenericQueueFamilyIndex, 0, &m_GenericQueue);
        }

        void RenderDevice::createMemoryAllocator()
        {
            // https://github.com/YaaZ/VulkanMemoryAllocator-Hpp/issues/11#issuecomment-1237511514
            const vma::VulkanFunctions functions = vma::functionsFromDispatcher(VULKAN_HPP_DEFAULT_DISPATCHER);

            vma::AllocatorCreateInfo allocatorInfo {};
            allocatorInfo.physicalDevice   = m_PhysicalDevice;
            allocatorInfo.device           = m_Device;
            allocatorInfo.instance         = m_Instance;
            allocatorInfo.pVulkanFunctions = &functions;

            vma::Allocator allocator;
            VK_CHECK(vma::createAllocator(&allocatorInfo, &allocator), LOGTAG, "Failed to create memory allocator");

            m_MemoryAllocator = allocator;
        }

        void RenderDevice::createCommandPool()
        {
            vk::CommandPoolCreateInfo createInfo {};
            createInfo.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
            createInfo.queueFamilyIndex = m_GenericQueueFamilyIndex;
            VK_CHECK(m_Device.createCommandPool(&createInfo, nullptr, &m_CommandPool),
                     LOGTAG,
                     "Failed to create command pool");
        }

        void RenderDevice::createPipelineCache()
        {
            vk::PipelineCacheCreateInfo createInfo {};
            VK_CHECK(m_Device.createPipelineCache(&createInfo, nullptr, &m_PipelineCache),
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

            if (HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eRaytracingPipeline))
            {
                poolSizes.emplace_back(vk::DescriptorType::eStorageBufferDynamic, 100);
                poolSizes.emplace_back(vk::DescriptorType::eAccelerationStructureKHR, 100);
            }

            vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
            descriptorPoolCreateInfo.setPoolSizes(poolSizes);
            descriptorPoolCreateInfo.setMaxSets(100);
            descriptorPoolCreateInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
            m_DefaultDescriptorPool = m_Device.createDescriptorPool(descriptorPoolCreateInfo);
        }

        void RenderDevice::createTracyContext()
        {
#ifdef TRACY_ENABLE
            const auto cmdBuffer = allocateCommandBuffer();
            m_TracyContext       = TracyVkContext(m_PhysicalDevice, m_Device, m_GenericQueue, cmdBuffer);
            m_Device.freeCommandBuffers(m_CommandPool, 1, &cmdBuffer);
            const auto deviceName = getPhysicalDeviceInfo().deviceName;
            TracyVkContextName(m_TracyContext, deviceName.data(), static_cast<uint16_t>(deviceName.length()));
#endif
        }

        void RenderDevice::createTracky() { TRACKY_STARTUP(m_Device, 64 * 1024); }

        vk::CommandBuffer RenderDevice::allocateCommandBuffer() const
        {
            assert(m_Device);

            vk::CommandBufferAllocateInfo allocateInfo {};
            allocateInfo.commandPool        = m_CommandPool;
            allocateInfo.level              = vk::CommandBufferLevel::ePrimary;
            allocateInfo.commandBufferCount = 1;

            vk::CommandBuffer commandBuffer {nullptr};
            VK_CHECK(m_Device.allocateCommandBuffers(&allocateInfo, &commandBuffer),
                     LOGTAG,
                     "Failed to allocate command buffer");
            return commandBuffer;
        }

        vk::Sampler RenderDevice::createSampler(const SamplerInfo& samplerInfo) const
        {
            vk::SamplerCreateInfo samplerCreateInfo {};
            samplerCreateInfo.magFilter               = static_cast<vk::Filter>(samplerInfo.magFilter);
            samplerCreateInfo.minFilter               = static_cast<vk::Filter>(samplerInfo.minFilter);
            samplerCreateInfo.mipmapMode              = static_cast<vk::SamplerMipmapMode>(samplerInfo.mipmapMode);
            samplerCreateInfo.addressModeU            = static_cast<vk::SamplerAddressMode>(samplerInfo.addressModeS);
            samplerCreateInfo.addressModeV            = static_cast<vk::SamplerAddressMode>(samplerInfo.addressModeT);
            samplerCreateInfo.addressModeW            = static_cast<vk::SamplerAddressMode>(samplerInfo.addressModeR);
            samplerCreateInfo.minLod                  = samplerInfo.minLod;
            samplerCreateInfo.maxLod                  = samplerInfo.maxLod;
            samplerCreateInfo.mipLodBias              = 0.0f;
            samplerCreateInfo.borderColor             = static_cast<vk::BorderColor>(samplerInfo.borderColor);
            samplerCreateInfo.unnormalizedCoordinates = false;
            samplerCreateInfo.compareEnable           = samplerInfo.compareOp.has_value();
            samplerCreateInfo.compareOp = static_cast<vk::CompareOp>(samplerInfo.compareOp.value_or(CompareOp::eLess));
            samplerCreateInfo.anisotropyEnable = samplerInfo.maxAnisotropy.has_value();
            samplerCreateInfo.maxAnisotropy =
                samplerInfo.maxAnisotropy ?
                    glm::clamp(*samplerInfo.maxAnisotropy, 1.0f, getDeviceLimits().maxSamplerAnisotropy) :
                    0.0f;

            vk::Sampler sampler {nullptr};
            VK_CHECK(m_Device.createSampler(&samplerCreateInfo, nullptr, &sampler), LOGTAG, "Failed to create sampler");

            return sampler;
        }

        CommandBuffer RenderDevice::createCommandBuffer() const
        {
            return CommandBuffer {
                m_Device,
                m_CommandPool,
                allocateCommandBuffer(),
                m_TracyContext,
                createFence(),
            };
        }

        RenderDevice& RenderDevice::execute(const std::function<void(CommandBuffer&)>& f)
        {
            auto cb = createCommandBuffer();
            cb.begin();
            // TRACKY_VK_NEXT_FRAME(cb);
            {
                TRACY_GPU_ZONE(cb, "ExecuteCommandBuffer");
                // TRACKY_GPU_ZONE(cb, "ExecuteCommandBuffer");
                std::invoke(f, cb);
            }
            return execute(cb);
        }

        RenderDevice& RenderDevice::execute(CommandBuffer& cb, const JobInfo& jobInfo)
        {
            cb.flushBarriers();
            TracyVkCollect(m_TracyContext, cb.m_Handle);
            cb.end();
            assert(cb.invariant(CommandBuffer::State::eExecutable));

            vk::CommandBufferSubmitInfo commandBufferInfo {};
            commandBufferInfo.commandBuffer = cb.m_Handle;

            vk::SemaphoreSubmitInfo waitSemaphoreInfo {};
            waitSemaphoreInfo.semaphore = jobInfo.wait;
            waitSemaphoreInfo.stageMask = jobInfo.waitStage;

            vk::SemaphoreSubmitInfo signalSemaphoreInfo {};
            signalSemaphoreInfo.semaphore = jobInfo.signal;

            vk::SubmitInfo2 submitInfo {};
            submitInfo.waitSemaphoreInfoCount   = jobInfo.wait != nullptr ? 1u : 0u;
            submitInfo.pWaitSemaphoreInfos      = jobInfo.wait ? &waitSemaphoreInfo : nullptr;
            submitInfo.commandBufferInfoCount   = 1;
            submitInfo.pCommandBufferInfos      = &commandBufferInfo;
            submitInfo.signalSemaphoreInfoCount = jobInfo.signal != nullptr ? 1u : 0u;
            submitInfo.pSignalSemaphoreInfos    = jobInfo.signal ? &signalSemaphoreInfo : nullptr;

            VK_CHECK(m_GenericQueue.submit2KHR(1, &submitInfo, cb.m_Fence), LOGTAG, "Failed to submit command buffer");

            cb.m_State = CommandBuffer::State::ePending;
            return *this;
        }

        RenderDevice& RenderDevice::present(Swapchain& swapchain, const vk::Semaphore wait)
        {
            ZoneScopedN("RHI::Present");

            assert(swapchain);
            assert(m_GenericQueue);

            vk::PresentInfoKHR presentInfo {};
            presentInfo.waitSemaphoreCount = wait != nullptr ? 1u : 0u;
            presentInfo.pWaitSemaphores    = &wait;
            presentInfo.swapchainCount     = 1;
            presentInfo.pSwapchains        = &swapchain.m_Handle;
            presentInfo.pImageIndices      = &swapchain.m_CurrentImageIndex;

            auto result = m_GenericQueue.presentKHR(&presentInfo);
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

            if (result != vk::Result::eErrorOutOfDateKHR)
            {
                m_GenericQueue.waitIdle();
            }

            return *this;
        }

        RenderDevice& RenderDevice::wait(const vk::Fence fence)
        {
            assert(fence);
            assert(m_Device);
            VK_CHECK(m_Device.waitForFences(1, &fence, true, std::numeric_limits<uint64_t>::max()),
                     LOGTAG,
                     "Failed to wait for fence");
            return reset(fence);
        }

        RenderDevice& RenderDevice::reset(const vk::Fence fence)
        {
            assert(fence);
            assert(m_Device);
            VK_CHECK(m_Device.resetFences(1, &fence), LOGTAG, "Failed to reset fence");
            return *this;
        }

        RenderDevice& RenderDevice::waitIdle()
        {
            assert(m_Device);
            m_Device.waitIdle();
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
                    .image     = texture,
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .stageMask  = rhi::PipelineStages::eTransfer,
                    .accessMask = rhi::Access::eTransferRead,
                });

            auto stagingBuffer = createStagingBuffer(texture.getSize());

            cb.copyImage(texture, stagingBuffer, imageAspect);

            // Wait for the copy to complete
            cb.getBarrierBuilder().bufferBarrier({.buffer = stagingBuffer},
                                                 {
                                                     .stageMask  = rhi::PipelineStages::eTransfer,
                                                     .accessMask = rhi::Access::eTransferRead,
                                                 });

            execute(cb, JobInfo {});
            m_GenericQueue.waitIdle();

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

        uint64_t RenderDevice::getBufferDeviceAddress(const Buffer& buffer) const
        {
            assert(m_Device);
            vk::BufferDeviceAddressInfo bufferDeviceAddressInfo {};
            bufferDeviceAddressInfo.buffer = buffer.getHandle();
            return m_Device.getBufferAddress(bufferDeviceAddressInfo);
        }

        uint64_t RenderDevice::getAccelerationStructureDeviceAddress(const AccelerationStructure& accel) const
        {
            assert(m_Device);
            vk::AccelerationStructureDeviceAddressInfoKHR addressInfo {};
            addressInfo.accelerationStructure = accel.getHandle();
            return m_Device.getAccelerationStructureAddressKHR(addressInfo);
        }
    } // namespace rhi
} // namespace vultra