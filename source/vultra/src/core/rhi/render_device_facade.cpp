#include "vultra/core/rhi/render_device.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/base/ranges.hpp"
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_buffer.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_command_buffer.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_compute_pipeline.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_pipeline.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_pipeline_layout.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_render_device.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_shader_module.hpp"
#endif
#include "vultra/core/rhi/backends/webgpu/conversions.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_buffer.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_compute_pipeline.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_compute_pipeline_destroy.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_pipeline_layout.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_render_device.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_shader_module.hpp"
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include "vultra/core/rhi/backends/webgpu/webgpu_sorter.hpp"
#endif
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/interfaces/texture_access.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/util.hpp"
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
#include "vultra/function/openxr/xr_device.hpp"
#endif

#include <bit>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <limits>
#include <stdexcept>
#include <vshadersystem/reflect.hpp>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] std::size_t hashSamplerInfo(const SamplerInfo& v)
            {
                std::size_t h {0};
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
                            v.compareOp ? *v.compareOp : CompareOp::eNever,
                            static_cast<int32_t>(v.minLod),
                            static_cast<int32_t>(v.maxLod),
                            static_cast<int32_t>(v.borderColor));
                return h;
            }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            [[nodiscard]] VulkanRenderDevice& vkBackend(std::unique_ptr<IRenderDevice>& backend)
            {
                assert(backend);
                auto* result = dynamic_cast<VulkanRenderDevice*>(backend.get());
                assert(result && "RenderDevice backend is not Vulkan");
                return *result;
            }

            [[nodiscard]] const VulkanRenderDevice& vkBackend(const std::unique_ptr<IRenderDevice>& backend)
            {
                assert(backend);
                const auto* result = dynamic_cast<const VulkanRenderDevice*>(backend.get());
                assert(result && "RenderDevice backend is not Vulkan");
                return *result;
            }
#endif

            [[nodiscard]] WebGPURenderDevice& webgpuBackend(std::unique_ptr<IRenderDevice>& backend)
            {
                assert(backend);
                auto* result = dynamic_cast<WebGPURenderDevice*>(backend.get());
                assert(result && "RenderDevice backend is not WebGPU");
                return *result;
            }

            [[nodiscard]] const WebGPURenderDevice& webgpuBackend(const std::unique_ptr<IRenderDevice>& backend)
            {
                assert(backend);
                const auto* result = dynamic_cast<const WebGPURenderDevice*>(backend.get());
                assert(result && "RenderDevice backend is not WebGPU");
                return *result;
            }

            [[nodiscard]] bool isRaytracingOrRayQueryEnabled(const RenderDeviceFeatureFlagBits featureFlag)
            {
                return HasFlagValues(featureFlag, RenderDeviceFeatureFlagBits::eRayTracingPipeline) ||
                       HasFlagValues(featureFlag, RenderDeviceFeatureFlagBits::eRayQuery);
            }

            [[nodiscard]] uint64_t webgpuFormatFeatureFlags(const WebGPURenderDevice& backend,
                                                            const PixelFormat         pixelFormat)
            {
                constexpr uint64_t kTransferSrc      = 0x00004000ull;
                constexpr uint64_t kTransferDst      = 0x00008000ull;
                constexpr uint64_t kSampledImage     = 0x00000004ull;
                constexpr uint64_t kSampledLinear    = 0x00001000ull;
                constexpr uint64_t kStorageImage     = 0x00000008ull;
                constexpr uint64_t kColorAttachment  = 0x00000080ull;

                switch (pixelFormat)
                {
                    case PixelFormat::eBC1_UNorm:
                    case PixelFormat::eBC2_UNorm:
                    case PixelFormat::eBC3_UNorm:
                    case PixelFormat::eBC4_UNorm:
                    case PixelFormat::eBC5_UNorm:
                    case PixelFormat::eBC6H_RGB16F:
                    case PixelFormat::eBC7_RGBA8_UNorm:
                        return backend.m_SupportsTextureCompressionBC ?
                                   (kTransferDst | kSampledImage | kSampledLinear) :
                                   0u;
                    case PixelFormat::eRGBA8_UNorm:
                    case PixelFormat::eRGBA8_sRGB:
                    case PixelFormat::eBGRA8_UNorm:
                    case PixelFormat::eBGRA8_sRGB:
                        return kTransferSrc | kTransferDst | kSampledImage | kSampledLinear | kStorageImage |
                               kColorAttachment;
                    case PixelFormat::eRGBA16F:
                    case PixelFormat::eRGBA32F:
                        return kTransferSrc | kTransferDst | kSampledImage | kStorageImage | kColorAttachment;
                    default:
                        return kTransferSrc | kTransferDst | kSampledImage;
                }
            }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            [[nodiscard]] constexpr auto makeAllocationFlags(const AllocationHints hints)
            {
                vma::AllocationCreateFlags flags {0};
                if (HasFlagValues(hints, AllocationHints::eMinMemory))
                {
                    flags |= vma::AllocationCreateFlagBits::eStrategyMinMemory;
                }
                if (HasFlagValues(hints, AllocationHints::eSequentialWrite))
                {
                    flags |= vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
                             vma::AllocationCreateFlagBits::eMapped;
                }
                if (HasFlagValues(hints, AllocationHints::eRandomAccess))
                {
                    flags |= vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped;
                }
                return flags;
            }

            [[nodiscard]] Buffer makeVulkanBuffer(const vma::Allocator             allocator,
                                                  IRenderDevice*                   renderDevice,
                                                  const uint64_t                   size,
                                                  const BufferUsage                usage,
                                                  const vma::AllocationCreateFlags flags,
                                                  const vma::MemoryUsage           memoryUsage)
            {
                return Buffer {std::make_unique<VulkanBuffer>(allocator, size, usage, flags, memoryUsage, renderDevice)};
            }

            [[nodiscard]] vk::ShaderModule createVulkanShaderModule(const vk::Device device, const SPIRV& spv)
            {
                vk::ShaderModule           handle {nullptr};
                vk::ShaderModuleCreateInfo createInfo {};
                createInfo.codeSize = sizeof(uint32_t) * spv.size();
                createInfo.pCode    = spv.data();
                VK_CHECK(device.createShaderModule(&createInfo, nullptr, &handle),
                         "RenderDevice",
                         "Failed to create shader module");
                return handle;
            }
