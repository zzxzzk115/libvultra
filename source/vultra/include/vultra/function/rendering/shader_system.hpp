#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/shader_service.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vultra
{
    class IAssetService;

    class ShaderSystem final : public EngineSubsystem, public IShaderService
    {
    public:
        ENGINE_SUBSYSTEM(ShaderSystem)

        bool onInit() override;
        void onShutdown() override;

    public:
        rhi::ShaderLibraryRuntime& builtinLibrary() override { return *m_DefaultBuiltinShaderLibrary; }
        rhi::ShaderLibraryRuntime& builtinLibrary(rhi::ShaderProfile profile) override;
        rhi::ShaderLibraryRuntime* loadProjectLibrary(std::string_view uri) override;
        rhi::ShaderLibraryRuntime* reloadProjectLibrary(std::string_view uri) override;
        rhi::ShaderLibraryRuntime* findProjectLibrary(std::string_view uri) override;

        void setRenderPassDiagnostics(const std::string&           sourcePath,
                                      std::vector<AssetDiagnostic> diagnostics) override;
        void                         clearRenderPassDiagnostics() override;
        std::vector<AssetDiagnostic> renderPassDiagnostics() const override;

    private:
        rhi::ShaderLibraryRuntime* loadProjectLibraryImpl(std::string_view uri, bool forceReload);

    private:
        rhi::ShaderLibraryRuntime  m_BuiltinHighendShaderLibrary;
        rhi::ShaderLibraryRuntime  m_BuiltinCompatibilityShaderLibrary;
        rhi::ShaderLibraryRuntime* m_DefaultBuiltinShaderLibrary {&m_BuiltinHighendShaderLibrary};
        std::unordered_map<std::string, rhi::ShaderLibraryRuntime> m_ProjectShaderLibraries;
        // URIs that resolved to "no asset present". The project shader library is an optional
        // per-project override; caching the miss avoids retrying the VFS lookup every frame and
        // keeps the logs quiet for projects/packages that don't ship one.
        std::unordered_set<std::string> m_MissingProjectLibraries;

        // Render-pass diagnostics (definition + shader-resolution), keyed by source .lua path.
        std::unordered_map<std::string, std::vector<AssetDiagnostic>> m_RenderPassDiagnostics;
    };
} // namespace vultra
