#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/scoped_enum_flags.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/raytracing/acceleration_structure.hpp"
#include "vultra/core/rhi/raytracing/acceleration_structure_buffer.hpp"
#include "vultra/core/rhi/raytracing/scratch_buffer.hpp"
#include "vultra/core/rhi/sampler_info.hpp"
#include "vultra/core/rhi/shader_compiler.hpp"
#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/rhi/uniform_buffer.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include <functional>
#include <string>
#include <unordered_map>

namespace vultra
{
    namespace imgui
    {
        class ImGuiRenderer;
    }

    namespace openxr
    {
        class XRDevice;
        class XRHeadset;
    } // namespace openxr

    namespace rhi
    {
        enum class RenderDeviceFeatureFlagBits : uint32_t
        {
            eNormal             = 0,
            eRaytracingPipeline = BIT(0),
            eMeshShader         = BIT(1),
            eOpenXR             = BIT(2),

            eAll = eNormal | eRaytracingPipeline | eMeshShader | eOpenXR,
        };

        struct PhysicalDeviceInfo
        {
            uint32_t    vendorId;
            uint32_t    deviceId;
            std::string deviceName;

            std::string toString()
            {
                return std::format("[Vendor ID: {}, Device ID: {}, Device Name: {}]", vendorId, deviceId, deviceName);
            }
        };

        struct JobInfo
        {
            vk::Semaphore           wait {nullptr};
            vk::PipelineStageFlags2 waitStage {vk::PipelineStageFlagBits2::eAllCommands};
            vk::Semaphore           signal {nullptr};
        };

        enum class AllocationHints
        {
            eNone            = ZERO_BIT,
            eMinMemory       = BIT(0),
            eSequentialWrite = BIT(1),
            eRandomAccess    = BIT(2),
        };

        class RenderDevice final
        {
            friend class GraphicsPipeline;
            friend class imgui::ImGuiRenderer;
            friend class openxr::XRHeadset;

        public:
            explicit RenderDevice(RenderDeviceFeatureFlagBits, std::string_view appName = "Untitled Vultra App");
            RenderDevice(const RenderDevice&)     = delete;
            RenderDevice(RenderDevice&&) noexcept = delete;
            ~RenderDevice();

            RenderDevice& operator=(const RenderDevice&)     = delete;
            RenderDevice& operator=(RenderDevice&&) noexcept = delete;

            [[nodiscard]] RenderDeviceFeatureFlagBits getFeatureFlag() const;

            [[nodiscard]] std::string getName() const;

            [[nodiscard]] PhysicalDeviceInfo getPhysicalDeviceInfo() const;

            [[nodiscard]] vk::PhysicalDeviceLimits   getDeviceLimits() const;
            [[nodiscard]] vk::PhysicalDeviceFeatures getDeviceFeatures() const;

            [[nodiscard]] vk::FormatProperties getFormatProperties(PixelFormat) const;

            [[nodiscard]] Swapchain createSwapchain(os::Window&,
                                                    Swapchain::Format = Swapchain::Format::esRGB,
                                                    VerticalSync      = VerticalSync::eEnabled) const;

            [[nodiscard]] vk::Fence     createFence(bool signaled = true) const;
            [[nodiscard]] vk::Semaphore createSemaphore();

            [[nodiscard]] Buffer createStagingBuffer(vk::DeviceSize size, const void* data = nullptr) const;

            [[nodiscard]] VertexBuffer
            createVertexBuffer(Buffer::Stride, vk::DeviceSize capacity, AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] IndexBuffer
            createIndexBuffer(IndexType, vk::DeviceSize capacity, AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] UniformBuffer createUniformBuffer(vk::DeviceSize size,
                                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] StorageBuffer createStorageBuffer(vk::DeviceSize size,
                                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] ScratchBuffer createScratchBuffer(vk::DeviceSize size,
                                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] AccelerationStructureBuffer
            createAccelerationStructureBuffer(vk::DeviceSize size, AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] std::pair<std::size_t, vk::DescriptorSetLayout>
            createDescriptorSetLayout(const std::vector<vk::DescriptorSetLayoutBinding>&);