#endif

            [[nodiscard]] Buffer
            makeWebGPUBuffer(WebGPURenderDevice& backend, const uint64_t size, const BufferUsage usage)
            {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
                const auto handle = backend.m_NextBufferHandle++;
                return Buffer {std::make_unique<WebGPUBuffer>(&backend, size, handle, 0)};
#else
                if (backend.m_Device == nullptr || backend.m_Queue == nullptr)
                {
                    return {};
                }

                WGPUBufferUsage wgpuUsage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
                if (HasFlagValues(usage, BufferUsage::eVertexBuffer))
                {
                    wgpuUsage |= WGPUBufferUsage_Vertex;
                }
                if (HasFlagValues(usage, BufferUsage::eIndexBuffer))
                {
                    wgpuUsage |= WGPUBufferUsage_Index;
                }
                if (HasFlagValues(usage, BufferUsage::eUniformBuffer))
                {
                    wgpuUsage |= WGPUBufferUsage_Uniform;
                }
                if (HasFlagValues(usage, BufferUsage::eStorageBuffer) ||
                    HasFlagValues(usage, BufferUsage::eAccelerationStorage) ||
                    HasFlagValues(usage, BufferUsage::eAccelerationBuildInput))
                {
                    wgpuUsage |= WGPUBufferUsage_Storage;
                }
                if (HasFlagValues(usage, BufferUsage::eIndirectBuffer))
                {
                    wgpuUsage |= WGPUBufferUsage_Indirect;
                }

                WGPUBufferDescriptor descriptor {};
                descriptor.usage            = wgpuUsage;
                descriptor.size             = size;
                descriptor.mappedAtCreation = false;

                auto* const handle = wgpuDeviceCreateBuffer(backend.m_Device, &descriptor);
                if (handle == nullptr)
                {
                    return {};
                }
                return Buffer {std::make_unique<WebGPUBuffer>(
                    &backend,
                    size,
                    reinterpret_cast<std::uintptr_t>(handle),
                    reinterpret_cast<std::uintptr_t>(backend.m_Queue))};
#endif
            }

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            [[nodiscard]] std::size_t hashDescriptorBindings(const std::vector<DescriptorSetLayoutBindingEx>& bindings)
            {
                std::size_t hash {0};
                for (const auto& binding : bindings)
                {
                    hashCombine(hash,
                                binding.binding,
                                binding.type,
                                binding.access,
                                binding.count,
                                binding.stageFlags,
                                binding.flags);
                }
                return hash;
            }

            [[nodiscard]] DescriptorSetLayoutKey
            createWebGPUDescriptorSetLayout(WebGPURenderDevice&                       backend,
                                            std::vector<DescriptorSetLayoutBindingEx> bindings)
            {
                const auto hash = hashDescriptorBindings(bindings);
                if (const auto it = backend.m_DescriptorSetLayouts.find(hash);
                    it != backend.m_DescriptorSetLayouts.end())
                {
                    return DescriptorSetLayoutKey {hash};
                }

                std::vector<WGPUBindGroupLayoutEntry> entries;
                entries.reserve(bindings.size() * 2u);
                for (const auto& binding : bindings)
                {
                    if (binding.count != 1)
                    {
                        VULTRA_CORE_ERROR("[RenderDevice] WebGPU descriptor array is not supported yet (binding={}, "
                                          "type={}, count={})",
                                          binding.binding,
                                          static_cast<int>(binding.type),
                                          binding.count);
                        return {};
                    }

                    const auto visibility = webgpu::toWgpuShaderStages(binding.stageFlags);
                    switch (binding.type)
                    {
                        case DescriptorType::eUniformBuffer: {
                            WGPUBindGroupLayoutEntry entry {};
                            entry.binding               = binding.binding;
                            entry.visibility            = visibility;
                            entry.buffer.type           = WGPUBufferBindingType_Uniform;
                            entry.buffer.minBindingSize = 0;
                            entries.push_back(entry);
                            break;
                        }
                        case DescriptorType::eStorageBuffer:
                        case DescriptorType::eStorageBufferDynamic: {
                            WGPUBindGroupLayoutEntry entry {};
                            entry.binding               = binding.binding;
                            entry.visibility            = visibility;
                            entry.buffer.type           = binding.access == vshadersystem::ResourceAccess::eReadOnly ?
                                                              WGPUBufferBindingType_ReadOnlyStorage :
                                                              WGPUBufferBindingType_Storage;
                            entry.buffer.minBindingSize = 0;
                            entries.push_back(entry);
                            break;
                        }
                        case DescriptorType::eSampledImage: {
                            WGPUBindGroupLayoutEntry entry {};
                            entry.binding               = binding.binding;
                            entry.visibility            = visibility;
                            entry.texture.sampleType    = WGPUTextureSampleType_Float;
                            entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                            entry.texture.multisampled  = false;
                            entries.push_back(entry);
                            break;
                        }
                        case DescriptorType::eSampler: {
                            WGPUBindGroupLayoutEntry entry {};
                            entry.binding      = binding.binding;
                            entry.visibility   = visibility;
                            entry.sampler.type = WGPUSamplerBindingType_Filtering;
                            entries.push_back(entry);
                            break;
                        }
                        case DescriptorType::eCombinedImageSampler: {
                            WGPUBindGroupLayoutEntry textureEntry {};
                            textureEntry.binding               = binding.binding;
                            textureEntry.visibility            = visibility;
                            textureEntry.texture.sampleType    = WGPUTextureSampleType_Float;
                            textureEntry.texture.viewDimension = WGPUTextureViewDimension_2D;
                            textureEntry.texture.multisampled  = false;
                            entries.push_back(textureEntry);

                            WGPUBindGroupLayoutEntry samplerEntry {};
                            samplerEntry.binding      = binding.binding + 1u;
                            samplerEntry.visibility   = visibility;
                            samplerEntry.sampler.type = WGPUSamplerBindingType_Filtering;
                            entries.push_back(samplerEntry);
                            break;
                        }
                        case DescriptorType::eStorageImage: {
                            WGPUBindGroupLayoutEntry entry {};
                            entry.binding                      = binding.binding;
                            entry.visibility                   = visibility;
                            switch (binding.access)
                            {
                                case vshadersystem::ResourceAccess::eReadOnly:
                                    entry.storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
                                    break;
                                case vshadersystem::ResourceAccess::eReadWrite:
                                    entry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
                                    break;
                                case vshadersystem::ResourceAccess::eWriteOnly:
                                case vshadersystem::ResourceAccess::eUnknown:
                                default:
                                    entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                                    break;
                            }
                            entry.storageTexture.format        = WGPUTextureFormat_RGBA8Unorm;
                            entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                            entries.push_back(entry);
                            break;
                        }
                        case DescriptorType::eInputAttachment:
                        case DescriptorType::eAccelerationStructure:
                            // Not supported in current WebGPU path.
                            break;
                    }
                }

                WGPUBindGroupLayoutDescriptor descriptor {};
                descriptor.entryCount = entries.size();
                descriptor.entries    = entries.data();
                auto* const layout    = wgpuDeviceCreateBindGroupLayout(backend.m_Device, &descriptor);
                if (layout == nullptr)
                {
                    return {};
                }
                backend.m_DescriptorSetLayouts.emplace(hash, layout);
                backend.m_DescriptorSetLayoutBindings.emplace(hash, std::move(bindings));
                return DescriptorSetLayoutKey {hash};
            }
