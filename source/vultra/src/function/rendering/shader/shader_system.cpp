#include "vultra/function/rendering/shader_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/builtin/builtin_resources.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/services/asset_service.hpp"

#include <vasset/vasset_type.hpp>

#include <cstddef>
#include <vector>

namespace vultra
{
    namespace
    {
        // Load a builtin shader library from the mounted builtin:: pack. Self-contained binaries
        // mount the pack (vultra.builtin_pack rule); the export-template runtime gets it from the
        // project VPK. No embedded fallback -- the byte arrays no longer compile into the binary.
        bool loadBuiltinShaderLib(rhi::ShaderLibraryRuntime& lib, std::string_view logicalPath)
        {
            std::vector<std::byte> bytes;
            if (!builtin::read(logicalPath, bytes) || bytes.empty())
            {
                VULTRA_CORE_ERROR("[ShaderSystem] builtin shader library '{}' missing from builtin pack", logicalPath);
                return false;
            }
            VULTRA_CORE_TRACE("[ShaderSystem] {} from builtin pack ({} bytes)", logicalPath, bytes.size());
            return lib.loadFromMemory(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        }

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
            // WebGPU runs the unified DEFERRED path, so it loads the highend web lib (deferred passes +
            // shared general passes). eHighend resolves the deferred shaders; eGeneral/default resolve the
            // shared post shaders from the same lib.
            if (!loadBuiltinShaderLib(m_BuiltinHighendShaderLibrary, "shaders/builtin_highend.vshweblib"))
            {
                VULTRA_CORE_ERROR("[ShaderSystem] Failed to load WebGPU highend builtin shader library");
                return false;
            }
            m_DefaultBuiltinShaderLibrary = &m_BuiltinHighendShaderLibrary;
        }
        else
        {
#if defined(__ANDROID__)
            if (!loadBuiltinShaderLib(m_BuiltinCompatibilityShaderLibrary, "shaders/builtin_compatibility.vshlib"))
            {
                VULTRA_CORE_ERROR("[ShaderSystem] Failed to load Android compatibility builtin shader library");
                return false;
            }
            m_DefaultBuiltinShaderLibrary = &m_BuiltinCompatibilityShaderLibrary;
#else
            if (!loadBuiltinShaderLib(m_BuiltinHighendShaderLibrary, "shaders/builtin_highend.vshlib"))
            {
                VULTRA_CORE_ERROR("[ShaderSystem] Failed to load highend builtin shader library");
                return false;
            }
            if (!loadBuiltinShaderLib(m_BuiltinCompatibilityShaderLibrary, "shaders/builtin_compatibility.vshlib"))
            {
                VULTRA_CORE_ERROR("[ShaderSystem] Failed to load compatibility builtin shader library");
                return false;
            }
            m_DefaultBuiltinShaderLibrary =
                useCompatibilityLibrary ? &m_BuiltinCompatibilityShaderLibrary : &m_BuiltinHighendShaderLibrary;
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

    void ShaderSystem::setRenderPassDiagnostics(const std::string& sourcePath, std::vector<AssetDiagnostic> diagnostics)
    {
        if (sourcePath.empty())
            return;

        const auto it  = m_RenderPassDiagnostics.find(sourcePath);
        const bool had = it != m_RenderPassDiagnostics.end();

        if (diagnostics.empty())
        {
            if (had)
            {
                VULTRA_CORE_INFO("[RenderPass] '{}': diagnostics cleared", sourcePath);
                m_RenderPassDiagnostics.erase(it);
            }
            return;
        }

        // Log only when the diagnostic set changes (setup runs every frame; this
        // avoids spamming the log with the same unresolved error).
        bool changed = !had || it->second.size() != diagnostics.size();
        for (size_t i = 0; !changed && i < diagnostics.size(); ++i)
            changed = it->second[i].message != diagnostics[i].message || it->second[i].line != diagnostics[i].line;
        if (changed)
            for (const auto& diagnostic : diagnostics)
                VULTRA_CORE_ERROR("[RenderPass] {}:{}: {}", diagnostic.path, diagnostic.line, diagnostic.message);

        m_RenderPassDiagnostics[sourcePath] = std::move(diagnostics);
    }

    void ShaderSystem::clearRenderPassDiagnostics() { m_RenderPassDiagnostics.clear(); }

    std::vector<AssetDiagnostic> ShaderSystem::renderPassDiagnostics() const
    {
        std::vector<AssetDiagnostic> out;
        for (const auto& [path, list] : m_RenderPassDiagnostics)
        {
            static_cast<void>(path);
            out.insert(out.end(), list.begin(), list.end());
        }
        return out;
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

        // The project shader library is an optional override. A reload retries (the asset may have
        // appeared); otherwise honor the cached "absent" result instead of probing the VFS again.
        if (forceReload)
            m_MissingProjectLibraries.erase(std::string(uri));
        else if (m_MissingProjectLibraries.contains(std::string(uri)))
            return nullptr;

        auto* assetService = ctx().services.tryGet<IAssetService>();
        if (!assetService)
        {
            VULTRA_CORE_ERROR("[ShaderSystem] Cannot load project shader library '{}': asset service unavailable", uri);
            return nullptr;
        }

        if (forceReload)
            assetService->reimportAsset(uri, false);

        std::string runtimeUri              = std::string(uri);
        const auto  logicalPath             = logicalPathFromUri(uri);
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
            // Asset simply not present: not an error. Callers fall back to the builtin library.
            // Cache the miss so per-frame lookups don't keep probing the VFS or spamming logs.
            VULTRA_CORE_TRACE("[ShaderSystem] Project shader library '{}' not available; using builtin shaders ({}).",
                              uri,
                              std::move(bytes).error());
            m_MissingProjectLibraries.insert(std::string(uri));
            return nullptr;
        }

        rhi::ShaderLibraryRuntime library;
        const auto&               data = bytes.value();
        if (!library.loadFromMemory(data.data(), data.size()))
        {
            VULTRA_CORE_ERROR("[ShaderSystem] Failed to parse project shader library '{}'", uri);
            return nullptr;
        }

        const auto key      = std::string(uri);
        auto [it, inserted] = m_ProjectShaderLibraries.insert_or_assign(key, std::move(library));
        VULTRA_CORE_INFO("[ShaderSystem] {} project shader library '{}'", inserted ? "Loaded" : "Reloaded", uri);
        return &it->second;
    }
} // namespace vultra
