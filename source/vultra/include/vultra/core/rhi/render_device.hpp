#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/profiling/tracy_wrapper.hpp"
#include "vultra/core/rhi/acceleration_structure.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/interfaces/irender_device.hpp"
#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/radix_sorter.hpp"
#include "vultra/core/rhi/raytracing_pipeline.hpp"
#include "vultra/core/rhi/sampler.hpp"
#include "vultra/core/rhi/scratch_buffer.hpp"
#include "vultra/core/rhi/shader_binding_table.hpp"
#include "vultra/core/rhi/shader_compiler.hpp"
#include "vultra/core/rhi/shader_library.hpp"
#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/core/rhi/structs/allocation_hints.hpp"
#include "vultra/core/rhi/structs/buffer_usage.hpp"
#include "vultra/core/rhi/structs/device_address.hpp"
#include "vultra/core/rhi/structs/draw_indirect_command.hpp"
#include "vultra/core/rhi/structs/handles.hpp"
#include "vultra/core/rhi/structs/image_aspect.hpp"
#include "vultra/core/rhi/structs/job_info.hpp"
#include "vultra/core/rhi/structs/raytracing_instance.hpp"
#include "vultra/core/rhi/structs/raytracing_pipeline_properties.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"
#include "vultra/core/rhi/structs/render_mesh.hpp"
#include "vultra/core/rhi/structs/sampler_info.hpp"
#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/rhi/uniform_buffer.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <vbase/core/scoped_enum_flags.hpp>