#endif
        } // namespace

        std::unique_ptr<IRenderDevice> createWebGPUBackend(const std::string_view appName)
        {
            return std::make_unique<WebGPURenderDevice>(appName);
        }

        RenderDeviceFeatureFlagBits RenderDevice::getFeatureFlag() const
        {
            assert(m_Backend);
            return m_Backend->getFeatureFlag();
        }

        RenderDeviceFeatureReport RenderDevice::getFeatureReport() const
        {
            assert(m_Backend);
            return m_Backend->getFeatureReport();
        }

        RenderDeviceLimits RenderDevice::getLimits() const
        {
            assert(m_Backend);
            return m_Backend->getLimits();
        }

        RenderDeviceSyncCapabilities RenderDevice::getSyncCapabilities() const
        {
            assert(m_Backend);
            return m_Backend->getSyncCapabilities();
        }

        RenderBackendApi RenderDevice::getBackendApi() const
        {
            assert(m_Backend);
            return m_Backend->getBackendApi();
        }

        bool RenderDevice::supportsSwapchain() const
        {
            assert(m_Backend);
            return m_Backend->supportsSwapchain();
        }

        openxr::XRDevice* RenderDevice::getXRDevice() const
        {
            assert(m_Backend);
            return m_Backend->getXRDevice();
        }

        std::string RenderDevice::getName() const
        {
            assert(m_Backend);
            return m_Backend->getName();
        }

        PhysicalDeviceInfo RenderDevice::getPhysicalDeviceInfo() const
        {
            assert(m_Backend);
            return m_Backend->getPhysicalDeviceInfo();
        }

        void RenderDevice::beginFrameGpuQuery(CommandBuffer& cb)
        {
            assert(m_Backend);
            m_Backend->beginFrameGpuQuery(cb.getHandle());
        }

        void RenderDevice::endFrameGpuQuery(CommandBuffer& cb)
        {
            assert(m_Backend);
            m_Backend->endFrameGpuQuery(cb.getHandle());
        }

        double RenderDevice::consumeGpuFrameMs()
        {
            assert(m_Backend);
            return m_Backend->consumeGpuFrameMs();
        }

        uint64_t RenderDevice::beginScopeGpuQuery(CommandBuffer& cb)
        {
            assert(m_Backend);
            return m_Backend->beginScopeGpuQuery(cb.getHandle());
        }

        uint64_t RenderDevice::beginScopeGpuQuery(const std::uintptr_t commandBufferHandle)
        {
            assert(m_Backend);
            return m_Backend->beginScopeGpuQuery(commandBufferHandle);
        }

        void RenderDevice::endScopeGpuQuery(CommandBuffer& cb, const uint64_t scopeToken)
        {
            assert(m_Backend);
            m_Backend->endScopeGpuQuery(cb.getHandle(), scopeToken);
        }

        void RenderDevice::endScopeGpuQuery(const std::uintptr_t commandBufferHandle, const uint64_t scopeToken)
        {
            assert(m_Backend);
            m_Backend->endScopeGpuQuery(commandBufferHandle, scopeToken);
        }

        double RenderDevice::consumeScopeGpuMs(const uint64_t scopeToken)
        {
            assert(m_Backend);
            return m_Backend->consumeScopeGpuMs(scopeToken);
        }

        RenderDeviceMemoryStats RenderDevice::getMemoryStats() const
        {
            assert(m_Backend);
            return m_Backend->getMemoryStats();
        }

        RenderDevice::RenderDevice(const RenderDeviceFeatureFlagBits  featureFlag,
                                   const std::string_view             appName,
                                   const std::span<const char* const> requiredInstanceExtensions,
                                   const RenderBackendApi             backendApi)
        {
            switch (backendApi)
            {
                case RenderBackendApi::eAuto:
                case RenderBackendApi::eVulkan:
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
                    m_Backend = std::make_unique<VulkanRenderDevice>();
#else
                    m_Backend = createWebGPUBackend(appName);
#ifdef TRACKY_ENABLE
                    TRACKY_STARTUP_WEBGPU(reinterpret_cast<std::uintptr_t>(webgpuBackend(m_Backend).m_Instance),
                                          reinterpret_cast<std::uintptr_t>(webgpuBackend(m_Backend).m_Device),
                                          reinterpret_cast<std::uintptr_t>(webgpuBackend(m_Backend).m_Queue),
                                          4 * 1024,
                                          false,
                                          1.0f);
#endif
                    return;
#endif
                    break;
                case RenderBackendApi::eWebGPU: {
                    m_Backend = createWebGPUBackend(appName);
#ifdef TRACKY_ENABLE
                    TRACKY_STARTUP_WEBGPU(reinterpret_cast<std::uintptr_t>(webgpuBackend(m_Backend).m_Instance),
                                          reinterpret_cast<std::uintptr_t>(webgpuBackend(m_Backend).m_Device),
                                          reinterpret_cast<std::uintptr_t>(webgpuBackend(m_Backend).m_Queue),
                                          4 * 1024,
                                          false,
                                          1.0f);
#endif
                    return;
                }
            }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            vkBackend(m_Backend).m_FeatureFlag = featureFlag;
            vkBackend(m_Backend).m_AppName     = appName;
            vkBackend(m_Backend).m_RequiredInstanceExtensions.assign(requiredInstanceExtensions.begin(),
                                                                     requiredInstanceExtensions.end());

            if (HasFlagValues(featureFlag, RenderDeviceFeatureFlagBits::eXR))
            {
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
                createXRDevice();
#endif
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
#else
            (void)featureFlag;
            (void)requiredInstanceExtensions;
#endif
        }

        RenderDevice::~RenderDevice()
        {
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return;
#else
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
#ifdef TRACKY_ENABLE
                TRACKY_TEARDOWN();
#endif
                return;
            }

            if (vkBackend(m_Backend).m_Device)
            {
                vkBackend(m_Backend).m_Device.waitIdle();
            }

            for (auto [_, layout] : vkBackend(m_Backend).m_DescriptorSetLayouts)
            {
                vkBackend(m_Backend).m_Device.destroyDescriptorSetLayout(layout);
            }

            for (auto [_, layout] : vkBackend(m_Backend).m_PipelineLayouts)
            {
                vkBackend(m_Backend).m_Device.destroyPipelineLayout(layout);
            }

            for (auto [_, sampler] : vkBackend(m_Backend).m_Samplers)
            {
                vkBackend(m_Backend).m_Device.destroySampler(vk::Sampler {asVkHandle<VkSampler>(sampler.value)});
            }

            TracyGpuDestroy(vkBackend(m_Backend).m_TracyContext);
            TRACKY_TEARDOWN();

            if (vkBackend(m_Backend).m_FrameTimeQueryPool)
            {
                vkBackend(m_Backend).m_Device.destroyQueryPool(vkBackend(m_Backend).m_FrameTimeQueryPool);
                vkBackend(m_Backend).m_FrameTimeQueryPool = nullptr;
            }
            if (vkBackend(m_Backend).m_ScopeTimeQueryPool)
            {
                vkBackend(m_Backend).m_Device.destroyQueryPool(vkBackend(m_Backend).m_ScopeTimeQueryPool);
                vkBackend(m_Backend).m_ScopeTimeQueryPool = nullptr;
            }

            if (vkBackend(m_Backend).m_MemoryAllocator)
            {
                vkBackend(m_Backend).m_MemoryAllocator.destroy();
            }

            if (vkBackend(m_Backend).m_Device)
            {
                vkBackend(m_Backend).m_Device.destroyDescriptorPool(vkBackend(m_Backend).m_DefaultDescriptorPool);
                vkBackend(m_Backend).m_Device.destroyPipelineCache(vkBackend(m_Backend).m_PipelineCache);
                vkBackend(m_Backend).m_Device.destroyCommandPool(vkBackend(m_Backend).m_CommandPool);
                vkBackend(m_Backend).m_Device.destroy();
            }

            if (vkBackend(m_Backend).m_Instance)
            {
#if _DEBUG
                if (vkBackend(m_Backend).m_DebugMessenger)
                {
                    vkBackend(m_Backend).m_Instance.destroyDebugUtilsMessengerEXT(
                        vkBackend(m_Backend).m_DebugMessenger);
                }
#endif
                vkBackend(m_Backend).m_Instance.destroy();
            }

#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
            if (vkBackend(m_Backend).m_XRDevice)
            {
                delete vkBackend(m_Backend).m_XRDevice;
                vkBackend(m_Backend).m_XRDevice = nullptr;
            }
#endif
#endif
        }

        std::array<float, 2> RenderDevice::getLineWidthRange() const
        {
            assert(m_Backend);
            return m_Backend->getLineWidthRange();
        }

        float RenderDevice::getMaxSamplerAnisotropy() const
        {
            assert(m_Backend);
            return m_Backend->getMaxSamplerAnisotropy();
        }

        uint64_t RenderDevice::getFormatFeatureFlagsOptimal(const PixelFormat pixelFormat) const
        {
            assert(m_Backend);
            return m_Backend->getFormatFeatureFlagsOptimal(pixelFormat);
        }

        Buffer RenderDevice::createStagingBuffer(const uint64_t size, const void* data) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend       = const_cast<WebGPURenderDevice&>(webgpuBackend(m_Backend));
                auto  stagingBuffer = makeWebGPUBuffer(backend, size, BufferUsage::eTransferSrc);

                if (data != nullptr && size > 0)
                {
                    auto* mappedPtr = static_cast<std::byte*>(stagingBuffer.map());
                    std::memcpy(mappedPtr, data, static_cast<size_t>(size));
                    stagingBuffer.unmap();
                }
                return stagingBuffer;
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            Buffer stagingBuffer = makeVulkanBuffer(vkBackend(m_Backend).m_MemoryAllocator,
                                                    m_Backend.get(),
                                                    size,
                                                    BufferUsage::eTransferSrc,
                                                    makeAllocationFlags(AllocationHints::eSequentialWrite),
                                                    vma::MemoryUsage::eAutoPreferHost);
            if (data != nullptr && size > 0)
            {
                auto* mappedPtr = stagingBuffer.map();
                std::memcpy(mappedPtr, data, static_cast<size_t>(size));
                stagingBuffer.unmap();
            }
            return stagingBuffer;
#endif
        }

        VertexBuffer RenderDevice::createVertexBuffer(const Buffer::Stride  stride,
                                                      const uint64_t        vertexCount,
                                                      const AllocationHints allocationHint) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = const_cast<WebGPURenderDevice&>(webgpuBackend(m_Backend));
                return VertexBuffer {makeWebGPUBuffer(backend,
                                                      stride * vertexCount,
                                                      BufferUsage::eVertexBuffer | BufferUsage::eTransferDst),
                                     stride};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            BufferUsage usage = BufferUsage::eVertexBuffer | BufferUsage::eTransferDst;
            if (HasFlagValues(vkBackend(m_Backend).m_FeatureReport.flags,
                              RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                usage |= BufferUsage::eShaderDeviceAddress;
            }
            if (isRaytracingOrRayQueryEnabled(vkBackend(m_Backend).m_FeatureFlag))
            {
                usage |= BufferUsage::eAccelerationBuildInput;
            }
            return VertexBuffer {
                makeVulkanBuffer(vkBackend(m_Backend).m_MemoryAllocator,
                                 m_Backend.get(),
                                 stride * vertexCount,
                                 usage,
                makeAllocationFlags(allocationHint),
                                 vma::MemoryUsage::eAutoPreferDevice),
                stride,
            };
#endif
        }

        IndexBuffer RenderDevice::createIndexBuffer(const IndexType       indexType,
                                                    const uint64_t        indexCount,
                                                    const AllocationHints allocationHint) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto&      backend     = const_cast<WebGPURenderDevice&>(webgpuBackend(m_Backend));
                const auto indexStride = indexType == IndexType::eUInt16 ? 2u : 4u;
                return IndexBuffer {makeWebGPUBuffer(backend,
                                                     indexStride * indexCount,
                                                     BufferUsage::eIndexBuffer | BufferUsage::eTransferDst),
                                    indexType};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            BufferUsage usage = BufferUsage::eIndexBuffer | BufferUsage::eTransferDst;
            if (HasFlagValues(vkBackend(m_Backend).m_FeatureReport.flags,
                              RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                usage |= BufferUsage::eShaderDeviceAddress;
            }
            if (isRaytracingOrRayQueryEnabled(vkBackend(m_Backend).m_FeatureFlag))
            {
                usage |= BufferUsage::eAccelerationBuildInput;
            }
            const auto indexStride = indexType == IndexType::eUInt16 ? 2 : 4;
            return IndexBuffer {
                makeVulkanBuffer(vkBackend(m_Backend).m_MemoryAllocator,
                                 m_Backend.get(),
                                 indexStride * indexCount,
                                 usage,
                makeAllocationFlags(allocationHint),
                                 vma::MemoryUsage::eAutoPreferDevice),
                indexType,
            };
#endif
        }

        UniformBuffer RenderDevice::createUniformBuffer(const uint64_t size, const AllocationHints allocationHint) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = const_cast<WebGPURenderDevice&>(webgpuBackend(m_Backend));
                return UniformBuffer {
                    Buffer {makeWebGPUBuffer(backend, size, BufferUsage::eUniformBuffer | BufferUsage::eTransferDst)}};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            return UniformBuffer {Buffer {
                makeVulkanBuffer(vkBackend(m_Backend).m_MemoryAllocator,
                                 m_Backend.get(),
                                 size,
                                 BufferUsage::eUniformBuffer | BufferUsage::eTransferDst,
                                 makeAllocationFlags(allocationHint),
                                 vma::MemoryUsage::eAutoPreferDevice),
            }};
