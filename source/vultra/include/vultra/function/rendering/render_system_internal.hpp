#pragma once

// Internal interface between render_system.cpp and the material-cooking translation unit
// material_cook.cpp (see ai/workspace/render-system-split-plan.md).
//
// material_cook.cpp owns the graph/shader/builtin material cooking helpers (~1.8k lines that
// turn a material URI or material-graph into a GpuMaterial). Only the handful of entry points
// below are called from render_system.cpp; every other cooking helper and the cooking caches
// stay file-local to material_cook.cpp. Definitions live there under namespace vultra::rsdetail;
// default arguments are declared here (once) and omitted from the definitions.

#include <nlohmann/json.hpp> // material params/overrides are passed as const nlohmann::json*

#include <cstdint>
#include <string>
#include <string_view>

namespace vultra
{
    class IAssetService;
    class IShaderService;
    class IGpuResourceService;

    namespace rhi
    {
        class RenderDevice;
    }

    namespace rsdetail
    {
        [[nodiscard]] uint32_t ensureMaterialGraphGpuMaterial(IAssetService&        assets,
                                                              IGpuResourceService&  gpuResources,
                                                              rhi::RenderDevice&    rd,
                                                              std::string_view      materialGraphUri,
                                                              float                 timeSeconds,
                                                              const nlohmann::json* properties  = nullptr,
                                                              std::string_view      materialKey = {});

        [[nodiscard]] uint32_t ensureBuiltinMaterialAssetGpuMaterial(IAssetService&        assets,
                                                                     IGpuResourceService&  gpuResources,
                                                                     rhi::RenderDevice&    rd,
                                                                     std::string_view      materialUri,
                                                                     const nlohmann::json* overrides = nullptr);

        [[nodiscard]] uint32_t ensureMaterialGraphShaderMaterial(IAssetService&        assets,
                                                                 IShaderService*       shaders,
                                                                 IGpuResourceService&  gpuResources,
                                                                 rhi::RenderDevice&    rd,
                                                                 std::string_view      graphUri,
                                                                 const nlohmann::json* properties,
                                                                 std::string_view      materialKey);

        [[nodiscard]] uint32_t ensureMaterialAssetGpuMaterial(IAssetService&        assets,
                                                              IShaderService*       shaders,
                                                              IGpuResourceService&  gpuResources,
                                                              rhi::RenderDevice&    rd,
                                                              std::string_view      materialUri,
                                                              float                 timeSeconds,
                                                              const nlohmann::json* overrides  = nullptr,
                                                              std::string_view      materialKey = {});

        [[nodiscard]] bool hasGraphConstantGpuMaterial(IGpuResourceService& gpuResources, std::string_view materialKey);

        [[nodiscard]] std::string lowerAscii(std::string_view text);
    } // namespace rsdetail
} // namespace vultra
