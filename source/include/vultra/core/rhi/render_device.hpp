#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/profiling/tracy_wrapper.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/structs/draw_indirect_command.hpp"
#include "vultra/core/rhi/structs/image_aspect.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/radix_sorter.hpp"
#include "vultra/core/rhi/structs/buffer_usage.hpp"
#include "vultra/core/rhi/raytracing/acceleration_structure.hpp"
#include "vultra/core/rhi/raytracing/raytracing_instance.hpp"
#include "vultra/core/rhi/raytracing/raytracing_pipeline.hpp"
#include "vultra/core/rhi/raytracing/raytracing_pipeline_properties.hpp"
#include "vultra/core/rhi/raytracing/scratch_buffer.hpp"
#include "vultra/core/rhi/raytracing/shader_binding_table.hpp"
#include "vultra/core/rhi/render_mesh.hpp"
#include "vultra/core/rhi/structs/sampler_info.hpp"
#include "vultra/core/rhi/shader_compiler.hpp"
#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/sampler.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/rhi/uniform_buffer.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_render_device_backend.hpp"
#include "vultra/core/rhi/structs/job_info.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"

#include <vbase/core/scoped_enum_flags.hpp>

#include <glm/fwd.hpp>

#include <functional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace vultra
{
    class ImGuiSystem;

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
        class VulkanImGuiBackend;
        enum class AllocationHints
        {
            eNone            = ZERO_BIT,
            eMinMemory       = BIT(0),
            eSequentialWrite = BIT(1),
            eRandomAccess    = BIT(2),
        };

        class RenderDevice final
        {
            friend class VulkanImGuiBackend;
            friend class GraphicsPipeline;
            friend class RayTracingPipeline;
            friend class RadixSorter;
            friend class vultra::ImGuiSystem;
            friend class openxr::XRHeadset;

        public:
            explicit RenderDevice(RenderDeviceFeatureFlagBits,
                                  std::string_view             appName                    = "Untitled Vultra App",
                                  std::span<const char* const> requiredInstanceExtensions = {});
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
                                                    VerticalSync      = VerticalSync::eDisabled) const;

            [[nodiscard]] vk::Fence     createFence(bool signaled = true) const;
            [[nodiscard]] vk::Semaphore createSemaphore();

            [[nodiscard]] Buffer createStagingBuffer(vk::DeviceSize size, const void* data = nullptr) const;

            [[nodiscard]] VertexBuffer createVertexBuffer(Buffer::Stride,
                                                          vk::DeviceSize vertexCount,
                                                          AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] IndexBuffer
            createIndexBuffer(IndexType, vk::DeviceSize indexCount, AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] UniformBuffer createUniformBuffer(vk::DeviceSize size,
                                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] StorageBuffer createStorageBuffer(vk::DeviceSize size,
                                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] StorageBuffer createStorageBufferWithUsage(vk::DeviceSize size,
                                                                     BufferUsage          extraUsage,
                                                                     AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] DrawIndirectBuffer
            createDrawIndirectBufferByCount(uint32_t         commandCount,
                                            DrawIndirectType type,
                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] DrawIndirectBuffer
            createDrawIndirectBufferBySize(vk::DeviceSize   size,
                                           DrawIndirectType type,
                                           AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] std::pair<std::size_t, std::uintptr_t>
            createDescriptorSetLayout(const std::vector<DescriptorSetLayoutBindingEx>&);

            [[nodiscard]] PipelineLayout createPipelineLayout(const PipelineLayoutInfo&);

            [[nodiscard]] Texture
            createTexture2D(Extent2D, PixelFormat, uint32_t numMipLevels, uint32_t numLayers, ImageUsage) const;

            [[nodiscard]] Texture
            createTexture3D(Extent2D, uint32_t depth, PixelFormat, uint32_t numMipLevels, ImageUsage) const;

            [[nodiscard]] Texture
            createCubemap(uint32_t size, PixelFormat, uint32_t numMipLevels, uint32_t numLayers, ImageUsage) const;

            RenderDevice&             setupSampler(Texture&, SamplerInfo);
            [[nodiscard]] Sampler getSampler(const SamplerInfo&);
            [[nodiscard]] vk::Sampler getSamplerHandle(const Sampler&) const;

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

            [[nodiscard]] RadixSorter createRadixSorter(uint32_t maxElementCount);

            [[nodiscard]] ComputePipeline createComputePipelineBuiltin(const SPIRV& spv,
                                                                       std::optional<PipelineLayout> = std::nullopt);

            // Direct mapping without staging buffer. Use with host-coherent memory or persistent mapped memory.
            RenderDevice& upload(Buffer&, const vk::DeviceSize offset, const vk::DeviceSize size, const void* data);

            // Upload with staging buffer. Use for non-host-visible memory. This is a helper that creates a staging
            // buffer and performs a synchronous submit/wait. Initialization/setup only, not per-frame pass execution.
            RenderDevice& uploadS(Buffer&, const vk::DeviceSize offset, const vk::DeviceSize size, const void* data);

            // Upload draw indirect commands.
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
            openxr::XRDevice* getXRDevice() const;

            // Native backend handles for backend-specific implementation code.
            [[nodiscard]] std::uintptr_t getNativeInstanceHandle() const;
            [[nodiscard]] std::uintptr_t getNativePhysicalDeviceHandle() const;
            [[nodiscard]] std::uintptr_t getNativeDeviceHandle() const;
            [[nodiscard]] int           getNativeQueueFamilyIndex() const;
            [[nodiscard]] std::uintptr_t getNativeQueueHandle() const;
            [[nodiscard]] std::uintptr_t getNativePipelineCacheHandle() const;
            [[nodiscard]] std::uintptr_t getNativeDescriptorPoolHandle() const;

            // Bindless
            // Bindless resource ownership is higher-level (e.g., resource::GpuScene).
            // RenderDevice only provides helpers for creating bindless-capable resources.
            Ref<rhi::Buffer> createBindlessStorageBuffer(AllocationHints = AllocationHints::eNone);

            // Fallback texture for invalid bindless indices. This is owned by RenderDevice for simplicity, but it can
            // be moved to a higher-level owner if needed.
            Ref<rhi::Texture> createDefaultWhite1x1Texture2D();

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
            std::unique_ptr<VulkanRenderDeviceBackend> m_Backend;
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