#endif
        }

        StorageBuffer RenderDevice::createStorageBuffer(const uint64_t size, const AllocationHints allocationHint) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = const_cast<WebGPURenderDevice&>(webgpuBackend(m_Backend));
                return StorageBuffer {
                    Buffer {makeWebGPUBuffer(backend, size, BufferUsage::eStorageBuffer | BufferUsage::eTransferDst)}};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            BufferUsage usage = BufferUsage::eStorageBuffer | BufferUsage::eTransferDst;
            if (HasFlagValues(vkBackend(m_Backend).m_FeatureReport.flags,
                              RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                usage |= BufferUsage::eShaderDeviceAddress;
            }
            if (isRaytracingOrRayQueryEnabled(vkBackend(m_Backend).m_FeatureFlag))
            {
                usage |= BufferUsage::eAccelerationBuildInput;
            }
            return StorageBuffer {Buffer {
                makeVulkanBuffer(vkBackend(m_Backend).m_MemoryAllocator,
                                 m_Backend.get(),
                                 size,
                                 usage,
                makeAllocationFlags(allocationHint),
                                 vma::MemoryUsage::eAutoPreferDevice),
            }};
#endif
        }

        StorageBuffer RenderDevice::createStorageBufferWithUsage(const uint64_t        size,
                                                                 const BufferUsage     extraUsage,
                                                                 const AllocationHints allocationHint) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = const_cast<WebGPURenderDevice&>(webgpuBackend(m_Backend));
                return StorageBuffer {Buffer {makeWebGPUBuffer(
                    backend, size, BufferUsage::eStorageBuffer | BufferUsage::eTransferDst | extraUsage)}};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            BufferUsage usage = BufferUsage::eStorageBuffer | BufferUsage::eTransferDst | extraUsage;
            if (HasFlagValues(vkBackend(m_Backend).m_FeatureReport.flags,
                              RenderDeviceFeatureReportFlagBits::eBufferDeviceAddress))
            {
                usage |= BufferUsage::eShaderDeviceAddress;
            }
            if (isRaytracingOrRayQueryEnabled(vkBackend(m_Backend).m_FeatureFlag))
            {
                usage |= BufferUsage::eAccelerationBuildInput;
            }
            return StorageBuffer {Buffer {
                makeVulkanBuffer(vkBackend(m_Backend).m_MemoryAllocator,
                                 m_Backend.get(),
                                 size,
                                 usage,
                makeAllocationFlags(allocationHint),
                                 vma::MemoryUsage::eAutoPreferDevice),
            }};
#endif
        }

        DrawIndirectBuffer RenderDevice::createDrawIndirectBufferByCount(const uint32_t         commandCount,
                                                                         const DrawIndirectType type,
                                                                         const AllocationHints  allocationHint) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto&      backend = const_cast<WebGPURenderDevice&>(webgpuBackend(m_Backend));
                constexpr std::size_t kDrawIndirectCommandSize        = sizeof(uint32_t) * 4;
                constexpr std::size_t kDrawIndexedIndirectCommandSize = sizeof(uint32_t) * 5;
                const auto            stride = type == DrawIndirectType::eIndexed ? kDrawIndexedIndirectCommandSize :
                                                                                     kDrawIndirectCommandSize;
                return DrawIndirectBuffer {makeWebGPUBuffer(backend,
                                                            commandCount * stride,
                                                            BufferUsage::eIndirectBuffer | BufferUsage::eStorageBuffer |
                                                                BufferUsage::eTransferDst),
                                           type};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            const auto stride = type == DrawIndirectType::eIndexed ? sizeof(vk::DrawIndexedIndirectCommand) :
                                                                     sizeof(vk::DrawIndirectCommand);
            return DrawIndirectBuffer {
                makeVulkanBuffer(vkBackend(m_Backend).m_MemoryAllocator,
                                 m_Backend.get(),
                                 commandCount * stride,
                                 BufferUsage::eStorageBuffer | BufferUsage::eTransferDst | BufferUsage::eIndirectBuffer,
                                 makeAllocationFlags(allocationHint),
                                 vma::MemoryUsage::eCpuToGpu),
                type};
#endif
        }

        DrawIndirectBuffer RenderDevice::createDrawIndirectBufferBySize(const uint64_t         size,
                                                                        const DrawIndirectType type,
                                                                        const AllocationHints  allocationHint) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = const_cast<WebGPURenderDevice&>(webgpuBackend(m_Backend));
                return DrawIndirectBuffer {makeWebGPUBuffer(backend,
                                                            size,
                                                            BufferUsage::eIndirectBuffer | BufferUsage::eStorageBuffer |
                                                                BufferUsage::eTransferDst),
                                           type};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            return DrawIndirectBuffer {
                makeVulkanBuffer(vkBackend(m_Backend).m_MemoryAllocator,
                                 m_Backend.get(),
                                 size,
                                 BufferUsage::eStorageBuffer | BufferUsage::eTransferDst | BufferUsage::eIndirectBuffer,
                                 makeAllocationFlags(allocationHint),
                                 vma::MemoryUsage::eCpuToGpu),
                type};
#endif
        }

        Texture RenderDevice::createTexture2D(const Extent2D    extent,
                                              const PixelFormat format,
                                              const uint32_t    numMipLevels,
                                              const uint32_t    numLayers,
                                              const ImageUsage  usageFlags) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
                (void)extent;
                (void)format;
                (void)numMipLevels;
                (void)numLayers;
                (void)usageFlags;
                return {};
#else
                const auto& backend = webgpuBackend(m_Backend);
                if (backend.m_Device == nullptr || extent.width == 0 || extent.height == 0)
                {
                    return {};
                }

                const auto wgpuFormat = webgpu::toWgpuTextureFormat(format);
                if (wgpuFormat == WGPUTextureFormat_Undefined)
                {
                    VULTRA_CORE_WARN("[RenderDevice] Unsupported WebGPU texture format: {}", toString(format));
                    return {};
                }

                WGPUTextureDescriptor descriptor {};
                descriptor.usage                   = webgpu::toWgpuTextureUsage(usageFlags);
                descriptor.dimension               = WGPUTextureDimension_2D;
                descriptor.size.width              = extent.width;
                descriptor.size.height             = extent.height;
                descriptor.size.depthOrArrayLayers = std::max(1u, numLayers);
                descriptor.format                  = wgpuFormat;
                const uint32_t resolvedMipLevels   = numMipLevels > 0 ? numMipLevels : calcMipLevels(extent);
                descriptor.mipLevelCount           = resolvedMipLevels;
                descriptor.sampleCount             = 1u;

                auto* const textureHandle = wgpuDeviceCreateTexture(backend.m_Device, &descriptor);
                if (textureHandle == nullptr)
                {
                    return {};
                }
                if (numLayers > 1u)
                {
                    return TextureAccess::fromOwnedImage(
                        RenderBackendApi::eWebGPU,
                        TextureDeviceHandle {reinterpret_cast<std::uintptr_t>(backend.m_Device)},
                        TextureImageHandle {reinterpret_cast<std::uintptr_t>(textureHandle)},
                        extent,
                        format,
                        0u,
                        numLayers,
                        resolvedMipLevels,
                        m_Backend.get());
                }
                return TextureAccess::fromOwnedImage(
                    RenderBackendApi::eWebGPU,
                    TextureDeviceHandle {reinterpret_cast<std::uintptr_t>(backend.m_Device)},
                    TextureImageHandle {reinterpret_cast<std::uintptr_t>(textureHandle)},
                    extent,
                    format,
                    0u,
                    1u,
                    resolvedMipLevels,
                    m_Backend.get());
#endif
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            const auto allocatorHandle =
                reinterpret_cast<std::uintptr_t>(static_cast<VmaAllocator>(vkBackend(m_Backend).m_MemoryAllocator));
            return Texture {
                TextureAllocatorHandle {allocatorHandle},
                Texture::CreateInfo {
                    .extent       = extent,
                    .depth        = 0,
                    .pixelFormat  = format,
                    .numMipLevels = numMipLevels,
                    .numLayers    = numLayers,
                    .numFaces     = 1,
                    .usageFlags   = usageFlags,
                },
                m_Backend.get(),
            };
#endif
        }

        Texture RenderDevice::createTexture3D(const Extent2D    extent,
                                              const uint32_t    depth,
                                              const PixelFormat format,
                                              const uint32_t    numMipLevels,
                                              const ImageUsage  usageFlags) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                return {};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            const auto allocatorHandle =
                reinterpret_cast<std::uintptr_t>(static_cast<VmaAllocator>(vkBackend(m_Backend).m_MemoryAllocator));
            return Texture {
                TextureAllocatorHandle {allocatorHandle},
                Texture::CreateInfo {
                    .extent       = extent,
                    .depth        = depth,
                    .pixelFormat  = format,
                    .numMipLevels = numMipLevels,
                    .numLayers    = 0,
                    .numFaces     = 1,
                    .usageFlags   = usageFlags,
                },
                m_Backend.get(),
            };
