// AssetSystem registry / import / resolution translation unit.
//
// Split out of asset_system.cpp (see ai/workspace/asset-system-split-plan.md): the registry
// configuration + mount, asset (re)import scan, registry reload, and URI/UUID resolution.
// These own the VAssetRegistry / VUUIDResolver / VFS members; the importer-options helper is
// used only here. Pure code-move; AssetSystem keeps lifecycle, residency, async CPU loading
// and text/binary I/O.

#include "vultra/function/asset/asset_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/builtin/builtin_resources.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/render_mesh.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"
#include "vultra/function/asset/asset_memory_estimate.hpp"
#include "vultra/function/asset/builtin_assets.hpp"
#include "vultra/function/asset/builtin_assets_io.hpp"
#include "vultra/function/asset/builtin_resource_ids.hpp"
#include "vultra/function/asset/imported_material_path.hpp"
#include "vultra/function/asset/mesh_vertex_packing.hpp"
#include "vultra/function/material/material_asset.hpp"
#include "vultra/function/material/material_params.hpp"
#include "vultra/function/rendering/srp/builtin/builtin_rendergraph_registry.hpp"
#include "vultra/function/resource/vtexture_loader.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#ifdef VULTRA_HAS_VASSET_IMPORT
#include "vultra/core/builtin/builtin_resources.hpp"

#include <vasset/editor_filesystem.hpp>
#include <vasset/vasset_importers.hpp>
#endif
#include <vasset/vanimation.hpp>
#include <vasset/vgaussiansplat.hpp>
#include <vasset/vmaterial.hpp>

#include <vfilesystem/backends/physical_filesystem.hpp>

