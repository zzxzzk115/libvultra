#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/base/ranges.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/raytracing/raytracing_pipeline.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/core/rhi/vk/macro.hpp"
#include "vultra/function/openxr/xr_device.hpp"

#include <SDL3/SDL_vulkan.h>
#include <cpptrace/cpptrace.hpp>
#include <glm/glm.hpp>
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
            hashCombine(h, v.binding, v.flags);
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
    [[nodiscard]] vk::IndexType toVk(const vultra::rhi::IndexType indexType)
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
} // namespace

namespace vultra
{
    namespace rhi
    {
        constexpr auto LOGTAG = "RenderDevice";

        RenderDevice::RenderDevice(const RenderDeviceFeatureFlagBits featureFlag, std::string_view appName) :
            m_FeatureFlag(featureFlag), m_AppName(appName)
        {
            if (HasFlagValues(featureFlag, RenderDeviceFeatureFlagBits::eOpenXR))
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

            for (auto& texture : m_LoadedTextures)
            {
                texture.reset();
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

            if (HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eOpenXR))
            {
                delete m_XRDevice;
            }
        }

        RenderDeviceFeatureFlagBits RenderDevice::getFeatureFlag() const { return m_FeatureFlag; }

        RenderDeviceFeatureReport RenderDevice::getFeatureReport() const { return m_FeatureReport; }

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

            vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;

            if (isRaytracingOrRayQueryEnabled(m_FeatureFlag))
            {
                usage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
            }

            if (isRaytracingOrRayQueryEnabled(m_FeatureFlag) ||
                HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eMeshShader))
            {
                usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
            }

            return VertexBuffer {
                Buffer {
                    m_MemoryAllocator,
                    stride * capacity,
                    usage,
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

            vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;

            if (isRaytracingOrRayQueryEnabled(m_FeatureFlag))
            {
                usage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
            }

            if (isRaytracingOrRayQueryEnabled(m_FeatureFlag) ||
                HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eMeshShader))
            {
                usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
            }

            return IndexBuffer {
                Buffer {
                    m_MemoryAllocator,
                    static_cast<uint8_t>(indexType) * capacity,
                    usage,
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

            vk::BufferUsageFlags usage =
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;

            if (isRaytracingOrRayQueryEnabled(m_FeatureFlag))
            {
                usage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
            }

            if (isRaytracingOrRayQueryEnabled(m_FeatureFlag) ||
                HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eMeshShader))
            {
                usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
            }

            return StorageBuffer {
                Buffer {
                    m_MemoryAllocator,
                    size,
                    usage,
                    makeAllocationFlags(allocationHint),
                    vma::MemoryUsage::eAutoPreferDevice,
                },
            };
        }

        std::pair<std::size_t, vk::DescriptorSetLayout>
        RenderDevice::createDescriptorSetLayout(const std::vector<DescriptorSetLayoutBindingEx>& bindings)
        {
            assert(m_Device);

            std::size_t hash {0};
            for (const auto& b : bindings)
                hashCombine(hash, b);

            if (const auto it = m_DescriptorSetLayouts.find(hash); it != m_DescriptorSetLayouts.cend())
            {
                return {hash, it->second};
            }

            std::vector<vk::DescriptorSetLayoutBinding> vkBindings;
            vkBindings.reserve(bindings.size());
            for (const auto& b : bindings)
                vkBindings.push_back(b.binding);

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
            assert(texture && HasFlagValues(texture.getUsageFlags(), ImageUsage::eSampled));

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
            assert(HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eOpenXR));

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
            if (HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eOpenXR))
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
            if (HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eOpenXR))
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
            }
            else
            {
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

            // Query properties and features
            // Properties
            vk::StructureChain<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>
                propertyChain;

            m_PhysicalDevice.getProperties2(&propertyChain.get<vk::PhysicalDeviceProperties2>());

            m_RayTracingPipelineProperties = propertyChain.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

            // Features
            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceAccelerationStructureFeaturesKHR>
                featureChain;

            m_PhysicalDevice.getFeatures2(&featureChain.get<vk::PhysicalDeviceFeatures2>());

            m_AccelerationStructureFeatures = featureChain.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
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

            // === Base extensions ===
            std::vector<const char*> extensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            };

            // === Feature structs ===
            vk::PhysicalDeviceFeatures2        deviceFeatures2 {};
            std::vector<vk::BaseOutStructure*> featureChain;

            vk::PhysicalDeviceFeatures enabledFeatures {};
            enabledFeatures.samplerAnisotropy = VK_TRUE;