#endif
        }

        Texture RenderDevice::createCubemap(const uint32_t    size,
                                            const PixelFormat format,
                                            const uint32_t    numMipLevels,
                                            const uint32_t    numLayers,
                                            const ImageUsage  usageFlags) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                return {};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_MemoryAllocator);
            const auto allocatorHandle =
                reinterpret_cast<std::uintptr_t>(static_cast<VmaAllocator>(vkBackend(m_Backend).m_MemoryAllocator));
            return Texture {
                TextureAllocatorHandle {allocatorHandle},
                Texture::CreateInfo {
                    .extent       = {size, size},
                    .depth        = 0,
                    .pixelFormat  = format,
                    .numMipLevels = numMipLevels,
                    .numLayers    = numLayers,
                    .numFaces     = 6,
                    .usageFlags   = usageFlags,
                },
                m_Backend.get(),
            };
#endif
        }

        RenderDevice& RenderDevice::setupSampler(Texture& texture, SamplerInfo samplerInfo)
        {
            assert(texture && HasFlagValues(texture.getUsageFlags(), ImageUsage::eSampled));

            if (m_Backend->getBackendApi() != RenderBackendApi::eWebGPU &&
                (getFormatFeatureFlagsOptimal(texture.getPixelFormat()) &
                 0x00001000ull) == 0)
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
            const auto hash = hashSamplerInfo(samplerInfo);

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackend(m_Backend);
                auto  it      = backend.m_Samplers.find(hash);
                if (it == backend.m_Samplers.cend())
                {
                    it = backend.m_Samplers.emplace(hash, createSampler(samplerInfo)).first;
                    VULTRA_CORE_TRACE("[RenderDevice] Created WebGPU Sampler {}", hash);
                }
                return Sampler {samplerInfo};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return Sampler {samplerInfo};
#else
            auto it = vkBackend(m_Backend).m_Samplers.find(hash);
            if (it == vkBackend(m_Backend).m_Samplers.cend())
            {
                it = vkBackend(m_Backend).m_Samplers.emplace(hash, createSampler(samplerInfo)).first;
                VULTRA_CORE_TRACE("[RenderDevice] Created Sampler {}", hash);
            }

            return Sampler {samplerInfo};
#endif
        }

        SamplerHandle RenderDevice::getSamplerHandle(const Sampler& sampler) const
        {
            assert(sampler);
            const auto hash = hashSamplerInfo(sampler.info());

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                const auto& backend = webgpuBackend(m_Backend);
                auto        it      = backend.m_Samplers.find(hash);
                if (it == backend.m_Samplers.cend())
                {
                    it = backend.m_Samplers.emplace(hash, createSampler(sampler.info())).first;
                }
                return it->second;
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            auto it = vkBackend(m_Backend).m_Samplers.find(hash);
            if (it == vkBackend(m_Backend).m_Samplers.cend())
            {
                it = vkBackend(m_Backend).m_Samplers.emplace(hash, createSampler(sampler.info())).first;
            }

            return it->second;
#endif
        }

        SamplerHandle RenderDevice::createSampler(const SamplerInfo& samplerInfo) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                const auto& backend = webgpuBackend(m_Backend);
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
                (void)samplerInfo;
                return SamplerHandle {backend.m_NextSamplerHandle++};
#else
                if (backend.m_Device == nullptr)
                {
                    return {};
                }

                WGPUSamplerDescriptor descriptor {};
                descriptor.magFilter       = webgpu::toWgpuFilter(samplerInfo.magFilter);
                descriptor.minFilter       = webgpu::toWgpuFilter(samplerInfo.minFilter);
                descriptor.mipmapFilter    = webgpu::toWgpuMipmapFilter(samplerInfo.mipmapMode);
                descriptor.addressModeU    = webgpu::toWgpuAddressMode(samplerInfo.addressModeS);
                descriptor.addressModeV    = webgpu::toWgpuAddressMode(samplerInfo.addressModeT);
                descriptor.addressModeW    = webgpu::toWgpuAddressMode(samplerInfo.addressModeR);
                descriptor.lodMinClamp     = samplerInfo.minLod;
                descriptor.lodMaxClamp     = samplerInfo.maxLod;
                const bool allLinearFilter = descriptor.magFilter == WGPUFilterMode_Linear &&
                                             descriptor.minFilter == WGPUFilterMode_Linear &&
                                             descriptor.mipmapFilter == WGPUMipmapFilterMode_Linear;
                descriptor.maxAnisotropy =
                    (allLinearFilter && samplerInfo.maxAnisotropy.has_value()) ?
                        static_cast<uint16_t>(glm::clamp(*samplerInfo.maxAnisotropy, 1.0f, 16.0f)) :
                        1u;
                descriptor.compare = WGPUCompareFunction_Undefined;

                auto* const sampler = wgpuDeviceCreateSampler(backend.m_Device, &descriptor);
                if (sampler == nullptr)
                {
                    return {};
                }
                return SamplerHandle {reinterpret_cast<std::uintptr_t>(sampler)};
#endif
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            const auto&           backend = vkBackend(m_Backend);
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
            samplerCreateInfo.compareOp               = toVk(samplerInfo.compareOp.value_or(CompareOp::eLess));
            samplerCreateInfo.anisotropyEnable        = samplerInfo.maxAnisotropy.has_value();
            samplerCreateInfo.maxAnisotropy =
                samplerInfo.maxAnisotropy ? glm::clamp(*samplerInfo.maxAnisotropy, 1.0f, getMaxSamplerAnisotropy()) :
                                            0.0f;

            vk::Sampler sampler {nullptr};
            VK_CHECK(backend.m_Device.createSampler(&samplerCreateInfo, nullptr, &sampler),
                     "RenderDevice",
                     "Failed to create sampler");
            return SamplerHandle {toBackendHandle(static_cast<VkSampler>(sampler))};
#endif
        }

        Swapchain
        RenderDevice::createSwapchain(os::Window& window, const SwapchainFormat format, const VerticalSync vsync) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                const auto& backend = webgpuBackend(m_Backend);
                return Swapchain {
                    reinterpret_cast<std::uintptr_t>(backend.m_Instance),
                    reinterpret_cast<std::uintptr_t>(backend.m_Adapter),
                    reinterpret_cast<std::uintptr_t>(backend.m_Device),
                    RenderBackendApi::eWebGPU,
                    &window,
                    format,
                    vsync,
                };
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_Device);
            return Swapchain {
                toBackendHandle(static_cast<VkInstance>(vkBackend(m_Backend).m_Instance)),
                toBackendHandle(static_cast<VkPhysicalDevice>(vkBackend(m_Backend).m_PhysicalDevice)),
                toBackendHandle(static_cast<VkDevice>(vkBackend(m_Backend).m_Device)),
                RenderBackendApi::eVulkan,
                &window,
                format,
                vsync,
            };
#endif
        }

        FenceHandle RenderDevice::createFence(const bool signaled) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                const auto& backend              = webgpuBackend(m_Backend);
                const auto  handle               = backend.m_NextSyncHandle++;
                backend.m_EmulatedFences[handle] = signaled;
                return FenceHandle {handle};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)signaled;
            return {};
#else
            assert(vkBackend(m_Backend).m_Device);
            vk::FenceCreateInfo createInfo {};
            createInfo.flags = signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlags(0u);
            vk::Fence fence {nullptr};
            VK_CHECK(vkBackend(m_Backend).m_Device.createFence(&createInfo, nullptr, &fence),
                     "RenderDevice",
                     "Failed to create fence");
            return FenceHandle {toBackendHandle(static_cast<VkFence>(fence))};
#endif
        }

        SemaphoreHandle RenderDevice::createSemaphore()
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto&      backend = webgpuBackend(m_Backend);
                const auto handle  = backend.m_NextSyncHandle++;
                backend.m_EmulatedSemaphores.insert(handle);
                return SemaphoreHandle {handle};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            assert(vkBackend(m_Backend).m_Device);
            vk::SemaphoreCreateInfo createInfo {};
            createInfo.flags = vk::SemaphoreCreateFlags(0);
            vk::Semaphore semaphore {nullptr};
            VK_CHECK(vkBackend(m_Backend).m_Device.createSemaphore(&createInfo, nullptr, &semaphore),
                     "RenderDevice",
                     "Failed to create semaphore");
            return SemaphoreHandle {toBackendHandle(static_cast<VkSemaphore>(semaphore))};
#endif
        }

        RenderDevice& RenderDevice::present(Swapchain& swapchain, const SemaphoreHandle wait)
        {
            ZoneScopedN("RHI::Present");
            assert(swapchain);

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
                (void)wait;
                auto* const surface = reinterpret_cast<WGPUSurface>(swapchain.getHandle());
#if !defined(__EMSCRIPTEN__)
                if (surface != nullptr)
                {
                    (void)wgpuSurfacePresent(surface);
                }
#else
                (void)surface;
#endif
#endif
                return *this;
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)swapchain;
            (void)wait;
            return *this;
