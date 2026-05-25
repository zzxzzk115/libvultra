#include "vultra/function/rendering/shader_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/services/asset_service.hpp"

#include <builtin_shaders.hpp>

#include <vasset/vasset_type.hpp>

namespace vultra
{
    namespace
    {
        [[nodiscard]] std::string logicalPathFromUri(std::string_view uri)
        {
            constexpr std::string_view kResPrefix = "res://";
            if (uri.starts_with(kResPrefix))
                return std::string(uri.substr(kResPrefix.size()));
            return std::string(uri);
        }

        [[nodiscard]] std::string uriFromLogicalPath(const std::string& logicalPath)
        {
            return logicalPath.rfind("res://", 0) == 0 ? logicalPath : "res://" + logicalPath;
        }

        [[nodiscard]] std::string webgpuLibraryPathFor(std::string path)
        {
            const auto dot = path.rfind('.');
            if (dot == std::string::npos)
                return path + ".vshweblib";
            return path.substr(0, dot) + ".vshweblib";
        }
    } // namespace

    bool ShaderSystem::onInit()
    {
        VULTRA_CORE_INFO("[ShaderSystem] Initializing...");

        const auto backendApi           = ctx().config.render.backendApi;
        const auto builtinShaderLibrary = ctx().config.render.builtinShaderLibrary;
        const bool useWebGpuLibrary     = backendApi == rhi::RenderBackendApi::eWebGPU;
        const bool useCompatibilityLibrary =
            builtinShaderLibrary == EngineContext::Config::RenderConfig::BuiltinShaderLibrary::eCompatibility;

        if (useWebGpuLibrary)
        {
            if (!m_BuiltinCompatibilityShaderLibrary.loadFromMemory(builtin_shaders_compatibility_web_vshweblib,
                                                                     builtin_shaders_compatibility_web_vshweblib_size))
            {
                VULTRA_CORE_ERROR("[ShaderSystem] Failed to load WebGPU compatibility builtin shader library");
                return false;
            }
            m_DefaultBuiltinShaderLibrary = &m_BuiltinCompatibilityShaderLibrary;
        }
        else
        {
#if defined(__ANDROID__)
            if (!m_BuiltinCompatibilityShaderLibrary.loadFromMemory(builtin_shaders_compatibility_vshlib,
                                                                     builtin_shaders_compatibility_vshlib_size))
            {
                VULTRA_CORE_ERROR("[ShaderSystem] Failed to load Android compatibility builtin shader library");
                return false;
            }
            m_DefaultBuiltinShaderLibrary = &m_BuiltinCompatibilityShaderLibrary;
#else
            if (!m_BuiltinHighendShaderLibrary.loadFromMemory(builtin_shaders_highend_vshlib,
                                                               builtin_shaders_highend_vshlib_size))
            {
                VULTRA_CORE_ERROR("[ShaderSystem] Failed to load highend builtin shader library");
                return false;
            }
            if (!m_BuiltinCompatibilityShaderLibrary.loadFromMemory(builtin_shaders_compatibility_vshlib,
                                                                     builtin_shaders_compatibility_vshlib_size))
            {
                VULTRA_CORE_ERROR("[ShaderSystem] Failed to load compatibility builtin shader library");
                return false;
            }
            m_DefaultBuiltinShaderLibrary = useCompatibilityLibrary ? &m_BuiltinCompatibilityShaderLibrary :
                                                              &m_BuiltinHighendShaderLibrary;
#endif
        }

        VULTRA_CORE_TRACE("[ShaderSystem] Providing IShaderService");
        ctx().services.provide<IShaderService>(this);

        return true;
    }

    void ShaderSystem::onShutdown() { VULTRA_CORE_INFO("[ShaderSystem] Shutting down"); }

    rhi::ShaderLibraryRuntime& ShaderSystem::builtinLibrary(const rhi::ShaderProfile profile)
    {
        switch (profile)
        {
            case rhi::ShaderProfile::eHighend:
                return m_BuiltinHighendShaderLibrary;
            case rhi::ShaderProfile::eCompatibility:
                return m_BuiltinCompatibilityShaderLibrary;
            case rhi::ShaderProfile::eGeneral:
            case rhi::ShaderProfile::eUnspecified:
            default:
                return builtinLibrary();
        }
    }

    rhi::ShaderLibraryRuntime* ShaderSystem::findProjectLibrary(std::string_view uri)
    {
        const auto it = m_ProjectShaderLibraries.find(std::string(uri));
        return it != m_ProjectShaderLibraries.end() ? &it->second : nullptr;
    }

    rhi::ShaderLibraryRuntime* ShaderSystem::loadProjectLibrary(std::string_view uri)
    {
        return loadProjectLibraryImpl(uri, false);
    }

    rhi::ShaderLibraryRuntime* ShaderSystem::reloadProjectLibrary(std::string_view uri)
    {
        return loadProjectLibraryImpl(uri, true);
    }

    rhi::ShaderLibraryRuntime* ShaderSystem::loadProjectLibraryImpl(std::string_view uri, const bool forceReload)
    {
        if (auto* existing = findProjectLibrary(uri))
        {
            if (!forceReload)
                return existing;
        }

        auto* assetService = ctx().services.tryGet<IAssetService>();
        if (!assetService)
        {
            VULTRA_CORE_ERROR("[ShaderSystem] Cannot load project shader library '{}': asset service unavailable", uri);
            return nullptr;
        }

        if (forceReload)
            assetService->reimportAsset(uri, false);

        std::string runtimeUri = std::string(uri);
        const auto  logicalPath = logicalPathFromUri(uri);
        bool        hasImportedRuntimeAsset = false;
        for (const auto& [_, entry] : assetService->registry().getRegistry())
        {
            static_cast<void>(_);
            if (entry.type == vasset::VAssetType::eShaderLibrary && entry.sourcePath == logicalPath &&
                !entry.importedPath.empty())
            {
                hasImportedRuntimeAsset = true;
                runtimeUri = uriFromLogicalPath(ctx().config.render.backendApi == rhi::RenderBackendApi::eWebGPU ?
                                                    webgpuLibraryPathFor(entry.importedPath) :
                                                    entry.importedPath);
                break;
            }
        }

        auto bytes = assetService->loadBinaryAssetSync(runtimeUri);
        if (!bytes && runtimeUri != uri && !hasImportedRuntimeAsset)
            bytes = assetService->loadBinaryAssetSync(uri);
        if (!bytes)
        {
            VULTRA_CORE_ERROR("[ShaderSystem] Cannot load project shader library '{}': {}", uri, std::move(bytes).error());
            return nullptr;
        }

        rhi::ShaderLibraryRuntime library;
        const auto& data = bytes.value();
        if (!library.loadFromMemory(data.data(), data.size()))
        {
            VULTRA_CORE_ERROR("[ShaderSystem] Failed to parse project shader library '{}'", uri);
            return nullptr;
        }

        const auto key = std::string(uri);
        auto [it, inserted] = m_ProjectShaderLibraries.insert_or_assign(key, std::move(library));
        VULTRA_CORE_INFO("[ShaderSystem] {} project shader library '{}'",
                         inserted ? "Loaded" : "Reloaded",
                         uri);
        return &it->second;
    }
} // namespace vultra
