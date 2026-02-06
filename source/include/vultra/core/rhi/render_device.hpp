#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/scoped_enum_flags.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/draw_indirect_command.hpp"
#include "vultra/core/rhi/image_aspect.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/raytracing/acceleration_structure.hpp"
#include "vultra/core/rhi/raytracing/raytracing_instance.hpp"
#include "vultra/core/rhi/raytracing/raytracing_pipeline.hpp"
#include "vultra/core/rhi/raytracing/raytracing_pipeline_properties.hpp"
#include "vultra/core/rhi/raytracing/scratch_buffer.hpp"
#include "vultra/core/rhi/raytracing/shader_binding_table.hpp"
#include "vultra/core/rhi/render_mesh.hpp"
#include "vultra/core/rhi/sampler_info.hpp"
#include "vultra/core/rhi/shader_compiler.hpp"
#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/rhi/uniform_buffer.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include "vultra/core/profiling/tracy_wrapper.hpp"

#include <glm/fwd.hpp>

#include <functional>
#include <set>
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

    namespace gfx
    {
        class MeshLoader;
    }

    namespace rhi
    {
        enum class RenderDeviceFeatureFlagBits : uint32_t
        {
            eNormal             = 0,
            eRayQuery           = BIT(0),
            eRayTracingPipeline = BIT(1),
            eMeshShader         = BIT(2),
            eOpenXR             = BIT(3),

            eRayTracing = eRayQuery | eRayTracingPipeline,
            eAll        = eNormal | eRayQuery | eRayTracingPipeline | eMeshShader | eOpenXR,
        };

        enum class RenderDeviceFeatureReportFlagBits : uint64_t
        {
            eNone                  = 0,
            eOpenXR                = BIT(0),
            eRayTracingPipeline    = BIT(1),
            eRayQuery              = BIT(2),
            eAccelerationStructure = BIT(3),
            eMeshShader            = BIT(4),
            eBufferDeviceAddress   = BIT(5),
            eDescriptorIndexing    = BIT(6),
            eDrawIndirectCount     = BIT(7),
        };

        struct RenderDeviceFeatureReport
        {
            RenderDeviceFeatureReportFlagBits flags {RenderDeviceFeatureReportFlagBits::eNone};

            std::string deviceName;
            uint32_t    apiMajor {0};
            uint32_t    apiMinor {0};
            uint32_t    apiPatch {0};
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
            friend class RayTracingPipeline;
            friend class imgui::ImGuiRenderer;
            friend class openxr::XRHeadset;
            friend class gfx::MeshLoader;

        public:
            explicit RenderDevice(RenderDeviceFeatureFlagBits, std::string_view appName = "Untitled Vultra App");
            RenderDevice(const RenderDevice&)     = delete;
            RenderDevice(RenderDevice&&) noexcept = delete;
            ~RenderDevice();

            RenderDevice& operator=(const RenderDevice&)     = delete;
            RenderDevice& operator=(RenderDevice&&) noexcept = delete;

            [[nodiscard]] RenderDeviceFeatureFlagBits getFeatureFlag() const;
            [[nodiscard]] RenderDeviceFeatureReport   getFeatureReport() const;

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

            [[nodiscard]] DrawIndirectBuffer createDrawIndirectBuffer(uint32_t         commandCount,
                                                                      DrawIndirectType type,
                                                                      AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] std::pair<std::size_t, vk::DescriptorSetLayout>
            createDescriptorSetLayout(const std::vector<DescriptorSetLayoutBindingEx>&);

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

            [[nodiscard]] ComputePipeline createComputePipelineBuiltin(const SPIRV& spv,
                                                                       std::optional<PipelineLayout> = std::nullopt);

            RenderDevice& upload(Buffer&, const vk::DeviceSize offset, const vk::DeviceSize size, const void* data);
            RenderDevice& uploadDrawIndirect(DrawIndirectBuffer&, const std::vector<DrawIndirectCommand>& commands);

            RenderDevice& destroy(vk::Fence&);
            RenderDevice& destroy(vk::Semaphore&);

            [[nodiscard]] CommandBuffer createCommandBuffer() const;
            // Blocking.
            RenderDevice& execute(const std::function<void(CommandBuffer&)>&, bool oneTime = false);
            RenderDevice& execute(CommandBuffer&, const JobInfo& = {}, bool oneTime = false);

            RenderDevice& present(Swapchain&, const vk::Semaphore wait = nullptr);

            RenderDevice& wait(const vk::Fence);
            RenderDevice& reset(const vk::Fence);

            RenderDevice& waitIdle();

            bool saveTextureToFile(const Texture&         texture,
                                   const std::string&     filePath,
                                   const rhi::ImageAspect imageAspect = rhi::ImageAspect::eColor);

            // For the RTX
            // General for ray query
            [[nodiscard]] AccelerationStructure
            createAccelerationStructure(AccelerationStructureType           type,
                                        AccelerationStructureBuildSizesInfo buildSizesInfo) const;

            // For single geometry BLAS, e.g., triangle
            [[nodiscard]] AccelerationStructure createBuildSingleGeometryBLAS(uint64_t vertexBufferAddress,
                                                                              uint64_t indexBufferAddress,
                                                                              uint64_t transformBufferAddress,
                                                                              uint32_t vertexStride,
                                                                              uint32_t vertexCount,
                                                                              uint32_t indexCount);

            // For render mesh BLAS, e.g., multiple sub-meshes
            [[nodiscard]] AccelerationStructure createBuildRenderMeshBLAS(std::vector<RenderSubMesh>& subMeshes);

            // For single instance TLAS, e.g., triangle instance
            [[nodiscard]] AccelerationStructure
            createBuildSingleInstanceTLAS(const AccelerationStructure& referenceBLAS, const glm::mat4& transform);

            // For multiple instance TLAS, e.g., render mesh instance
            [[nodiscard]] AccelerationStructure
            createBuildMultipleInstanceTLAS(const std::vector<RayTracingInstance>& instances);

            [[nodiscard]] ScratchBuffer createScratchBuffer(uint64_t size,
                                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] InstanceBuffer createInstancesBuffer(uint32_t instanceCount,
                                                               AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] TransformBuffer createTransformBuffer(AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] ShaderBindingTable createShaderBindingTable(const rhi::RayTracingPipeline& pipeline,
                                                                      AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] uint64_t getBufferDeviceAddress(const Buffer&) const;

            [[nodiscard]] RayTracingPipelineProperties getRayTracingPipelineProperties() const;

            // For the OpenXR
            openxr::XRDevice* getXRDevice() const { return m_XRDevice; }

            // Bindless
            Ref<rhi::Texture>                getTextureByIndex(const uint32_t index);
            std::vector<const rhi::Texture*> getAllLoadedTextures();
            void                             clearLoadedTextures() { m_LoadedTextures.clear(); }
            Ref<rhi::Buffer>                 createBindlessStorageBuffer(AllocationHints = AllocationHints::eNone);

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

            [[nodiscard]] AccelerationStructureBuffer
            createAccelerationStructureBuffer(vk::DeviceSize size, AllocationHints = AllocationHints::eNone) const;

            uint64_t getAccelerationStructureDeviceAddress(const AccelerationStructure&) const;

            StrideDeviceAddressRegion
            getSbtEntryStrideDeviceAddressRegion(const Buffer& sbt, uint32_t handleCount, uint64_t offset) const;

        private:
            std::set<std::string>       m_SupportedExtensions;
            RenderDeviceFeatureReport   m_FeatureReport {};
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

            // Raytracing properties and features
            vk::PhysicalDeviceRayTracingPipelinePropertiesKHR  m_RayTracingPipelineProperties;
            vk::PhysicalDeviceAccelerationStructureFeaturesKHR m_AccelerationStructureFeatures;

            TracyVkCtx m_TracyContext {nullptr};

            template<typename T>
            using Cache = std::unordered_map<size_t, T>;

            Cache<vk::Sampler>             m_Samplers;
            Cache<vk::DescriptorSetLayout> m_DescriptorSetLayouts;
            Cache<vk::PipelineLayout>      m_PipelineLayouts;

            ShaderCompiler m_ShaderCompiler;

            openxr::XRDevice* m_XRDevice {nullptr};

            // Textures loaded from files, used for bindless textures
            std::vector<Ref<rhi::Texture>> m_LoadedTextures;
        };
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::RenderDeviceFeatureFlagBits> : std::true_type
{};

template<>
struct HasFlags<vultra::rhi::RenderDeviceFeatureReportFlagBits> : std::true_type
{};

template<>
struct HasFlags<vultra::rhi::AllocationHints> : std::true_type
{};