#else
            assert(vkBackend(m_Backend).m_GenericQueue);
            vk::PresentInfoKHR  presentInfo {};
            const vk::Semaphore waitSemaphore {asVkHandle<VkSemaphore>(wait.value)};
            presentInfo.waitSemaphoreCount = static_cast<bool>(wait) ? 1u : 0u;
            presentInfo.pWaitSemaphores    = static_cast<bool>(wait) ? &waitSemaphore : nullptr;
            presentInfo.swapchainCount     = 1;
            const auto swapchainHandle     = vk::SwapchainKHR {asVkHandle<VkSwapchainKHR>(swapchain.getHandle())};
            const auto imageIndex          = swapchain.getCurrentBufferIndex();
            presentInfo.pSwapchains        = &swapchainHandle;
            presentInfo.pImageIndices      = &imageIndex;

            auto result = vkBackend(m_Backend).m_GenericQueue.presentKHR(&presentInfo);
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
#endif
        }

        RenderDevice& RenderDevice::wait(const FenceHandle fence)
        {
            assert(static_cast<bool>(fence));

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackend(m_Backend);
                if (const auto it = backend.m_EmulatedFences.find(fence.value); it != backend.m_EmulatedFences.end())
                {
                    it->second = true;
                }
                return *this;
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return *this;
#else
            assert(vkBackend(m_Backend).m_Device);
            const vk::Fence vkFence {asVkHandle<VkFence>(fence.value)};
            VK_CHECK(
                vkBackend(m_Backend).m_Device.waitForFences(1, &vkFence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
                "RenderDevice",
                "Failed to wait for fence");
            return reset(fence);
#endif
        }

        RenderDevice& RenderDevice::reset(const FenceHandle fence)
        {
            assert(static_cast<bool>(fence));

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackend(m_Backend);
                if (const auto it = backend.m_EmulatedFences.find(fence.value); it != backend.m_EmulatedFences.end())
                {
                    it->second = false;
                }
                return *this;
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return *this;
#else
            assert(vkBackend(m_Backend).m_Device);
            const vk::Fence vkFence {asVkHandle<VkFence>(fence.value)};
            VK_CHECK(vkBackend(m_Backend).m_Device.resetFences(1, &vkFence), "RenderDevice", "Failed to reset fence");
            return *this;
#endif
        }

        RenderDevice& RenderDevice::waitIdle()
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
                auto& backend = webgpuBackend(m_Backend);
                if (backend.m_Instance)
                {
                    wgpuInstanceProcessEvents(backend.m_Instance);
                }
#endif
                return *this;
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return *this;
#else
            assert(vkBackend(m_Backend).m_Device);
            vkBackend(m_Backend).m_Device.waitIdle();
            return *this;
#endif
        }

        RenderDevice& RenderDevice::destroy(FenceHandle& fence)
        {
            assert(static_cast<bool>(fence));

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackend(m_Backend);
                backend.m_EmulatedFences.erase(fence.value);
                fence = {};
                return *this;
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            fence = {};
            return *this;
#else
            assert(vkBackend(m_Backend).m_Device);
            vkBackend(m_Backend).m_Device.destroyFence(vk::Fence {asVkHandle<VkFence>(fence.value)});
            fence = {};
            return *this;
#endif
        }

        RenderDevice& RenderDevice::destroy(SemaphoreHandle& semaphore)
        {
            assert(static_cast<bool>(semaphore));

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                auto& backend = webgpuBackend(m_Backend);
                backend.m_EmulatedSemaphores.erase(semaphore.value);
                semaphore = {};
                return *this;
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            semaphore = {};
            return *this;
#else
            assert(vkBackend(m_Backend).m_Device);
            vkBackend(m_Backend).m_Device.destroySemaphore(vk::Semaphore {asVkHandle<VkSemaphore>(semaphore.value)});
            semaphore = {};
            return *this;
#endif
        }

        CommandBuffer RenderDevice::createCommandBuffer() const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                const auto& backend = webgpuBackend(m_Backend);
                return CommandBuffer {std::make_unique<WebGPUCommandBuffer>(backend)};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return {};
#else
            const auto      fenceHandle = createFence();
            const vk::Fence fence {asVkHandle<VkFence>(fenceHandle.value)};
            return CommandBuffer {std::make_unique<VulkanCommandBuffer>(
                vkBackend(m_Backend).m_Device,
                vkBackend(m_Backend).m_CommandPool,
                vk::CommandBuffer {asVkHandle<VkCommandBuffer>(allocateCommandBuffer())},
                vkBackend(m_Backend).m_TracyContext,
                fence,
                this,
                vkBackend(m_Backend).m_UseKhrDynamicRendering,
                vkBackend(m_Backend).m_UseKhrSynchronization2,
                isRaytracingOrRayQueryEnabled(vkBackend(m_Backend).m_FeatureFlag))};
#endif
        }

        RenderDevice& RenderDevice::execute(const std::function<void(CommandBuffer&)>& f, const bool oneTime)
        {
            auto cb = createCommandBuffer();
            cb.begin();
            {
                TRACY_GPU_ZONE(cb, "ExecuteCommandBuffer");
                std::invoke(f, cb);
            }
            return execute(cb, JobInfo {}, oneTime);
        }

        RenderDevice& RenderDevice::execute(CommandBuffer& cb, const JobInfo& jobInfo, const bool oneTime)
        {
            cb.flushBarriers();
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            if (m_Backend->getBackendApi() == RenderBackendApi::eVulkan)
            {
                TracyGpuCollect(vkBackend(m_Backend).m_TracyContext, asVkHandle<VkCommandBuffer>(cb.getHandle()));
            }
#endif
            cb.end();
            cb.submit(jobInfo, oneTime);
            return *this;
        }

        RenderDevice& RenderDevice::upload(Buffer& buffer, const uint64_t offset, const uint64_t size, const void* data)
        {
            assert(buffer && data);
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            if (m_Backend->getBackendApi() == RenderBackendApi::eVulkan)
            {
                assert(vkBackend(m_Backend).m_Device);
            }
#endif

            auto* mappedMemory = std::bit_cast<std::byte*>(buffer.map());
            std::memcpy(mappedMemory + offset, data, size);
            buffer.flush().unmap();
            return *this;
        }

        RenderDevice&
        RenderDevice::uploadS(Buffer& buffer, const uint64_t offset, const uint64_t size, const void* data)
        {
            assert(buffer && data);
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                return upload(buffer, offset, size, data);
            }
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            assert(vkBackend(m_Backend).m_Device);
#endif

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

            const uint32_t maxCommands = buffer.getSize();

            assert(commands.size() <= maxCommands);

            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                webgpuBackend(m_Backend).uploadDrawIndirect(buffer, commands);
                return *this;
            }

            auto*      dst    = static_cast<std::byte*>(buffer.map());
            const auto type   = buffer.getDrawIndirectType();
            const auto stride = buffer.getStride();

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            assert(vkBackend(m_Backend).m_Device);

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
#else
            (void)type;
            (void)stride;
#endif

            buffer.flush().unmap();
            return *this;
        }

        Ref<rhi::Texture> RenderDevice::createDefaultWhite1x1Texture2D()
        {
            uint32_t whitePixel = 0xFFFFFFFF;
            auto     texture    = Texture::Builder {}
                               .setExtent({1, 1})
                               .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                               .setUsageFlags(ImageUsage::eSampled | ImageUsage::eTransferDst)
                               .setupOptimalSampler(true)
                               .build(*this);

            auto stagingBuffer = createStagingBuffer(sizeof(whitePixel));
            rhi::upload(*this, stagingBuffer, {}, texture, false);
            return createRef<rhi::Texture>(std::move(texture));
        }

#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
        RadixSorter RenderDevice::createRadixSorter(const uint32_t maxElementCount)
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_Backend && m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                return RadixSorter::create(std::make_unique<WebGPUSorter>(*this, maxElementCount));
            }
#endif
            (void)maxElementCount;
            return {};
        }

        DeviceAddress RenderDevice::getBufferDeviceAddress(const Buffer& buffer) const
        {
            (void)buffer;
            return {};
        }
#endif

        ShaderCompiler::Result
        RenderDevice::compile(const ShaderType                                                   shaderType,
                              const std::string_view                                             code,
                              const std::string_view                                             entryPointName,
                              const std::unordered_map<std::string, std::optional<std::string>>& defines) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                (void)shaderType;
                (void)code;
                (void)entryPointName;
                (void)defines;
                return std::unexpected("WebGPU path does not emit SPIR-V from ShaderCompiler");
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)shaderType;
            (void)code;
            (void)entryPointName;
            (void)defines;
            return std::unexpected("Vulkan backend is disabled for this build");
#else
            return vkBackend(m_Backend).m_ShaderCompiler.compile(shaderType, code, entryPointName, defines);
#endif
        }

        DescriptorSetLayoutKey
        RenderDevice::createDescriptorSetLayout(const std::vector<DescriptorSetLayoutBindingEx>& bindings)
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
                auto& backend = webgpuBackend(m_Backend);
                return createWebGPUDescriptorSetLayout(backend, bindings);
#else
                (void)bindings;
                return {};
#endif
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)bindings;
            return {};
