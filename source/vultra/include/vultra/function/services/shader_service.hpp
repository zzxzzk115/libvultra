#pragma once

#include "vultra/core/rhi/base_pass.hpp"
#include "vultra/core/rhi/shader_library.hpp"
#include "vultra/function/services/asset_service.hpp" // AssetDiagnostic

#include <vbase/service/service_registry.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    class IShaderService
    {
    public:
        SERVICE_REGISTER(IShaderService)

        virtual rhi::ShaderLibraryRuntime& builtinLibrary() = 0;
        virtual rhi::ShaderLibraryRuntime& builtinLibrary(rhi::ShaderProfile profile) = 0;
        virtual rhi::ShaderLibraryRuntime* loadProjectLibrary(std::string_view uri) = 0;
        virtual rhi::ShaderLibraryRuntime* reloadProjectLibrary(std::string_view uri) = 0;
        virtual rhi::ShaderLibraryRuntime* findProjectLibrary(std::string_view uri) = 0;

        // Render-pass diagnostics, keyed by the pass's source .lua path. Covers
        // both pass-definition errors (set once at pipeline load) and setup-time
        // shader-resolution errors (self-healing: a pass overwrites its own entry
        // every frame its setup runs, an empty list clears it). The code editor
        // surfaces these for the open file. Producers also log them.
        virtual void setRenderPassDiagnostics(const std::string&           sourcePath,
                                              std::vector<AssetDiagnostic> diagnostics) = 0;
        virtual void                         clearRenderPassDiagnostics()               = 0;
        virtual std::vector<AssetDiagnostic> renderPassDiagnostics() const              = 0;
    };
} // namespace vultra