#ifndef __APPLE__
            enabledFeatures.geometryShader            = VK_TRUE;
            enabledFeatures.shaderImageGatherExtended = VK_TRUE;
            enabledFeatures.shaderInt64               = VK_TRUE;
#endif
            deviceFeatures2.features = enabledFeatures;

#ifdef __APPLE__
            extensions.push_back("VK_KHR_portability_subset");

            // Dynamic Rendering & Synchronization2
            vk::PhysicalDeviceDynamicRenderingFeatures vkDynamicRenderingFeatures {};
            vkDynamicRenderingFeatures.dynamicRendering = VK_TRUE;
            vk::PhysicalDeviceSynchronization2FeaturesKHR vkSync2Features {};
            vkSync2Features.synchronization2 = VK_TRUE;
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vkDynamicRenderingFeatures));
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vkSync2Features));
#else
            // Dynamic Rendering & Synchronization2
            vk::PhysicalDeviceVulkan13Features vk13Features {};
            vk13Features.dynamicRendering = VK_TRUE;
            vk13Features.synchronization2 = VK_TRUE;
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vk13Features));
#endif

            // Vulkan 1.2 features
            vk::PhysicalDeviceVulkan12Features vk12Features {};
            vk12Features.bufferDeviceAddress                       = VK_TRUE;
            vk12Features.scalarBlockLayout                         = VK_TRUE;
            vk12Features.descriptorIndexing                        = VK_TRUE;
            vk12Features.descriptorBindingVariableDescriptorCount  = VK_TRUE;
            vk12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            vk12Features.runtimeDescriptorArray                    = VK_TRUE;
            vk12Features.storageBuffer8BitAccess                   = VK_TRUE;
            vk12Features.timelineSemaphore                         = VK_TRUE;
#ifdef VULTRA_ENABLE_RENDERDOC
            vk12Features.bufferDeviceAddressCaptureReplay = VK_TRUE;
#endif
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&vk12Features));

            // Ray Tracing & Ray Query
            vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures {};
            extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            accelerationStructureFeatures.accelerationStructure = VK_TRUE;
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&accelerationStructureFeatures));

            vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures {};
            extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            rayQueryFeatures.rayQuery = VK_TRUE;
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&rayQueryFeatures));

            vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures {};
            extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            rayTracingFeatures.rayTracingPipeline = VK_TRUE;
#ifdef VULTRA_ENABLE_RENDERDOC
            rayTracingFeatures.rayTracingPipelineShaderGroupHandleCaptureReplay = VK_TRUE;