#else
            assert(vkBackend(m_Backend).m_Device);

            // DescriptorSetLayoutKey uses 0 as "invalid", so the hash seed must be non-zero
            // even for empty layouts (set with no bindings).
            std::size_t hash = (sizeof(std::size_t) >= sizeof(std::uint64_t)) ?
                                   static_cast<std::size_t>(1469598103934665603ull) :
                                   static_cast<std::size_t>(2166136261u);
            for (const auto& b : bindings)
            {
                hashCombine(hash, b.binding, b.type, b.access, b.count, b.stageFlags, b.flags);
            }

            if (const auto it = vkBackend(m_Backend).m_DescriptorSetLayouts.find(hash);
                it != vkBackend(m_Backend).m_DescriptorSetLayouts.cend())
            {
                return DescriptorSetLayoutKey {hash};
            }

            std::vector<vk::DescriptorSetLayoutBinding> vkBindings;
            vkBindings.reserve(bindings.size());
            for (const auto& b : bindings)
            {
                vkBindings.push_back(
                    vk::DescriptorSetLayoutBinding {b.binding, toVk(b.type), b.count, toVk(b.stageFlags)});
            }

            std::vector<vk::DescriptorBindingFlags> vkFlags;
            vkFlags.reserve(bindings.size());
            for (const auto& b : bindings)
            {
                vkFlags.push_back(static_cast<vk::DescriptorBindingFlags>(b.flags));
            }

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
            VK_CHECK(
                vkBackend(m_Backend).m_Device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout),
                "RenderDevice",
                "Failed to create descriptor set layout");

            vkBackend(m_Backend).m_DescriptorSetLayouts.emplace(hash, descriptorSetLayout);
            return DescriptorSetLayoutKey {hash};
#endif
        }

        std::uintptr_t RenderDevice::getDescriptorSetLayoutHandle(const DescriptorSetLayoutKey layoutKey) const
        {
            assert(layoutKey);
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
                if (const auto it = webgpuBackend(m_Backend).m_DescriptorSetLayouts.find(layoutKey.value);
                    it != webgpuBackend(m_Backend).m_DescriptorSetLayouts.end())
                {
                    return reinterpret_cast<std::uintptr_t>(it->second);
                }
#endif
                return 0;
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            return 0;
#else
            if (const auto it = vkBackend(m_Backend).m_DescriptorSetLayouts.find(layoutKey.value);
                it != vkBackend(m_Backend).m_DescriptorSetLayouts.end())
            {
                return toBackendHandle(static_cast<VkDescriptorSetLayout>(it->second));
            }
            return 0;
#endif
        }

        ShaderModule RenderDevice::createShaderModule(SPIRV spv, ShaderReflection* reflection) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                (void)spv;
                (void)reflection;
                return {};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)spv;
            (void)reflection;
            return {};
#else
            assert(vkBackend(m_Backend).m_Device != nullptr);
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
            return ShaderModule {std::make_unique<VulkanShaderModule>(std::move(spv))};
#endif
        }

        ShaderModule
        RenderDevice::createShaderModule(const ShaderType       shaderType,
                                         const std::string_view code,
                                         const std::string_view entryPointName,
                                         const std::unordered_map<std::string, std::optional<std::string>>& defines,
                                         ShaderReflection* reflection) const
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                (void)shaderType;
                (void)entryPointName;
                (void)defines;
                (void)reflection;
                return ShaderModule {std::make_unique<WebGPUShaderModule>(std::string(code))};
            }

            auto spv = compile(shaderType, code, entryPointName, defines);
            if (spv)
            {
                return createShaderModule(*spv, reflection);
            }

            VULTRA_CORE_ERROR("[RenderDevice] Failed to compile shader: {}", spv.error());
            return {};
        }

        ComputePipeline RenderDevice::createComputePipeline(const ShaderStageInfo&        shaderStageInfo,
                                                            std::optional<PipelineLayout> pipelineLayout)
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
                (void)shaderStageInfo;
                (void)pipelineLayout;
                return {};
#else
                auto& backend = webgpuBackend(m_Backend);
                if (backend.m_Device == nullptr)
                {
                    return {};
                }

                auto reflection = pipelineLayout ? std::nullopt : std::make_optional<ShaderReflection>();
                auto shaderModule = createShaderModule(ShaderType::eCompute,
                                                       shaderStageInfo.code,
                                                       shaderStageInfo.entryPointName,
                                                       shaderStageInfo.defines,
                                                       reflection ? std::addressof(reflection.value()) : nullptr);
                if (!shaderModule)
                {
                    return {};
                }

                if (shaderStageInfo.reflection.has_value())
                {
                    shaderModule.getReflection() = *shaderStageInfo.reflection;
                    if (reflection)
                    {
                        reflection = shaderStageInfo.reflection;
                    }
                }

                if (reflection && !pipelineLayout)
                {
                    pipelineLayout = reflectPipelineLayout(*this, *reflection);
                }
                if (!pipelineLayout)
                {
                    return {};
                }

                WGPUShaderSourceWGSL source {};
                source.chain.sType = WGPUSType_ShaderSourceWGSL;
                source.code        = WGPUStringView {.data = shaderModule.getWgsl().data(), .length = WGPU_STRLEN};

                WGPUShaderModuleDescriptor shaderDesc {};
                shaderDesc.nextInChain = const_cast<WGPUChainedStruct*>(
                    reinterpret_cast<const WGPUChainedStruct*>(&source));
                auto* const shaderHandle = wgpuDeviceCreateShaderModule(backend.m_Device, &shaderDesc);
                if (shaderHandle == nullptr)
                {
                    return {};
                }

                WGPUComputePipelineDescriptor descriptor {};
                descriptor.layout  = reinterpret_cast<WGPUPipelineLayout>(pipelineLayout->getHandle());
                descriptor.compute.module = shaderHandle;
                descriptor.compute.entryPoint = WGPUStringView {.data   = shaderStageInfo.entryPointName.data(),
                                                                .length = shaderStageInfo.entryPointName.size()};

                auto* const pipeline = wgpuDeviceCreateComputePipeline(backend.m_Device, &descriptor);
                wgpuShaderModuleRelease(shaderHandle);
                if (pipeline == nullptr)
                {
                    return {};
                }

                const auto localSize =
                    reflection && reflection->localSize.has_value() ? *reflection->localSize : glm::uvec3 {1u, 1u, 1u};

                return ComputePipeline {
                    std::move(*pipelineLayout),
                    localSize,
                    reinterpret_cast<std::uintptr_t>(pipeline),
                    std::make_unique<WebGPUComputePipelineDestroy>(),
                    std::make_unique<WebGPUComputePipeline>(reinterpret_cast<std::uintptr_t>(pipeline), localSize),
                };
#endif
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)shaderStageInfo;
            (void)pipelineLayout;
            return {};
#else
            auto reflection = pipelineLayout ? std::nullopt : std::make_optional<ShaderReflection>();

            const auto shaderModule = createShaderModule(ShaderType::eCompute,
                                                         shaderStageInfo.code,
                                                         shaderStageInfo.entryPointName,
                                                         shaderStageInfo.defines,
                                                         reflection ? std::addressof(reflection.value()) : nullptr);
            if (!shaderModule)
            {
                return {};
            }

            if (reflection)
            {
                pipelineLayout = reflectPipelineLayout(*this, *reflection);
            }
            assert(*pipelineLayout);

            const auto shaderModuleHandle =
                createVulkanShaderModule(vkBackend(m_Backend).m_Device, shaderModule.getSpirv());

            vk::ComputePipelineCreateInfo createInfo {};
            createInfo.stage =
                vk::PipelineShaderStageCreateInfo {{}, vk::ShaderStageFlagBits::eCompute, shaderModuleHandle, "main"};
            createInfo.layout = vk::PipelineLayout {asVkHandle<VkPipelineLayout>(pipelineLayout->getHandle())};

            auto [result, computePipeline] = vkBackend(m_Backend).m_Device.createComputePipeline(
                vkBackend(m_Backend).m_PipelineCache, createInfo, nullptr);
            vkBackend(m_Backend).m_Device.destroyShaderModule(shaderModuleHandle);
            if (result != vk::Result::eSuccess)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to create compute pipeline: {}", vk::to_string(result));
                throw std::runtime_error("Failed to create compute pipeline");
            }

            return ComputePipeline {
                std::move(pipelineLayout.value()),
                reflection ? reflection->localSize.value() : glm::uvec3 {},
                toBackendHandle(static_cast<VkPipeline>(computePipeline)),
                std::make_unique<VulkanPipeline>(toBackendHandle(static_cast<VkDevice>(vkBackend(m_Backend).m_Device))),
                std::make_unique<VulkanComputePipeline>(toBackendHandle(static_cast<VkPipeline>(computePipeline)),
                                                        reflection ? reflection->localSize.value() : glm::uvec3 {}),
            };
#endif
        }

        ComputePipeline RenderDevice::createComputePipelineBuiltin(const SPIRV&                  spv,
                                                                   std::optional<PipelineLayout> pipelineLayout)
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
                (void)spv;
                (void)pipelineLayout;
                VULTRA_CORE_WARN("[RenderDevice] WebGPU compute pipeline is not implemented yet");
                return {};
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)spv;
            (void)pipelineLayout;
            return {};