#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <glm/gtc/packing.hpp>
#include <glm/gtx/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace vultra
{
    // Builtin-asset URI classification/loading helpers (builtin_assets_io.{hpp,cpp}); the URI/UUID
    // resolvers below call them unqualified, mirroring asset_system.cpp.
    using namespace asset_io;

    namespace
    {
        using namespace resource;
        using namespace rhi;
        using namespace vasset;

#ifdef VULTRA_HAS_VASSET_IMPORT
        vasset::VAssetImporter::ImportOptions
        makeAssetImportOptions(const bool                    importShaderLibraries = true,
                               std::vector<AssetDiagnostic>* diagnostics           = nullptr)
        {
            vasset::VAssetImporter::ImportOptions options;
            options.importShaderLibraries = importShaderLibraries;
            if (diagnostics)
            {
                options.diagnostics =
                    [diagnostics](const vasset::VAssetImporter::ImportOptions::Diagnostic& diagnostic) {
                        diagnostics->push_back(AssetDiagnostic {
                            .path    = diagnostic.path,
                            .line    = diagnostic.line,
                            .column  = diagnostic.column,
                            .message = diagnostic.message,
                        });
                    };
            }
            // Builtin GLSL includes are provided to the importer as a single VFS
            // mount (see runShaderCompiler). Each include is exposed once by its
            // canonical "include/..." path; the VFS resolves it by absolute path
            // from any shader directory, so no per-path duplication is needed.
            for (auto& [virtualPath, sourceText] : builtin::shaderIncludeSources())
            {
                options.shaderVirtualIncludes.push_back({
                    .virtualPath = std::move(virtualPath),
                    .sourceText  = std::move(sourceText),
                });
            }
            return options;
        }
#endif
    } // namespace

    void AssetSystem::configure(const AssetSystemDesc& desc)
    {
        if (auto* backend = ctx().services.tryGet<IRenderBackendService>())
            backend->renderDevice().waitIdle();

        waitForCpuLoadTasks();
        {
            std::scoped_lock lock(m_UploadQueueMutex);
            m_UploadQueue.clear();
        }
        m_MeshCache.clear();
        m_TextureCache.clear();
        m_GaussianSplatCache.clear();
        m_TexUUIDToBindlessIndex.clear();
        m_PendingMaterialRefreshes.clear();
        if (m_GpuResourceService)
            m_GpuResourceService->pool().clear();

        m_Desc = desc;
        {
            std::scoped_lock lock(m_TextOverrideMutex);
            m_TextAssetOverrides.clear();
        }
        if (m_Desc.assetRoot.empty())
            m_Desc.assetRoot = ctx().config.asset.assetRoot;
        if (m_Desc.importedFolder.empty())
            m_Desc.importedFolder = ctx().config.asset.importedFolder;
        if (m_Desc.registryFile.empty())
            m_Desc.registryFile = ctx().config.asset.registryFile;
        if (m_Desc.scheme.empty())
            m_Desc.scheme = "res";
        if (m_Desc.vpkFile.empty())
            m_Desc.vpkFile = ctx().config.asset.vpkFile;

        const auto resolveVpkPath = [&](const std::string& p) {
            std::filesystem::path path {p};
#if defined(__EMSCRIPTEN__)
            // wasm preloads VPK at "/resources.vpk", keep this deterministic.
            if (path.is_relative())
                path = std::filesystem::path("/") / path;
#endif
            return path.lexically_normal();
        };

        m_Registry.setAssetRootPath(m_Desc.assetRoot);
        m_Registry.setImportedFolderName(m_Desc.importedFolder);

        if (ctx().config.asset.loadFromVPK)
        {
            // For production, mount the VPK file (read-only). The VPK's embedded registry is the source of truth
            // for UUID -> source path mapping.
            const auto vpkPath       = resolveVpkPath(m_Desc.vpkFile).generic_string();
            auto       vpkFileSystem = createRef<vasset::VpkFileSystem>(vpkPath);
            auto       openResult    = vpkFileSystem->openPackage();
            if (!openResult)
            {
                VULTRA_CORE_ERROR("[AssetSystem] Failed to open VPK file: {}", vpkPath);
                return;
            }
            m_Desc.vpkFile = vpkPath;

            m_Registry = vasset::VAssetRegistry {};
            m_Registry.setAssetRootPath(m_Desc.assetRoot);
            m_Registry.setImportedFolderName(m_Desc.importedFolder);

            const auto& vpk = vpkFileSystem->getVpk();
            for (const auto& entry : vpk.registry)
            {
                if (entry.pathOffset + entry.pathSize > vpk.stringTable.size())
                    continue;

                const char*       s = vpk.stringTable.data() + entry.pathOffset;
                const std::string logicalPath(s, entry.pathSize);
                const std::string ext = std::filesystem::path(logicalPath).extension().generic_string();

                vasset::VAssetType inferredType = vasset::VAssetType::eUnknown;
                if (ext == ".vscn")
                    inferredType = vasset::VAssetType::eScene;
                else if (ext == ".vmanifest" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj" ||
                         ext == ".dae")
                    inferredType = vasset::VAssetType::eSceneManifest;
                else if (ext == ".lua")
                    inferredType = vasset::VAssetType::eScriptLua;
                else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
                         ext == ".gif" || ext == ".psd" || ext == ".pic" || ext == ".hdr" || ext == ".ktx" ||
                         ext == ".dds" || ext == ".ktx2")
                    inferredType = vasset::VAssetType::eTexture;
                else if (ext == ".ply" || ext == ".spz" || ext == ".splat" || ext == ".ksplat")
                    inferredType = vasset::VAssetType::eGaussianSplat;
                else if (logicalPath.rfind(m_Desc.importedFolder + "/mesh/", 0) == 0)
                    inferredType = vasset::VAssetType::eMesh;

                const auto registryType = entry.type == vasset::VAssetType::eUnknown ? inferredType : entry.type;
                m_Registry.registerAsset(entry.uuid, logicalPath, logicalPath, registryType);
            }

            m_Resolver.loadFromVPK(vpk);
            m_VFS.mount(vpkFileSystem, m_Desc.scheme);
        }
        else
        {
            // For development, mount the editor remap filesystem, which allows transparent access to source assets and
            // imported assets. Runtime-only builds mount the physical filesystem directly and expect pre-baked assets.
            auto registryPath = (std::filesystem::path(m_Desc.assetRoot) / m_Desc.importedFolder / m_Desc.registryFile)
                                    .generic_string();
            const bool hasRegistryFile = std::filesystem::exists(registryPath);
            const bool loadedRegistry  = hasRegistryFile && m_Registry.load(registryPath);
            if (loadedRegistry)
            {
                const auto beforeCleanup = m_Registry.getRegistry().size();
                m_Registry.cleanup();
                const auto afterCleanup = m_Registry.getRegistry().size();
                if (afterCleanup < beforeCleanup)
                {
                    VULTRA_CORE_INFO("[AssetSystem] Removed {} stale asset registry entr{} from '{}'",
                                     beforeCleanup - afterCleanup,
                                     beforeCleanup - afterCleanup == 1 ? "y" : "ies",
                                     registryPath);
                    if (!m_Registry.save(registryPath))
                    {
                        VULTRA_CORE_WARN("[AssetSystem] Failed to save reconciled asset registry: {}", registryPath);
                    }
                }
            }
            if (m_Desc.enableImportScan && (!loadedRegistry || m_Registry.getRegistry().empty()))
            {
                if (!loadedRegistry)
                {
                    VULTRA_CORE_WARN("[AssetSystem] Failed to load asset registry from file: {}", registryPath);
                }
                else
                {
                    VULTRA_CORE_WARN("[AssetSystem] Asset registry is empty, rebuilding: {}", registryPath);
                }
#ifdef VULTRA_HAS_VASSET_IMPORT
                vasset::VAssetImporter importer {m_Registry};
                importer.setOptions(makeAssetImportOptions(false));
                importer.importOrReimportAssetFolder(m_Desc.assetRoot);
                m_Registry.save(registryPath);
#else
                VULTRA_CORE_WARN("[AssetSystem] Runtime-only vasset build cannot auto-import source assets. "
                                 "Expect pre-baked assets or an existing registry.");
#endif
            }
            else
            {
                if (loadedRegistry)
                {
                    VULTRA_CORE_INFO("[AssetSystem] Loaded asset registry from file: {}", registryPath);
                }
                else
                {
                    VULTRA_CORE_WARN("[AssetSystem] Asset import scan disabled and registry is unavailable: {}",
                                     registryPath);
                }
            }

#ifdef VULTRA_HAS_VASSET_IMPORT
            if (m_Desc.enableImportScan)
            {
                vasset::VAssetImporter importer {m_Registry};
                importer.setOptions(makeAssetImportOptions(false));
                auto importResult = importer.importOrReimportAssetFolder(m_Desc.assetRoot, false);
                if (!importResult)
                {
                    VULTRA_CORE_WARN("[AssetSystem] Asset import scan failed while checking stale editor assets.");
                }
                m_Registry.save(registryPath);
            }
#endif

            m_Resolver.loadFromAssetRegistry(m_Registry);

#ifdef VULTRA_HAS_VASSET_IMPORT
            m_VFS.mount(createRef<vasset::EditorRemapFileSystem>(
                            createRef<vfilesystem::PhysicalFileSystem>(vfilesystem::Path {m_Desc.assetRoot})),
                        m_Desc.scheme);
#else
            m_VFS.mount(createRef<vfilesystem::PhysicalFileSystem>(vfilesystem::Path {m_Desc.assetRoot}),
                        m_Desc.scheme);
#endif

            // Managed plugin store: plugins://<id>/<version>/<path> maps straight onto
            // <project>/.vultra/plugins/<id>/<version>/<path>, so plugin content (render pass
            // scripts, shader artifacts, data) resolves through the VFS even though managed
            // plugins live outside the asset root.
            if (const auto& managedRoot = ctx().config.plugin.managedRoot; !managedRoot.empty())
                m_VFS.mount(createRef<vfilesystem::PhysicalFileSystem>(vfilesystem::Path {managedRoot}), "plugins");
        }

        m_Resolver.setScheme(m_Desc.scheme);

        if (!m_RenderDevice || !m_GpuResourceService)
        {
            VULTRA_CORE_INFO("[AssetSystem] Asset registry configured. Registry entries: {}",
                             m_Registry.getRegistry().size());
            return;
        }

        auto& pool = m_GpuResourceService->pool();

        // Global bindless texture table: reserve slot 0 as fallback.
        pool.ensureBindlessSlot0(*m_RenderDevice);

        // Reset global material param pool.
        pool.materialParams.reset();

        VULTRA_CORE_INFO("[AssetSystem] Asset registry configured. Registry entries: {}",
                         m_Registry.getRegistry().size());
    }

    bool AssetSystem::resolveUUIDToUri(const CoreUUID& uuid, std::string& outUri) const
    {
        if (const auto builtinUri = builtinTextureUriForUuid(uuid); !builtinUri.empty())
        {
            outUri = builtinUri;
            return true;
        }

        if (const auto builtinFontUri = builtinFontUriForUuid(uuid); !builtinFontUri.empty())
        {
            outUri = builtinFontUri;
            return true;
        }

        auto entry = m_Registry.lookup(uuid);
        if (entry.type == vasset::VAssetType::eUnknown)
            return false;

        const bool cookedOnly = entry.type == vasset::VAssetType::eMesh || entry.type == vasset::VAssetType::eTexture ||
                                entry.type == vasset::VAssetType::eSkeleton ||
                                entry.type == vasset::VAssetType::eAnimation;
        const std::string& path = cookedOnly && !entry.importedPath.empty() ? entry.importedPath :
                                  !entry.sourcePath.empty()                 ? entry.sourcePath :
                                                                              entry.importedPath;
        outUri                  = m_Desc.scheme + "://" + path;
        return true;
    }

    bool AssetSystem::resolveUriToUUID(std::string_view uri, CoreUUID& outUUID) const
    {
        if (isBuiltinTextureUri(uri))
        {
            outUUID = builtinTextureUuidForUri(uri);
            return true;
        }

        if (isBuiltinFontUri(uri))
        {
            outUUID = builtinFontUuidForUri(uri);
            return true;
        }

        return m_Resolver.reverseResolve(uri, outUUID);
    }

    std::string AssetSystem::resolveUri(const std::string_view uri) const
    {
        auto vbaseUri = vfilesystem::parse_uri(uri);
        return m_Desc.assetRoot + vbaseUri.path.str().data();
    }

    bool AssetSystem::resolveAssetUri(const CoreUUID& uuid, std::string& outUri) const
    {
        return resolveUUIDToUri(uuid, outUri);
    }

    bool AssetSystem::reimportAsset(std::string_view uri, const bool forceReimport)
    {
#ifdef VULTRA_HAS_VASSET_IMPORT
        if (ctx().config.asset.loadFromVPK)
            return false;

        const auto             physicalPath = std::filesystem::path(resolveUri(uri)).lexically_normal();
        vasset::VAssetImporter importer {m_Registry};
        m_LastImportDiagnostics.clear();
        importer.setOptions(makeAssetImportOptions(true, &m_LastImportDiagnostics));
        auto result = importer.importOrReimportAsset(physicalPath.generic_string(), forceReimport);
        if (!result)
        {
            VULTRA_CORE_ERROR("[AssetSystem] Failed to reimport asset '{}'", uri);
            return false;
        }

        std::error_code ec;
        const auto      sourceRelativePath = std::filesystem::relative(physicalPath, m_Desc.assetRoot, ec);
        if (!ec && !sourceRelativePath.empty())
        {
            const auto relativeText = sourceRelativePath.generic_string();
            if (!relativeText.starts_with("../"))
                emitImportedMaterialAssets(relativeText);
        }

        const auto registryPath =
            (std::filesystem::path(m_Desc.assetRoot) / m_Desc.importedFolder / m_Desc.registryFile).generic_string();
        m_Registry.save(registryPath);
        m_Resolver.loadFromAssetRegistry(m_Registry);
        m_Resolver.setScheme(m_Desc.scheme);
        return true;
#else
        static_cast<void>(uri);
        static_cast<void>(forceReimport);
        return false;
#endif
    }

    bool AssetSystem::reloadRegistry()
    {
        if (ctx().config.asset.loadFromVPK)
            return false;

        const auto registryPath =
            (std::filesystem::path(m_Desc.assetRoot) / m_Desc.importedFolder / m_Desc.registryFile).generic_string();
        vasset::VAssetRegistry registry;
        registry.setAssetRootPath(m_Desc.assetRoot);
        registry.setImportedFolderName(m_Desc.importedFolder);
        if (!registry.load(registryPath))
        {
            VULTRA_CORE_WARN("[AssetSystem] Failed to reload asset registry: {}", registryPath);
            return false;
        }

        const auto beforeCleanup = registry.getRegistry().size();
        registry.cleanup();
        if (registry.getRegistry().size() < beforeCleanup && !registry.save(registryPath))
            VULTRA_CORE_WARN("[AssetSystem] Failed to save cleaned asset registry: {}", registryPath);

        m_Registry = std::move(registry);
        m_Resolver.loadFromAssetRegistry(m_Registry);
        m_Resolver.setScheme(m_Desc.scheme);
        VULTRA_CORE_INFO("[AssetSystem] Reloaded asset registry. Registry entries: {}",
                         m_Registry.getRegistry().size());
        return true;
    }
} // namespace vultra