            [[nodiscard]] PipelineLayout createPipelineLayout(const PipelineLayoutInfo&);

            [[nodiscard]] Texture
            createTexture2D(Extent2D, PixelFormat, uint32_t numMipLevels, uint32_t numLayers, ImageUsage) const;

            [[nodiscard]] Texture
            createTexture3D(Extent2D, uint32_t depth, PixelFormat, uint32_t numMipLevels, ImageUsage) const;

            [[nodiscard]] Texture
            createCubemap(uint32_t size, PixelFormat, uint32_t numMipLevels, uint32_t numLayers, ImageUsage) const;

            RenderDevice&             setupSampler(Texture&, SamplerInfo);
            [[nodiscard]] vk::Sampler getSampler(const SamplerInfo&);

            [[nodiscard]] ShaderCompiler::Result
            compile(const ShaderType,
                    const std::string_view                                             code,
                    const std::string_view                                             entryPointName,
                    const std::unordered_map<std::string, std::optional<std::string>>& defines) const;

            [[nodiscard]] ShaderModule
            createShaderModule(const ShaderType,
                               const std::string_view                                             code,
                               const std::string_view                                             entryPointName,
                               const std::unordered_map<std::string, std::optional<std::string>>& defines,
                               ShaderReflection* = nullptr) const;

            [[nodiscard]] ShaderModule createShaderModule(SPIRV, ShaderReflection* = nullptr) const;

            [[nodiscard]] ComputePipeline createComputePipeline(const ShaderStageInfo& shaderStageInfo,
                                                                std::optional<PipelineLayout> = std::nullopt);

            RenderDevice& upload(Buffer&, const vk::DeviceSize offset, const vk::DeviceSize size, const void* data);

            RenderDevice& destroy(vk::Fence&);
            RenderDevice& destroy(vk::Semaphore&);

            [[nodiscard]] CommandBuffer createCommandBuffer() const;
            // Blocking.
            RenderDevice& execute(const std::function<void(CommandBuffer&)>&);
            RenderDevice& execute(CommandBuffer&, const JobInfo& = {});

            RenderDevice& present(Swapchain&, const vk::Semaphore wait = nullptr);

            RenderDevice& wait(const vk::Fence);
            RenderDevice& reset(const vk::Fence);

            RenderDevice& waitIdle();

            // For the raytracing
            uint64_t getBufferDeviceAddress(const Buffer&) const;
            uint64_t getAccelerationStructureDeviceAddress(const AccelerationStructure&) const;

            openxr::XRDevice* getXRDevice() const { return m_XRDevice; }

        private:
            void createXRDevice();
            void createInstance();
            void selectPhysicalDevice();
            void findGenericQueue();
            void createLogicalDevice();
            void createMemoryAllocator();
            void createCommandPool();
            void createPipelineCache();
            void createDefaultDescriptorPool();
            void createTracyContext();
            void createTracky();

            vk::CommandBuffer allocateCommandBuffer() const;
            vk::Sampler       createSampler(const SamplerInfo&) const;

        private:
            RenderDeviceFeatureFlagBits m_FeatureFlag {RenderDeviceFeatureFlagBits::eNormal};
            std::string                 m_AppName;

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

            TracyVkCtx m_TracyContext {nullptr};

            template<typename T>
            using Cache = std::unordered_map<size_t, T>;

            Cache<vk::Sampler>             m_Samplers;
            Cache<vk::DescriptorSetLayout> m_DescriptorSetLayouts;
            Cache<vk::PipelineLayout>      m_PipelineLayouts;

            ShaderCompiler m_ShaderCompiler;

            openxr::XRDevice* m_XRDevice {nullptr};
        };
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::RenderDeviceFeatureFlagBits> : std::true_type
{};

template<>
struct HasFlags<vultra::rhi::AllocationHints> : std::true_type
{};