#endif
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&rayTracingFeatures));

            // Mesh Shaders
            vk::PhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures {};
            extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
            meshShaderFeatures.meshShader = VK_TRUE;
            meshShaderFeatures.taskShader = VK_TRUE;
            featureChain.push_back(reinterpret_cast<vk::BaseOutStructure*>(&meshShaderFeatures));

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

            // === Check supported extensions ===
            const auto                      supportedExtensions = m_PhysicalDevice.enumerateDeviceExtensionProperties();
            std::unordered_set<std::string> supportedExtNames;
            for (const auto& e : supportedExtensions)
                supportedExtNames.insert(e.extensionName);

            // === Query Vulkan version ===
            const auto     props      = m_PhysicalDevice.getProperties();
            const uint32_t apiVersion = props.apiVersion;
            const uint32_t apiMajor   = VK_API_VERSION_MAJOR(apiVersion);
            const uint32_t apiMinor   = VK_API_VERSION_MINOR(apiVersion);

            // === Build feature report ===
            m_FeatureReport.deviceName = props.deviceName.data();
            m_FeatureReport.apiMajor   = apiMajor;
            m_FeatureReport.apiMinor   = apiMinor;
            m_FeatureReport.apiPatch   = VK_API_VERSION_PATCH(apiVersion);
            auto& f                    = m_FeatureReport.flags;

            auto add = [&](RenderDeviceFeatureReportFlagBits bit, const char* name) {
                if (supportedExtNames.count(name))
                    f |= bit;
                else
                    VULTRA_CORE_WARN("[RenderDevice] Extension not supported: {}", name);
            };

            add(RenderDeviceFeatureReportFlagBits::eRayTracingPipeline, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            add(RenderDeviceFeatureReportFlagBits::eRayQuery, VK_KHR_RAY_QUERY_EXTENSION_NAME);
            add(RenderDeviceFeatureReportFlagBits::eAccelerationStructure,
                VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            add(RenderDeviceFeatureReportFlagBits::eMeshShader, VK_EXT_MESH_SHADER_EXTENSION_NAME);
            add(RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            add(RenderDeviceFeatureReportFlagBits::eDescriptorIndexing, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
            add(RenderDeviceFeatureReportFlagBits::eDynamicRendering, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            add(RenderDeviceFeatureReportFlagBits::eSynchronization2, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
            add(RenderDeviceFeatureReportFlagBits::eTimelineSemaphore, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

            // === Filter unsupported extensions ===
            std::vector<const char*> filteredExtensions;
            for (const auto* ext : extensions)
            {
                if (supportedExtNames.count(ext))
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

            bool useOpenXR = (m_XRDevice && m_XRDevice->m_XrInstance != XR_NULL_HANDLE);

            if (useOpenXR)
            {
                VULTRA_CORE_INFO("[RenderDevice] Creating Vulkan device via OpenXR runtime");
                m_FeatureReport.flags |= RenderDeviceFeatureReportFlagBits::eOpenXR;

                VkDeviceCreateInfo          deviceCreateInfoC(createInfo);
                XrVulkanDeviceCreateInfoKHR xrVulkanDeviceCreateInfo {};
                xrVulkanDeviceCreateInfo.type                   = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
                xrVulkanDeviceCreateInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
                xrVulkanDeviceCreateInfo.systemId               = m_XRDevice->m_XrSystemId;
                xrVulkanDeviceCreateInfo.vulkanCreateInfo       = &deviceCreateInfoC;
                xrVulkanDeviceCreateInfo.vulkanPhysicalDevice   = m_PhysicalDevice;

                VkResult vkResult = VK_SUCCESS;
                VkDevice device   = VK_NULL_HANDLE;

                if (XR_FAILED(m_XRDevice->xrCreateVulkanDeviceKHR(
                        m_XRDevice->m_XrInstance, &xrVulkanDeviceCreateInfo, &device, &vkResult)) ||
                    vkResult != VK_SUCCESS)
                {
                    VULTRA_CORE_ERROR("[RenderDevice] OpenXR Vulkan device creation failed (VkResult: {})",
                                      vk::to_string(static_cast<vk::Result>(vkResult)));
                    VULTRA_CORE_WARN("[RenderDevice] Falling back to standard Vulkan device creation");
                    VK_CHECK(m_PhysicalDevice.createDevice(&createInfo, nullptr, &m_Device),
                             LOGTAG,
                             "Failed to create logical device");
                }
                else
                {
                    m_Device = device;
                }
            }
            else
            {
                VULTRA_CORE_INFO("[RenderDevice] Creating standalone Vulkan device (non-XR)");
                VK_CHECK(m_PhysicalDevice.createDevice(&createInfo, nullptr, &m_Device),
                         LOGTAG,
                         "Failed to create logical device");
            }

            // === Get Generic Queue (for both graphics & compute) ===
            m_Device.getQueue(m_GenericQueueFamilyIndex, 0, &m_GenericQueue);

            // === Log summary ===
            VULTRA_CORE_INFO("[RenderDevice] Created logical device: {}", m_FeatureReport.deviceName);
            VULTRA_CORE_INFO("  Vulkan API: {}.{}.{}", apiMajor, apiMinor, VK_API_VERSION_PATCH(apiVersion));

#define PRINT_FEATURE(f) \
    VULTRA_CORE_INFO( \
        "   {:<30} {}", #f, HasFlagValues(m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::f) ? "yes" : "no")
            VULTRA_CORE_INFO("[RenderDevice] Feature support report:");
            PRINT_FEATURE(eRayTracingPipeline);
            PRINT_FEATURE(eRayQuery);
            PRINT_FEATURE(eAccelerationStructure);
            PRINT_FEATURE(eMeshShader);
            PRINT_FEATURE(eBufferDeviceAddress);
            PRINT_FEATURE(eDescriptorIndexing);
            PRINT_FEATURE(eDynamicRendering);
            PRINT_FEATURE(eSynchronization2);
            PRINT_FEATURE(eTimelineSemaphore);
#undef PRINT_FEATURE

            // === Assign & Check Feature Flags ===
            auto availableFeatureFlag = RenderDeviceFeatureFlagBits::eNormal;
            if (HasFlagValues(m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eAccelerationStructure) &&
                HasFlagValues(m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eRayTracingPipeline))
            {
                availableFeatureFlag |= RenderDeviceFeatureFlagBits::eRayTracingPipeline;
            }
            if (HasFlagValues(m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eAccelerationStructure) &&
                HasFlagValues(m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eRayQuery))
            {
                availableFeatureFlag |= RenderDeviceFeatureFlagBits::eRayQuery;
            }
            if (HasFlagValues(m_FeatureReport.flags, RenderDeviceFeatureReportFlagBits::eMeshShader))
            {
                availableFeatureFlag |= RenderDeviceFeatureFlagBits::eMeshShader;
            }
            if (useOpenXR)
            {
                availableFeatureFlag |= RenderDeviceFeatureFlagBits::eOpenXR;
            }

            if (!HasFlagValues(availableFeatureFlag, m_FeatureFlag) &&
                m_FeatureFlag != RenderDeviceFeatureFlagBits::eNormal)
            {
                throw std::runtime_error("Requested features are not supported by the physical device");
            }
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

            if (isRaytracingOrRayQueryEnabled(m_FeatureFlag) ||
                HasFlagValues(m_FeatureFlag, RenderDeviceFeatureFlagBits::eMeshShader))
            {
                // When using raytracing/query or mesh shading, enable the buffer device address feature in VMA
                allocatorInfo.flags |= vma::AllocatorCreateFlagBits::eBufferDeviceAddress;
            }

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

            if (isRaytracingOrRayQueryEnabled(m_FeatureFlag))
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
            return CommandBuffer {m_Device,
                                  m_CommandPool,
                                  allocateCommandBuffer(),
                                  m_TracyContext,
                                  createFence(),
                                  isRaytracingOrRayQueryEnabled(m_FeatureFlag)};
        }

        RenderDevice& RenderDevice::execute(const std::function<void(CommandBuffer&)>& f, bool oneTime)
        {
            auto cb = createCommandBuffer();
            cb.begin();
            // TRACKY_VK_NEXT_FRAME(cb);
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

            if (oneTime)
            {
                VK_CHECK(m_Device.waitForFences(cb.m_Fence, VK_TRUE, UINT64_MAX), LOGTAG, "Failed to wait for fence");
                m_Device.resetFences(cb.m_Fence);
                cb.m_State = CommandBuffer::State::eInitial;
            }
            else
            {
                cb.m_State = CommandBuffer::State::ePending;
            }

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
            VK_CHECK(m_Device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
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

        AccelerationStructure
        RenderDevice::createAccelerationStructure(AccelerationStructureType           type,
                                                  AccelerationStructureBuildSizesInfo buildSizesInfo) const
        {
            assert(m_Device);

            auto buffer = createAccelerationStructureBuffer(buildSizesInfo.accelerationStructureSize);

            vk::AccelerationStructureCreateInfoKHR createInfo {};
            createInfo.type   = static_cast<vk::AccelerationStructureTypeKHR>(type);
            createInfo.size   = buildSizesInfo.accelerationStructureSize;
            createInfo.buffer = buffer.getHandle();
            createInfo.offset = 0;

            vk::AccelerationStructureKHR handle {nullptr};
            VK_CHECK(m_Device.createAccelerationStructureKHR(&createInfo, nullptr, &handle),
                     LOGTAG,
                     "Failed to create acceleration structure");

            vk::AccelerationStructureDeviceAddressInfoKHR addressInfo {};
            addressInfo.accelerationStructure = handle;
            uint64_t deviceAddress            = m_Device.getAccelerationStructureAddressKHR(addressInfo);

            return AccelerationStructure {
                m_Device, handle, deviceAddress, type, std::move(buildSizesInfo), std::move(buffer)};
        }

        AccelerationStructure RenderDevice::createBuildSingleGeometryBLAS(uint64_t vertexBufferAddress,
                                                                          uint64_t indexBufferAddress,
                                                                          uint64_t transformBufferAddress,
                                                                          uint32_t vertexStride,
                                                                          uint32_t vertexCount,
                                                                          uint32_t indexCount)
        {
            VULTRA_CORE_ASSERT(isRaytracingOrRayQueryEnabled(m_FeatureFlag),
                               "[RenderDevice] Raytracing Pipeline feature is not enabled!");
            VULTRA_CORE_ASSERT(m_AccelerationStructureFeatures.accelerationStructure,
                               "[RenderDevice] Acceleration Structure feature is not enabled!");

            // Describe the geometry
            vk::AccelerationStructureGeometryTrianglesDataKHR triangles {};
            triangles.vertexFormat                = vk::Format::eR32G32B32Sfloat;
            triangles.vertexData.deviceAddress    = vertexBufferAddress;
            triangles.vertexStride                = vertexStride;
            triangles.indexType                   = vk::IndexType::eUint32;
            triangles.indexData.deviceAddress     = indexBufferAddress;
            triangles.transformData.deviceAddress = transformBufferAddress;
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

            auto buildSizes = m_Device.getAccelerationStructureBuildSizesKHR(
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
            buildGeometryInfo.dstAccelerationStructure  = blas.getHandle();
            buildGeometryInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer);
            std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> buildRangeInfos = {&buildRangeInfo};

            // Build the BLAS using a one-time command buffer
            execute(
                [&](CommandBuffer& cb) {
                    cb.getHandle().buildAccelerationStructuresKHR(1, &buildGeometryInfo, buildRangeInfos.data());
                },
                true);

            return blas;
        }

        AccelerationStructure RenderDevice::createBuildRenderMeshBLAS(std::vector<RenderSubMesh>& subMeshes)
        {
            VULTRA_CORE_ASSERT(isRaytracingOrRayQueryEnabled(m_FeatureFlag),
                               "[RenderDevice] Raytracing Pipeline feature is not enabled!");
            VULTRA_CORE_ASSERT(m_AccelerationStructureFeatures.accelerationStructure,
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
                triangles.vertexData.deviceAddress    = sm.vertexBufferAddress;
                triangles.vertexStride                = sm.vertexStride;
                triangles.indexType                   = toVk(sm.indexType);
                triangles.indexData.deviceAddress     = sm.indexBufferAddress;
                triangles.transformData.deviceAddress = sm.transformBufferAddress;
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

            auto buildSizes = m_Device.getAccelerationStructureBuildSizesKHR(
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
            buildGeometryInfo.dstAccelerationStructure  = blas.getHandle();
            buildGeometryInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer);

            std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> buildRangePtrs;
            buildRangePtrs.reserve(buildRanges.size());
            for (auto& r : buildRanges)
                buildRangePtrs.push_back(&r);

            // Build the BLAS using a one-time command buffer
            execute(
                [&](CommandBuffer& cb) {
                    cb.getHandle().buildAccelerationStructuresKHR(1, &buildGeometryInfo, buildRangePtrs.data());
                },
                true);

            return blas;
        }

        AccelerationStructure RenderDevice::createBuildSingleInstanceTLAS(const AccelerationStructure& referenceBLAS,
                                                                          const glm::mat4&             transform)
        {
            VULTRA_CORE_ASSERT(isRaytracingOrRayQueryEnabled(m_FeatureFlag),
                               "[RenderDevice] Raytracing Pipeline feature is not enabled!");
            VULTRA_CORE_ASSERT(m_AccelerationStructureFeatures.accelerationStructure,
                               "[RenderDevice] Acceleration Structure feature is not enabled!");

            vk::TransformMatrixKHR vkTransform {};
            glm::mat4              rowMajor = glm::transpose(transform);
            memcpy(&vkTransform, &rowMajor, sizeof(vk::TransformMatrixKHR));

            vk::AccelerationStructureInstanceKHR instance {};
            instance.transform                              = vkTransform;
            instance.instanceCustomIndex                    = 0;
            instance.mask                                   = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags =
                static_cast<VkGeometryInstanceFlagsKHR>(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
            instance.accelerationStructureReference = referenceBLAS.getDeviceAddress();

            auto  instancesBuffer = createInstancesBuffer(1);
            void* mapped          = instancesBuffer.map();
            memcpy(mapped, &instance, sizeof(vk::AccelerationStructureInstanceKHR));
            instancesBuffer.unmap();

            vk::DeviceOrHostAddressConstKHR instanceData {};
            instanceData.deviceAddress = getBufferDeviceAddress(instancesBuffer);

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

            auto buildSizes = m_Device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, rangeInfo.primitiveCount);

            AccelerationStructureBuildSizesInfo sizesInfo {};
            sizesInfo.accelerationStructureSize = buildSizes.accelerationStructureSize;
            sizesInfo.buildScratchSize          = buildSizes.buildScratchSize;
            sizesInfo.updateScratchSize         = buildSizes.updateScratchSize;

            auto tlas          = createAccelerationStructure(AccelerationStructureType::eTopLevel, sizesInfo);
            auto scratchBuffer = createScratchBuffer(buildSizes.buildScratchSize);

            buildInfo.dstAccelerationStructure                              = tlas.getHandle();
            buildInfo.scratchData.deviceAddress                             = getBufferDeviceAddress(scratchBuffer);
            std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> ranges = {&rangeInfo};

            execute(
                [&](CommandBuffer& cb) { cb.getHandle().buildAccelerationStructuresKHR(1, &buildInfo, ranges.data()); },
                true);

            return tlas;
        }

        AccelerationStructure
        RenderDevice::createBuildMultipleInstanceTLAS(const std::vector<RayTracingInstance>& instances)
        {
            VULTRA_CORE_ASSERT(isRaytracingOrRayQueryEnabled(m_FeatureFlag),
                               "[RenderDevice] Raytracing Pipeline feature is not enabled!");
            VULTRA_CORE_ASSERT(m_AccelerationStructureFeatures.accelerationStructure,
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
                vkInstance.flags                                  = static_cast<VkGeometryInstanceFlagsKHR>(inst.flags);
                vkInstance.accelerationStructureReference         = inst.blas->getDeviceAddress();

                vkInstances.push_back(vkInstance);
            }

            auto  instancesBuffer = createInstancesBuffer(static_cast<uint32_t>(vkInstances.size()));
            void* mapped          = instancesBuffer.map();
            memcpy(mapped, vkInstances.data(), sizeof(vk::AccelerationStructureInstanceKHR) * vkInstances.size());
            instancesBuffer.unmap();

            vk::DeviceOrHostAddressConstKHR instanceData {};
            instanceData.deviceAddress = getBufferDeviceAddress(instancesBuffer);

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

            auto buildSizes = m_Device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, rangeInfo.primitiveCount);

            AccelerationStructureBuildSizesInfo sizesInfo {};
            sizesInfo.accelerationStructureSize = buildSizes.accelerationStructureSize;
            sizesInfo.buildScratchSize          = buildSizes.buildScratchSize;
            sizesInfo.updateScratchSize         = buildSizes.updateScratchSize;

            auto tlas          = createAccelerationStructure(AccelerationStructureType::eTopLevel, sizesInfo);
            auto scratchBuffer = createScratchBuffer(buildSizes.buildScratchSize);

            buildInfo.dstAccelerationStructure                              = tlas.getHandle();
            buildInfo.scratchData.deviceAddress                             = getBufferDeviceAddress(scratchBuffer);
            std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> ranges = {&rangeInfo};

            execute(
                [&](CommandBuffer& cb) { cb.getHandle().buildAccelerationStructuresKHR(1, &buildInfo, ranges.data()); },
                true);

            return tlas;
        }

        ScratchBuffer RenderDevice::createScratchBuffer(uint64_t size, AllocationHints allocationHint) const
        {
            assert(m_MemoryAllocator);

            Buffer buffer {
                m_MemoryAllocator,
                size,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                makeAllocationFlags(allocationHint),
                vma::MemoryUsage::eAutoPreferDevice,
            };

            uint64_t bufferAddress = getBufferDeviceAddress(buffer);

            return ScratchBuffer {std::move(buffer), bufferAddress};
        }

        InstanceBuffer RenderDevice::createInstancesBuffer(uint32_t instanceCount, AllocationHints allocationHint) const
        {
            assert(m_MemoryAllocator);

            return InstanceBuffer {
                m_MemoryAllocator,
                instanceCount * sizeof(VkAccelerationStructureInstanceKHR),
                vk::BufferUsageFlagBits::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                makeAllocationFlags(allocationHint),
                vma::MemoryUsage::eCpuToGpu, // Host visible & coherent for easy mapping
            };
        }

        TransformBuffer RenderDevice::createTransformBuffer(AllocationHints allocationHint) const
        {
            assert(m_MemoryAllocator);

            return TransformBuffer {
                m_MemoryAllocator,
                sizeof(vk::TransformMatrixKHR),
                vk::BufferUsageFlagBits::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                makeAllocationFlags(allocationHint),
                vma::MemoryUsage::eCpuToGpu, // Host visible & coherent for easy mapping
            };
        }

        ShaderBindingTable RenderDevice::createShaderBindingTable(const rhi::RayTracingPipeline& pipeline,
                                                                  AllocationHints                allocationHint) const
        {
            assert(m_MemoryAllocator);

            const auto& props = m_RayTracingPipelineProperties;

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
            Buffer sbtBuffer {
                m_MemoryAllocator,
                sbtSize,
                vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits::eTransferDst,
                makeAllocationFlags(allocationHint),
                vma::MemoryUsage::eCpuToGpu, // Host visible & coherent for easy mapping
            };

            std::vector<uint8_t> handles(sbtSize);
            VK_CHECK(m_Device.getRayTracingShaderGroupHandlesKHR(
                         pipeline.getHandle(), 0, pipeline.getGroupCount(), handles.size(), handles.data()),
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
                return getSbtEntryStrideDeviceAddressRegion(sbtBuffer, count, offset);
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

        uint64_t RenderDevice::getBufferDeviceAddress(const Buffer& buffer) const
        {
            assert(m_Device);
            vk::BufferDeviceAddressInfo bufferDeviceAddressInfo {};
            bufferDeviceAddressInfo.buffer = buffer.getHandle();
            return m_Device.getBufferAddress(bufferDeviceAddressInfo);
        }

        RayTracingPipelineProperties RenderDevice::getRayTracingPipelineProperties() const
        {
            return RayTracingPipelineProperties {
                m_RayTracingPipelineProperties.shaderGroupHandleSize,
                m_RayTracingPipelineProperties.maxRayRecursionDepth,
                m_RayTracingPipelineProperties.maxShaderGroupStride,
                m_RayTracingPipelineProperties.shaderGroupBaseAlignment,
                m_RayTracingPipelineProperties.shaderGroupHandleCaptureReplaySize,
                m_RayTracingPipelineProperties.maxRayDispatchInvocationCount,
                m_RayTracingPipelineProperties.shaderGroupHandleAlignment,
                m_RayTracingPipelineProperties.maxRayHitAttributeSize,
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

        uint64_t RenderDevice::getAccelerationStructureDeviceAddress(const AccelerationStructure& accel) const
        {
            assert(m_Device);
            vk::AccelerationStructureDeviceAddressInfoKHR addressInfo {};
            addressInfo.accelerationStructure = accel.getHandle();
            return m_Device.getAccelerationStructureAddressKHR(addressInfo);
        }

        StrideDeviceAddressRegion RenderDevice::getSbtEntryStrideDeviceAddressRegion(const Buffer& sbt,
                                                                                     uint32_t      handleCount,
                                                                                     uint64_t      offset) const
        {
            const uint32_t handleSizeAligned = alignedSize(m_RayTracingPipelineProperties.shaderGroupHandleSize,
                                                           m_RayTracingPipelineProperties.shaderGroupHandleAlignment);

            return StrideDeviceAddressRegion {
                .deviceAddress = getBufferDeviceAddress(sbt) + offset,
                .stride        = handleSizeAligned,
                .size          = handleCount * handleSizeAligned,
            };
        }

        Ref<rhi::Texture> RenderDevice::getTextureByIndex(const uint32_t index)
        {
            if (index >= m_LoadedTextures.size())
                return nullptr;
            return m_LoadedTextures[index];
        }

        std::vector<const rhi::Texture*> RenderDevice::getAllLoadedTextures()
        {
            std::vector<const rhi::Texture*> textures;
            textures.reserve(m_LoadedTextures.size());
            for (const auto& tex : m_LoadedTextures)
            {
                if (tex)
                    textures.push_back(tex.get());
            }
            return textures;
        }

        Ref<rhi::Buffer> RenderDevice::createBindlessStorageBuffer(AllocationHints allocationHint)
        {
            assert(m_MemoryAllocator);

            // Analyze texture memory usage
            vk::DeviceSize totalTextureMemory = 0;
            for (const auto& texture : m_LoadedTextures)
            {
                if (texture)
                {
                    const auto& extent = texture->getExtent();
                    const auto& depth  = texture->getDepth() > 0 ? texture->getDepth() : 1;
                    // Calculate the memory usage for this texture
                    vk::DeviceSize textureSize =
                        extent.width * extent.height * depth * getBytesPerPixel(texture->getPixelFormat());
                    totalTextureMemory += textureSize;
                }
            }

            // Create a large storage buffer for bindless resources
            Buffer buffer {
                m_MemoryAllocator,
                totalTextureMemory,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                makeAllocationFlags(allocationHint),
                vma::MemoryUsage::eAutoPreferDevice,
            };

            return createRef<rhi::Buffer>(std::move(buffer));
        }
    } // namespace rhi
} // namespace vultra
