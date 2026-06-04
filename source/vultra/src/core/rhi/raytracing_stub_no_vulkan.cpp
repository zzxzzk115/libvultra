// Ray-tracing RHI stubs for builds without the Vulkan backend (e.g. WebGPU / wasm).
//
// The acceleration-structure and ray-tracing-pipeline entry points on RenderDevice are
// implemented only by the Vulkan backend (backends/vk/**), which is excluded from builds with
// VULTRA_ENABLE_VULKAN == 0. WebGPU has no ray-tracing support, yet always-compiled engine code
// (asset/render systems, the RT passes) still references these symbols, so the link would fail
// without a definition. Provide inert definitions here so such builds link. They must never be
// reached at runtime: the ray-tracing feature flag is never enabled on a non-Vulkan device, and
// every caller is gated behind that flag.

#include "vultra/core/rhi/raytracing_pipeline.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include "vultra/core/base/common_context.hpp"

#if !(defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN)

namespace vultra::rhi
{
    AccelerationStructure RenderDevice::createBuildRenderMeshBLAS(std::vector<RenderSubMesh>&)
    {
        VULTRA_CORE_ERROR(
            "[RenderDevice] createBuildRenderMeshBLAS is unavailable: ray tracing requires the Vulkan backend.");
        return {};
    }

    AccelerationStructure RenderDevice::createBuildMultipleInstanceTLAS(const std::vector<RayTracingInstance>&)
    {
        VULTRA_CORE_ERROR(
            "[RenderDevice] createBuildMultipleInstanceTLAS is unavailable: ray tracing requires the Vulkan backend.");
        return {};
    }

    ShaderBindingTable RenderDevice::createShaderBindingTable(const RayTracingPipeline&, AllocationHints) const
    {
        VULTRA_CORE_ERROR(
            "[RenderDevice] createShaderBindingTable is unavailable: ray tracing requires the Vulkan backend.");
        return {};
    }

    RayTracingPipeline RayTracingPipeline::Builder::build(RenderDevice&)
    {
        VULTRA_CORE_ERROR("[RayTracingPipeline] build is unavailable: ray tracing requires the Vulkan backend.");
        return {};
    }
} // namespace vultra::rhi

#endif