#include <glm/fwd.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
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
        class RenderDeviceAccess;

        class RenderDevice final
        {
            friend class GraphicsPipeline;
            friend class RayTracingPipeline;
            friend class RadixSorter;
            friend class DescriptorSetBuilder;
            friend class RenderDeviceAccess;
            friend class vultra::ImGuiSystem;
            friend class openxr::XRHeadset;

        public:
            explicit RenderDevice(RenderDeviceFeatureFlagBits,
                                  std::string_view             appName                    = "Untitled Vultra App",
                                  std::span<const char* const> requiredInstanceExtensions = {},
                                  RenderBackendApi             backendApi                 = RenderBackendApi::eAuto,
                                  bool enableValidation = defaultRenderDiagnosticsEnabled(),
                                  bool enableDebugMarkers = defaultRenderDiagnosticsEnabled(),
                                  bool enableRenderDoc = defaultRenderDiagnosticsEnabled());
            RenderDevice(const RenderDevice&)     = delete;
            RenderDevice(RenderDevice&&) noexcept = delete;
            ~RenderDevice();

            RenderDevice& operator=(const RenderDevice&)     = delete;
            RenderDevice& operator=(RenderDevice&&) noexcept = delete;

            [[nodiscard]] RenderDeviceFeatureFlagBits  getFeatureFlag() const;
            [[nodiscard]] RenderDeviceFeatureReport    getFeatureReport() const;
            [[nodiscard]] RenderDeviceLimits           getLimits() const;
            [[nodiscard]] RenderDeviceSyncCapabilities getSyncCapabilities() const;
            [[nodiscard]] RenderBackendApi             getBackendApi() const;
            [[nodiscard]] bool                         supportsSwapchain() const;

            [[nodiscard]] std::string getName() const;

            [[nodiscard]] PhysicalDeviceInfo getPhysicalDeviceInfo() const;

            void   beginFrameGpuQuery(CommandBuffer& cb);
            void   endFrameGpuQuery(CommandBuffer& cb);
            [[nodiscard]] double consumeGpuFrameMs();
            [[nodiscard]] uint64_t beginScopeGpuQuery(CommandBuffer& cb);
            [[nodiscard]] uint64_t beginScopeGpuQuery(std::uintptr_t commandBufferHandle);
            void                   endScopeGpuQuery(CommandBuffer& cb, uint64_t scopeToken);
            void                   endScopeGpuQuery(std::uintptr_t commandBufferHandle, uint64_t scopeToken);
            [[nodiscard]] double   consumeScopeGpuMs(uint64_t scopeToken);
            [[nodiscard]] RenderDeviceMemoryStats getMemoryStats() const;

            [[nodiscard]] std::array<float, 2> getLineWidthRange() const;
            [[nodiscard]] float                getMaxSamplerAnisotropy() const;
            [[nodiscard]] uint64_t             getFormatFeatureFlagsOptimal(PixelFormat) const;

            [[nodiscard]] Swapchain createSwapchain(os::Window&,
                                                    SwapchainFormat = SwapchainFormat::esRGB,
                                                    VerticalSync    = VerticalSync::eDisabled) const;

            [[nodiscard]] FenceHandle     createFence(bool signaled = true) const;
            [[nodiscard]] SemaphoreHandle createSemaphore();

            [[nodiscard]] Buffer createStagingBuffer(uint64_t size, const void* data = nullptr) const;

            [[nodiscard]] VertexBuffer
            createVertexBuffer(Buffer::Stride, uint64_t vertexCount, AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] IndexBuffer
            createIndexBuffer(IndexType, uint64_t indexCount, AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] UniformBuffer createUniformBuffer(uint64_t size,
                                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] StorageBuffer createStorageBuffer(uint64_t size,
                                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] StorageBuffer createStorageBufferWithUsage(uint64_t    size,
                                                                     BufferUsage extraUsage,
                                                                     AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] DrawIndirectBuffer
            createDrawIndirectBufferByCount(uint32_t         commandCount,
                                            DrawIndirectType type,
                                            AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] DrawIndirectBuffer
            createDrawIndirectBufferBySize(uint64_t         size,
                                           DrawIndirectType type,
                                           AllocationHints = AllocationHints::eNone) const;

            [[nodiscard]] PipelineLayout createPipelineLayout(const PipelineLayoutInfo&);

            [[nodiscard]] Texture
            createTexture2D(Extent2D, PixelFormat, uint32_t numMipLevels, uint32_t numLayers, ImageUsage) const;

            [[nodiscard]] Texture
            createTexture3D(Extent2D, uint32_t depth, PixelFormat, uint32_t numMipLevels, ImageUsage) const;

            [[nodiscard]] Texture
            createCubemap(uint32_t size, PixelFormat, uint32_t numMipLevels, uint32_t numLayers, ImageUsage) const;

            RenderDevice&               setupSampler(Texture&, SamplerInfo);
            [[nodiscard]] Sampler       getSampler(const SamplerInfo&);
            [[nodiscard]] SamplerHandle getSamplerHandle(const Sampler&) const;

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

            [[nodiscard]] ComputePipeline createComputePipelineBuiltin(const SPIRV&            spv,
                                                                       std::optional<PipelineLayout> = std::nullopt,
                                                                       const ShaderReflection*       reflection = nullptr);
            [[nodiscard]] ComputePipeline createComputePipelineBuiltin(const ShaderLibraryRuntime::LoadedShader&,
                                                                       std::optional<PipelineLayout> = std::nullopt);

            // Direct mapping without staging buffer. Use with host-coherent memory or persistent mapped memory.
            RenderDevice& upload(Buffer&, uint64_t offset, uint64_t size, const void* data);

            // Upload with staging buffer. Use for non-host-visible memory. This is a helper that creates a staging
            // buffer and performs a synchronous submit/wait. Initialization/setup only, not per-frame pass execution.
            RenderDevice& uploadS(Buffer&, uint64_t offset, uint64_t size, const void* data);

            // Upload draw indirect commands.
            RenderDevice& uploadDrawIndirect(DrawIndirectBuffer&, const std::vector<DrawIndirectCommand>& commands);

            RenderDevice& destroy(FenceHandle&);
            RenderDevice& destroy(SemaphoreHandle&);

            [[nodiscard]] CommandBuffer createCommandBuffer() const;
            // Blocking.
            RenderDevice& execute(const std::function<void(CommandBuffer&)>&, bool oneTime = false);
            RenderDevice& execute(CommandBuffer&, const JobInfo& = {}, bool oneTime = false);

            RenderDevice& present(Swapchain&, SemaphoreHandle wait = {});

            RenderDevice& wait(FenceHandle);
            RenderDevice& reset(FenceHandle);

            RenderDevice& waitIdle();

            bool saveTextureToFile(const Texture&         texture,
                                   const std::string&     filePath,
                                   const rhi::ImageAspect imageAspect = rhi::ImageAspect::eColor);
            [[nodiscard]] std::optional<std::vector<uint8_t>> readTextureRGBA8(const Texture& texture);
            [[nodiscard]] std::optional<std::array<uint8_t, 4>>
            readTexturePixelRGBA8(const Texture& texture, uint32_t x, uint32_t y);

            // For the RTX
            // General for ray query
            [[nodiscard]] AccelerationStructure
            createAccelerationStructure(AccelerationStructureType           type,
                                        AccelerationStructureBuildSizesInfo buildSizesInfo) const;

            // For single geometry BLAS, e.g., triangle
            [[nodiscard]] AccelerationStructure createBuildSingleGeometryBLAS(DeviceAddress vertexBufferAddress,
                                                                              DeviceAddress indexBufferAddress,
                                                                              DeviceAddress transformBufferAddress,
                                                                              uint32_t      vertexStride,
                                                                              uint32_t      vertexCount,
                                                                              uint32_t      indexCount);

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

            [[nodiscard]] DeviceAddress getBufferDeviceAddress(const Buffer&) const;

            [[nodiscard]] RayTracingPipelineProperties getRayTracingPipelineProperties() const;

            // For the OpenXR
            openxr::XRDevice* getXRDevice() const;

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

            [[nodiscard]] DescriptorSetLayoutKey
                                         createDescriptorSetLayout(const std::vector<DescriptorSetLayoutBindingEx>&);
            [[nodiscard]] std::uintptr_t getDescriptorSetLayoutHandle(DescriptorSetLayoutKey) const;

            std::uintptr_t allocateCommandBuffer() const;
            SamplerHandle  createSampler(const SamplerInfo&) const;

            [[nodiscard]] AccelerationStructureBuffer
            createAccelerationStructureBuffer(uint64_t size, AllocationHints = AllocationHints::eNone) const;

            DeviceAddress getAccelerationStructureDeviceAddress(const AccelerationStructure&) const;

            StrideDeviceAddressRegion
            getSbtEntryStrideDeviceAddressRegion(const Buffer& sbt, uint32_t handleCount, DeviceAddress offset) const;

        private:
            std::unique_ptr<IRenderDevice> m_Backend;
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