#else
            auto reflection = pipelineLayout ? std::nullopt : std::make_optional<ShaderReflection>();

            const auto shaderModule =
                createShaderModule(spv, reflection ? std::addressof(reflection.value()) : nullptr);
            if (!shaderModule)
            {
                return {};
            }

            if (reflection)
            {
                pipelineLayout = reflectPipelineLayout(*this, *reflection);
            }
            assert(*pipelineLayout);

            vk::ComputePipelineCreateInfo createInfo {};
            const auto                    shaderModuleHandle =
                createVulkanShaderModule(vkBackend(m_Backend).m_Device, shaderModule.getSpirv());
            createInfo.stage =
                vk::PipelineShaderStageCreateInfo {{}, vk::ShaderStageFlagBits::eCompute, shaderModuleHandle, "main"};
            createInfo.layout = vk::PipelineLayout {asVkHandle<VkPipelineLayout>(pipelineLayout->getHandle())};

            auto [result, computePipeline] = vkBackend(m_Backend).m_Device.createComputePipeline(
                vkBackend(m_Backend).m_PipelineCache, createInfo, nullptr);
            vkBackend(m_Backend).m_Device.destroyShaderModule(shaderModuleHandle);
            if (result != vk::Result::eSuccess)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to create compute pipeline: {}", vk::to_string(result));
                throw std::runtime_error("Failed to create compute pipeline");
            }

            return ComputePipeline {
                std::move(pipelineLayout.value()),
                reflection ? reflection->localSize.value() : glm::uvec3 {},
                toBackendHandle(static_cast<VkPipeline>(computePipeline)),
                std::make_unique<VulkanPipeline>(toBackendHandle(static_cast<VkDevice>(vkBackend(m_Backend).m_Device))),
                std::make_unique<VulkanComputePipeline>(toBackendHandle(static_cast<VkPipeline>(computePipeline)),
                                                        reflection ? reflection->localSize.value() : glm::uvec3 {}),
            };
#endif
        }

        PipelineLayout RenderDevice::createPipelineLayout(const PipelineLayoutInfo& layoutInfo)
        {
            if (m_Backend->getBackendApi() == RenderBackendApi::eWebGPU)
            {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
                auto& backend = webgpuBackend(m_Backend);
                if (backend.m_Device == nullptr)
                {
                    return {};
                }

                if (!layoutInfo.pushConstantRanges.empty())
                {
                    VULTRA_CORE_WARN("[RenderDevice] WebGPU does not natively support push constants, current path "
                                     "ignores push constant ranges");
                }

                std::size_t                                            hash {0};
                std::vector<DescriptorSetLayoutKey>                    descriptorSetLayoutKeys(kMinNumDescriptorSets);
                std::array<WGPUBindGroupLayout, kMinNumDescriptorSets> setLayouts {};
                std::size_t                                            maxSetWithBindings  = 0;
                bool                                                   hasAnyDescriptorSet = false;

                for (const auto& [set, bindings] : vultra::enumerate(layoutInfo.descriptorSets))
                {
                    if (bindings.empty())
                    {
                        continue;
                    }
                    hasAnyDescriptorSet          = true;
                    maxSetWithBindings           = std::max(maxSetWithBindings, static_cast<std::size_t>(set));
                    const auto key               = createWebGPUDescriptorSetLayout(backend, bindings);
                    descriptorSetLayoutKeys[set] = key;
                    hashCombine(hash, set, key.value, 1u);
                    if (const auto it = backend.m_DescriptorSetLayouts.find(key.value);
                        it != backend.m_DescriptorSetLayouts.end())
                    {
                        setLayouts[set] = it->second;
                    }
                }
                if (hasAnyDescriptorSet && backend.m_EmptyDescriptorSetLayout == nullptr)
                {
                    WGPUBindGroupLayoutDescriptor emptyDesc {};
                    backend.m_EmptyDescriptorSetLayout = wgpuDeviceCreateBindGroupLayout(backend.m_Device, &emptyDesc);
                }
                std::vector<WGPUBindGroupLayout> bindGroupLayouts;
                if (hasAnyDescriptorSet)
                {
                    bindGroupLayouts.reserve(maxSetWithBindings + 1u);
                    for (std::size_t set = 0; set <= maxSetWithBindings; ++set)
                    {
                        if (!setLayouts[set])
                        {
                            std::size_t emptyKey = 0;
                            hashCombine(emptyKey, std::size_t {0xE11F7E7u}, set);
                            if (emptyKey == 0)
                            {
                                emptyKey = set + 1u;
                            }
                            descriptorSetLayoutKeys[set] = DescriptorSetLayoutKey {emptyKey};
                            backend.m_DescriptorSetLayouts.try_emplace(emptyKey, backend.m_EmptyDescriptorSetLayout);
                            backend.m_DescriptorSetLayoutBindings.try_emplace(
                                emptyKey, std::vector<DescriptorSetLayoutBindingEx> {});
                        }
                        auto* const layout = setLayouts[set] ? setLayouts[set] : backend.m_EmptyDescriptorSetLayout;
                        bindGroupLayouts.push_back(layout);
                        hashCombine(hash, set, descriptorSetLayoutKeys[set].value, setLayouts[set] ? 1u : 0u);
                    }
                }
                for (const auto& range : layoutInfo.pushConstantRanges)
                {
                    hashCombine(hash, range.offset, range.size, range.stageFlags);
                }

                if (const auto it = backend.m_PipelineLayouts.find(hash); it != backend.m_PipelineLayouts.cend())
                {
                    return PipelineLayout {std::make_unique<WebGPUPipelineLayout>(
                        reinterpret_cast<std::uintptr_t>(it->second), std::move(descriptorSetLayoutKeys))};
                }

                WGPUPipelineLayoutDescriptor descriptor {};
                descriptor.bindGroupLayoutCount = bindGroupLayouts.size();
                descriptor.bindGroupLayouts     = bindGroupLayouts.empty() ? nullptr : bindGroupLayouts.data();
                auto* const layout              = wgpuDeviceCreatePipelineLayout(backend.m_Device, &descriptor);
                if (layout == nullptr)
                {
                    return {};
                }
                backend.m_PipelineLayouts.emplace(hash, layout);
                return PipelineLayout {std::make_unique<WebGPUPipelineLayout>(reinterpret_cast<std::uintptr_t>(layout),
                                                                              std::move(descriptorSetLayoutKeys))};
#else
                (void)layoutInfo;
                return {};
#endif
            }
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)layoutInfo;
            return {};
#else
            assert(vkBackend(m_Backend).m_Device);

            std::size_t                          hash {0};
            std::vector<DescriptorSetLayoutKey>  descriptorSetLayoutKeys(kMinNumDescriptorSets);
            std::vector<vk::DescriptorSetLayout> descriptorSetLayouts(kMinNumDescriptorSets);
            std::vector<vk::PushConstantRange>   vkPushConstantRanges;
            vkPushConstantRanges.reserve(layoutInfo.pushConstantRanges.size());

            for (const auto& [set, bindings] : vultra::enumerate(layoutInfo.descriptorSets))
            {
                for (const auto& binding : bindings)
                {
                    hashCombine(
                        hash, set, binding.binding, binding.type, binding.count, binding.stageFlags, binding.flags);
                }
                descriptorSetLayoutKeys[set] = createDescriptorSetLayout(bindings);
                descriptorSetLayouts[set]    = vk::DescriptorSetLayout {
                    asVkHandle<VkDescriptorSetLayout>(getDescriptorSetLayoutHandle(descriptorSetLayoutKeys[set]))};
            }
            for (const auto& range : layoutInfo.pushConstantRanges)
            {
                hashCombine(hash, range.offset, range.size, range.stageFlags);
                vk::PushConstantRange vkRange {};
                vkRange.offset     = range.offset;
                vkRange.size       = range.size;
                vkRange.stageFlags = toVk(range.stageFlags);
                vkPushConstantRanges.push_back(vkRange);
            }

            if (const auto it = vkBackend(m_Backend).m_PipelineLayouts.find(hash);
                it != vkBackend(m_Backend).m_PipelineLayouts.cend())
            {
                return PipelineLayout {std::make_unique<VulkanPipelineLayout>(
                    toBackendHandle(static_cast<VkPipelineLayout>(it->second)), std::move(descriptorSetLayoutKeys))};
            }

            vk::PipelineLayoutCreateInfo createInfo {};
            createInfo.setLayoutCount         = static_cast<uint32_t>(descriptorSetLayouts.size());
            createInfo.pSetLayouts            = descriptorSetLayouts.data();
            createInfo.pushConstantRangeCount = static_cast<uint32_t>(vkPushConstantRanges.size());
            createInfo.pPushConstantRanges    = vkPushConstantRanges.data();

            vk::PipelineLayout handle {nullptr};
            VK_CHECK(vkBackend(m_Backend).m_Device.createPipelineLayout(&createInfo, nullptr, &handle),
                     "RenderDevice",
                     "Failed to create pipeline layout");

            const auto& [inserted, _] = vkBackend(m_Backend).m_PipelineLayouts.emplace(hash, handle);
            return PipelineLayout {std::make_unique<VulkanPipelineLayout>(
                toBackendHandle(static_cast<VkPipelineLayout>(inserted->second)), std::move(descriptorSetLayoutKeys))};
#endif
        }
    } // namespace rhi
} // namespace vultra
