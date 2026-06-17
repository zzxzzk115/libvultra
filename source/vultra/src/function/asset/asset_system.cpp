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
    // Builtin-asset URI classification + loading was split out to builtin_assets_io.{hpp,cpp}; bring those
    // helpers into scope so the existing unqualified call sites keep resolving.
    using namespace asset_io;
    // Imported-material path helper shared with asset_material_upload.cpp (single source of truth).
    using asset_detail::importedMaterialAssetRelativePath;

    namespace
    {
        constexpr uint32_t kAssetLoadMaxAttempts = 3;

        template<class Record>
        void markAssetUsed(Record& record, const uint64_t frameIndex)
        {
            record.lastUsedFrame.store(frameIndex, std::memory_order_release);
        }

        template<class Cache>
        void releaseZeroRefCpuAssetCache(Cache& cache, const uint64_t frameIndex, const uint64_t idleFrames)
        {
            cache.forEachRecord([&](auto& record) {
                if (!record.cpu)
                    return;
                if (record.state.load(std::memory_order_acquire) != AssetState::eReady)
                    return;
                if (record.refCount.load(std::memory_order_acquire) != 0)
                    return;

                const uint64_t lastUsed = record.lastUsedFrame.load(std::memory_order_acquire);
                if (frameIndex < lastUsed || frameIndex - lastUsed < idleFrames)
                    return;

                record.cpu.reset();
                record.gpuIndex.store(std::numeric_limits<uint32_t>::max(), std::memory_order_release);
                record.state.store(AssetState::eUnloaded, std::memory_order_release);
            });
        }

        // Fixed builtin/imported PBR parameter block lives in
        // vultra/function/material/material_params.hpp. Shader-backed materials use
        // vshadersystem reflection offsets when their assets are resolved.

        // MaterialParamsPBRSG / MaterialParamsUnlit / MaterialParamsPhong now live in
        // vultra/function/material/material_params.hpp (single byte-layout source of
        // truth shared with the render system and the graph constant path).

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

        constexpr std::size_t kMaxUploadCommandsPerFrame        = 2;

        using namespace resource;
        using namespace rhi;
        using namespace vasset;

        // buildVertexAttributes / PackedVertexLayout / packVertices now live in
        // vultra/function/asset/mesh_vertex_packing.hpp.

        // estimateV*Bytes / stringBytes / vectorBytes now live in
        // vultra/function/asset/asset_memory_estimate.hpp.

        bool shouldReadPhysicalTextSourceDirectly(const std::filesystem::path& path)
        {
            auto ext = path.extension().generic_string();
            std::ranges::transform(
                ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

            const auto filename = path.filename().generic_string();
            return ext == ".vscn" || ext == ".vmanifest" || ext == ".lua" || ext == ".vmatgraph" ||
                   filename.ends_with(".vrg.json") || filename.ends_with(".vmatgraph.json") ||
                   filename.ends_with(".vmat.json") || filename.ends_with(".vmatnode.json") ||
                   filename.ends_with(".vshaderlib.lua") || filename.ends_with(".vso.lua") ||
                   filename.ends_with(".vsrp.lua") || filename.ends_with(".vfeature.lua");
        }
    } // namespace

    AssetSystem::~AssetSystem() = default;

    bool AssetSystem::onInit()
    {
        VULTRA_CORE_INFO("[AssetSystem] Initializing...");

        VULTRA_CORE_TRACE("[AssetSystem] Getting render backend");
        if (auto* backend = ctx().services.tryGet<IRenderBackendService>())
            m_RenderDevice = &backend->renderDevice();

        VULTRA_CORE_TRACE("[AssetSystem] Getting GPU resource service");
        m_GpuResourceService = ctx().services.tryGet<IGpuResourceService>();
        if (!m_RenderDevice || !m_GpuResourceService)
            VULTRA_CORE_INFO("[AssetSystem] GPU services unavailable; running CPU/text asset mode.");
        m_CpuLoadScheduler = std::make_unique<vtask::Scheduler>();

        // Default config (can be overridden at runtime/editor).
        configure(AssetSystemDesc {
            .assetRoot        = ctx().config.asset.assetRoot,
            .importedFolder   = ctx().config.asset.importedFolder,
            .registryFile     = ctx().config.asset.registryFile,
            .vpkFile          = ctx().config.asset.vpkFile,
            .enableImportScan = ctx().config.asset.enableImportScan,
            .asyncLoading     = ctx().config.asset.asyncLoading,
        });

        VULTRA_CORE_TRACE("[AssetSystem] Providing IAssetService");
        ctx().services.provide<IAssetService>(this);

        VULTRA_CORE_INFO("[AssetSystem] Initialized!");

        return true;
    }

    void AssetSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[AssetSystem] Shutting down");
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
        m_SkeletonCache.clear();
        m_AnimationCache.clear();
        m_AudioCache.clear();
        m_TexUUIDToBindlessIndex.clear();
        m_PendingMaterialRefreshes.clear();
        m_CpuLoadScheduler.reset();
        m_RenderDevice       = nullptr;
        m_GpuResourceService = nullptr;
    }

    AssetMemoryStats AssetSystem::memoryStats() const
    {
        AssetMemoryStats stats {};

        auto addMesh = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVMeshBytes(*record.cpu);
            }
        };

        m_MeshCache.forEachRecord(addMesh);

        auto addTexture = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVTextureBytes(*record.cpu);
            }
        };

        m_TextureCache.forEachRecord(addTexture);

        auto addSplat = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVGaussianSplatBytes(*record.cpu);
            }
        };

        m_GaussianSplatCache.forEachRecord(addSplat);

        auto addSkeleton = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVSkeletonBytes(*record.cpu);
            }
        };

        m_SkeletonCache.forEachRecord(addSkeleton);

        auto addAnimation = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVAnimationBytes(*record.cpu);
            }
        };

        m_AnimationCache.forEachRecord(addAnimation);

        auto addAudio = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVAudioBytes(*record.cpu);
            }
        };

        m_AudioCache.forEachRecord(addAudio);

        return stats;
    }

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

    template<typename TCpu, typename TGpu, uint32_t ShardCount, typename UploadFn>
    void AssetSystem::processUpload(AssetCache<TCpu, TGpu, ShardCount>& cache, const CoreUUID& uuid, UploadFn upload)
    {
        auto* rec = cache.findOrCreate(uuid);
        if (!rec)
            return;

        const auto st = rec->state.load(std::memory_order_acquire);
        if (st != AssetState::eUploadQueued && st != AssetState::eCPUReady)
            return;

        rec->state.store(AssetState::eUploadingGPU, std::memory_order_release);

        if (!rec->cpu)
        {
            rec->state.store(AssetState::eFailed, std::memory_order_release);
            return;
        }

        upload(*rec);
    }

    void AssetSystem::update(uint64_t frameIndex)
    {
        m_LastUpdateFrame = std::max(m_LastUpdateFrame, frameIndex);

        // NOTE:
        // GPU upload must happen on the main/render thread. Even in sync bring-up, we keep a queue + update() shape so
        // the system can migrate to async loading later without breaking APIs.
        collectFinishedCpuLoadTasks();
        if (!m_RenderDevice || !m_GpuResourceService)
        {
            std::scoped_lock lock(m_UploadQueueMutex);
            m_UploadQueue.clear();
            m_PendingMaterialRefreshes.clear();
            (void)frameIndex;
            return;
        }

        refreshPendingMaterialParams();

        // Drain upload commands
        std::vector<UploadCmd> cmds;
        {
            std::scoped_lock lock(m_UploadQueueMutex);
            const auto       count = std::min(kMaxUploadCommandsPerFrame, m_UploadQueue.size());
            cmds.insert(cmds.end(), m_UploadQueue.begin(), m_UploadQueue.begin() + static_cast<std::ptrdiff_t>(count));
            m_UploadQueue.erase(m_UploadQueue.begin(), m_UploadQueue.begin() + static_cast<std::ptrdiff_t>(count));
        }

        for (const auto& cmd : cmds)
        {
            switch (cmd.kind)
            {
                case UploadCmd::Kind::eMesh:
                    processUpload(m_MeshCache, cmd.uuid, [this, &cmd](auto& rec) {
                        auto&          pool           = m_GpuResourceService->pool();
                        const uint32_t materialOffset = static_cast<uint32_t>(pool.materials.size());
                        const auto     meshEntry      = m_Registry.lookup(cmd.uuid.native());
                        for (uint32_t materialSlot = 0; materialSlot < static_cast<uint32_t>(rec.cpu->materials.size());
                             ++materialSlot)
                        {
                            const auto& mat           = rec.cpu->materials[materialSlot];
                            uint32_t    materialIndex = std::numeric_limits<uint32_t>::max();
                            if (!meshEntry.importedPath.empty())
                            {
                                const auto relativeMaterialPath =
                                    importedMaterialAssetRelativePath(meshEntry.importedPath, materialSlot, mat.name);
                                const auto physicalMaterialPath =
                                    (std::filesystem::path(m_Desc.assetRoot) / relativeMaterialPath).lexically_normal();
                                std::error_code ec;
                                if (std::filesystem::is_regular_file(physicalMaterialPath, ec))
                                {
                                    materialIndex = createAndAppendGpuMaterialFromAsset(
                                        m_Desc.scheme + "://" + relativeMaterialPath.generic_string(), mat);
                                }
                            }
                            if (materialIndex == std::numeric_limits<uint32_t>::max())
                                createAndAppendGpuMaterial(mat);
                        }

                        const uint32_t meshIndex = uploadMesh(*rec.cpu, materialOffset);

                        rec.gpuIndex.store(meshIndex, std::memory_order_release);
                        rec.state.store(AssetState::eReady, std::memory_order_release);

                        // Release CPU copy if not requested - but keep it for skinned meshes so the
                        // animation system can resolve the mesh's bundled skeleton (resolveSkeletonFor
                        // reads cpu->skeleton) in packaged builds, which otherwise drop CPU mesh data
                        // after GPU upload (keepCpuCopy defaults to false outside the editor).
                        if (!m_Desc.keepCpuCopy && !(rec.cpu && rec.cpu->hasSkin))
                            rec.cpu.reset();
                    });
                    break;

                case UploadCmd::Kind::eTexture:
                    processUpload(m_TextureCache, cmd.uuid, [this](auto& rec) {
                        const uint32_t texIndex = uploadTexture(*rec.cpu);

                        rec.gpuIndex.store(texIndex, std::memory_order_release);
                        rec.state.store(AssetState::eReady, std::memory_order_release);

                        if (!m_Desc.keepCpuCopy)
                            rec.cpu.reset();
                    });
                    break;

                case UploadCmd::Kind::eGaussianSplat:
                    processUpload(m_GaussianSplatCache, cmd.uuid, [this](auto& rec) {
                        const uint32_t splatIndex = uploadGaussianSplat(*rec.cpu);
                        rec.gpuIndex.store(splatIndex, std::memory_order_release);
                        rec.state.store(AssetState::eReady, std::memory_order_release);

                        if (!m_Desc.keepCpuCopy)
                            rec.cpu.reset();
                    });
                    break;
            }
        }

        auto& pool = m_GpuResourceService->pool();
        pool.processDeferredFrees(frameIndex);
        if (pool.materialTableDirty)
        {
            pool.uploadMaterialTable(*m_RenderDevice);
        }

        releaseZeroRefCpuAssets(frameIndex);
    }

    void AssetSystem::releaseZeroRefCpuAssets(const uint64_t frameIndex)
    {
        if (!m_Desc.releaseZeroRefCpuAssets)
            return;

        const uint64_t idleFrames = std::max<uint64_t>(1, m_Desc.zeroRefCpuAssetIdleFrames);
        releaseZeroRefCpuAssetCache(m_SkeletonCache, frameIndex, idleFrames);
        releaseZeroRefCpuAssetCache(m_AnimationCache, frameIndex, idleFrames);
        releaseZeroRefCpuAssetCache(m_AudioCache, frameIndex, idleFrames);
    }

    void AssetSystem::enqueueUploadOnce(UploadCmd::Kind kind, const CoreUUID& uuid, std::atomic_bool& queuedFlag)
    {
        bool expected = false;
        if (!queuedFlag.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        {
            std::scoped_lock lock(m_UploadQueueMutex);
            m_UploadQueue.push_back(UploadCmd {kind, uuid});
        }
    }

    void AssetSystem::collectFinishedCpuLoadTasks()
    {
        std::scoped_lock lock(m_CpuLoadTasksMutex);
        for (auto it = m_CpuLoadTasks.begin(); it != m_CpuLoadTasks.end();)
        {
            CpuLoadTask& cpuTask = **it;
            if (!cpuTask.done.load(std::memory_order_acquire))
            {
                ++it;
                continue;
            }

            if (cpuTask.task && m_CpuLoadScheduler)
                m_CpuLoadScheduler->wait(*cpuTask.task);
            it = m_CpuLoadTasks.erase(it);
        }
    }

    void AssetSystem::waitForCpuLoadTasks()
    {
        std::scoped_lock lock(m_CpuLoadTasksMutex);
        if (m_CpuLoadScheduler)
        {
            for (auto& cpuTask : m_CpuLoadTasks)
            {
                if (cpuTask && cpuTask->task)
                    m_CpuLoadScheduler->wait(*cpuTask->task);
            }
        }
        m_CpuLoadTasks.clear();
    }

    template<typename TCpu, typename TGpu, typename ReadFn, typename ParseFn>
    void AssetSystem::startAssetCpuLoadAsync(AssetRecord<TCpu, TGpu>& rec,
                                             const CoreUUID&          uuid,
                                             UploadCmd::Kind          kind,
                                             const char*              logName,
                                             ReadFn                   readFn,
                                             ParseFn                  parseFn)
    {
        std::string uri;
        if (!resolveUUIDToUri(uuid, uri))
        {
            VULTRA_CLIENT_ERROR("{}: cannot resolve uuid {}", logName, uuid.toString());
            rec.state.store(AssetState::eFailed, std::memory_order_release);
            return;
        }

        auto  cpuLoadTask = std::make_unique<CpuLoadTask>();
        auto* taskState   = cpuLoadTask.get();
        cpuLoadTask->task = std::make_unique<vtask::TaskSet>(
            1, 1, [this, &rec, uuid, uri, kind, logName, readFn, parseFn, taskState](vtask::Range) {
                for (uint32_t attempt = 1; attempt <= kAssetLoadMaxAttempts; ++attempt)
                {
                    auto br = readFn(uri);
                    if (!br)
                    {
                        VULTRA_CLIENT_ERROR(
                            "{}: failed to read {} (attempt {}/{})", logName, uri, attempt, kAssetLoadMaxAttempts);
                        continue;
                    }

                    auto cpu = parseFn(uri, br.value());
                    if (!cpu)
                    {
                        VULTRA_CLIENT_ERROR(
                            "{}: parse failed: {} (attempt {}/{})", logName, uri, attempt, kAssetLoadMaxAttempts);
                        continue;
                    }

                    rec.cpu = std::move(cpu);
                    rec.state.store(AssetState::eCPUReady, std::memory_order_release);
                    rec.state.store(AssetState::eUploadQueued, std::memory_order_release);
                    enqueueUploadOnce(kind, uuid, rec.uploadQueued);
                    taskState->done.store(true, std::memory_order_release);
                    return;
                }

                rec.state.store(AssetState::eFailed, std::memory_order_release);
                taskState->done.store(true, std::memory_order_release);
            });

        if (!m_CpuLoadScheduler)
            m_CpuLoadScheduler = std::make_unique<vtask::Scheduler>();
        m_CpuLoadScheduler->run(*cpuLoadTask->task);

        std::scoped_lock lock(m_CpuLoadTasksMutex);
        m_CpuLoadTasks.push_back(std::move(cpuLoadTask));
    }

    void AssetSystem::startMeshCpuLoadAsync(AssetRecord<vasset::VMesh, resource::GpuMesh>& rec, const CoreUUID& uuid)
    {
        startAssetCpuLoadAsync(
            rec,
            uuid,
            UploadCmd::Kind::eMesh,
            "loadMeshAsync",
            [this](std::string_view uri) { return readAssetBytes(uri); },
            [](std::string_view, std::vector<std::byte>& bytes) -> std::unique_ptr<vasset::VMesh> {
                auto cpu = std::make_unique<vasset::VMesh>();
                if (!vasset::loadMeshFromMemory(bytes, *cpu))
                    return nullptr;
                return cpu;
            });
    }

    void AssetSystem::startTextureCpuLoadAsync(AssetRecord<vasset::VTexture, resource::GpuTexture>& rec,
                                               const CoreUUID&                                      uuid)
    {
        startAssetCpuLoadAsync(
            rec,
            uuid,
            UploadCmd::Kind::eTexture,
            "loadTextureAsync",
            [this](std::string_view uri) { return readAssetBytes(uri); },
            [](std::string_view uri, std::vector<std::byte>& bytes) -> std::unique_ptr<vasset::VTexture> {
                auto cpu =
                    isBuiltinTextureUri(uri) ? makeTextureFromBytes(uri, bytes) : std::make_unique<vasset::VTexture>();
                if (!cpu || (!isBuiltinTextureUri(uri) && !vasset::loadTextureFromMemory(bytes, *cpu)))
                    return nullptr;
                return cpu;
            });
    }

    void
    AssetSystem::startGaussianSplatCpuLoadAsync(AssetRecord<vasset::VGaussianSplat, resource::GpuGaussianSplat>& rec,
                                                const CoreUUID&                                                  uuid)
    {
        startAssetCpuLoadAsync(
            rec,
            uuid,
            UploadCmd::Kind::eGaussianSplat,
            "loadGaussianSplatAsync",
            [this](std::string_view uri) -> vbase::Result<std::vector<std::byte>, std::string> {
                auto br = m_VFS.readAll(uri);
                if (!br)
                    return vbase::Result<std::vector<std::byte>, std::string>::err("failed to read " +
                                                                                   std::string(uri));
                return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(br.value()));
            },
            [](std::string_view, std::vector<std::byte>& bytes) -> std::unique_ptr<vasset::VGaussianSplat> {
                auto cpu = std::make_unique<vasset::VGaussianSplat>();
                if (!vasset::loadGaussianSplatFromMemory(bytes, *cpu))
                    return nullptr;
                return cpu;
            });
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

    vbase::Result<std::vector<std::byte>, std::string> AssetSystem::readAssetBytes(std::string_view uri)
    {
        if (isBuiltinTextureUri(uri))
        {
            // Packaged runtime has no builtin/ folder on disk: fall back to the mounted builtin::
            // pack (logical path = uri without the "builtin://" scheme), then to the legacy
            // embedded resource (editor .rc / linker symbols) for backward compatibility.
            const auto builtinFallback = [&]() -> vbase::Result<std::vector<std::byte>, std::string> {
                std::string_view           logical = uri;
                constexpr std::string_view kScheme = "builtin://";
                if (logical.starts_with(kScheme))
                    logical.remove_prefix(kScheme.size());
                std::vector<std::byte> packed;
                if (builtin::read(logical, packed) && !packed.empty())
                    return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(packed));
                return readResourceBuiltinTextureBytes(uri);
            };

            const auto    path = builtinTexturePathForUri(uri);
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                return builtinFallback();

            const auto             size = static_cast<std::streamsize>(file.tellg());
            std::vector<std::byte> bytes(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
            file.seekg(0);
            if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size))
                return builtinFallback();
            return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(bytes));
        }

        if (isBuiltinFontUri(uri))
        {
            // builtin/fonts on disk (editor), then the mounted builtin:: pack (packaged runtime).
            const auto    path = builtinFontPathForUri(uri);
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (file)
            {
                const auto             size = static_cast<std::streamsize>(file.tellg());
                std::vector<std::byte> bytes(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
                file.seekg(0);
                if (bytes.empty() || file.read(reinterpret_cast<char*>(bytes.data()), size))
                    return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(bytes));
            }

            std::string_view           logical = uri;
            constexpr std::string_view kScheme = "builtin://";
            if (logical.starts_with(kScheme))
                logical.remove_prefix(kScheme.size());
            std::vector<std::byte> packed;
            if (builtin::read(logical, packed) && !packed.empty())
                return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(packed));
            return vbase::Result<std::vector<std::byte>, std::string>::err("failed to read builtin font " +
                                                                           std::string(uri));
        }

        auto br = m_VFS.readAll(uri);
        if (!br)
            return vbase::Result<std::vector<std::byte>, std::string>::err("failed to read " + std::string(uri));
        return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(br.value()));
    }

    uint32_t AssetSystem::resolveBindlessTextureIndex(const CoreUUID& texUUID)
    {
        if (!texUUID.valid())
            return 0;

        auto it = m_TexUUIDToBindlessIndex.find(texUUID);
        if (it != m_TexUUIDToBindlessIndex.end())
            return it->second;

        // Load sync and register.
        auto  h   = loadTextureSync(texUUID);
        auto* rec = h.getRecord();
        if (!rec)
            return 0;

        const uint32_t idx = h.gpuIndex();
        if (idx == std::numeric_limits<uint32_t>::max())
            return 0;
        return idx;
    }

    uint32_t AssetSystem::resolveBindlessTextureIndexAsync(const CoreUUID& texUUID)
    {
        if (!texUUID.valid())
            return 0;

        auto it = m_TexUUIDToBindlessIndex.find(texUUID);
        if (it != m_TexUUIDToBindlessIndex.end())
            return it->second;

        auto h = loadTextureAsync(texUUID);
        if (!h.ready())
            return 0;

        const uint32_t idx = h.gpuIndex();
        if (idx == std::numeric_limits<uint32_t>::max())
            return 0;
        return idx;
    }

    bool AssetSystem::meshPreviewReady(const CoreUUID& meshUUID)
    {
        if (!meshUUID.valid())
            return false;

        auto handle = loadMeshAsync(meshUUID);
        if (!handle.ready())
            return false;

        if (const auto* cpuMesh = handle.cpu())
        {
            for (const auto& material : cpuMesh->materials)
            {
                if (!materialTextureDependenciesReady(material))
                    return false;
            }
        }

        return !materialRefreshPending();
    }

    bool AssetSystem::materialRefreshPending() const { return !m_PendingMaterialRefreshes.empty(); }

    template<typename TCpu, typename TGpu, uint32_t ShardCount, typename StartFn>
    AssetHandle<TCpu, TGpu> AssetSystem::loadGpuAssetAsync(const CoreUUID&                     uuid,
                                                           AssetCache<TCpu, TGpu, ShardCount>& cache,
                                                           UploadCmd::Kind                     kind,
                                                           StartFn                             startCpuLoad)
    {
        auto* rec = cache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        auto st = rec->state.load(std::memory_order_acquire);
        if (st == AssetState::eUnloaded)
        {
            AssetState expected = AssetState::eUnloaded;
            if (rec->state.compare_exchange_strong(
                    expected, AssetState::eLoadingCPU, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                startCpuLoad(*rec, uuid);
            }
        }
        else if (st == AssetState::eCPUReady)
        {
            rec->state.store(AssetState::eUploadQueued, std::memory_order_release);
            enqueueUploadOnce(kind, uuid, rec->uploadQueued);
        }

        return AssetHandle<TCpu, TGpu>(rec);
    }

    AssetHandle<vasset::VTexture, resource::GpuTexture> AssetSystem::loadTextureAsync(const CoreUUID& uuid)
    {
        if (!m_Desc.asyncLoading)
            return loadTextureSync(uuid);
        return loadGpuAssetAsync(uuid,
                                 m_TextureCache,
                                 UploadCmd::Kind::eTexture,
                                 [this](AssetRecord<vasset::VTexture, resource::GpuTexture>& rec, const CoreUUID& u) {
                                     startTextureCpuLoadAsync(rec, u);
                                 });
    }

    AssetHandle<vasset::VMesh, resource::GpuMesh> AssetSystem::loadMeshAsync(const CoreUUID& uuid)
    {
        if (!m_Desc.asyncLoading)
            return loadMeshSync(uuid);
        return loadGpuAssetAsync(uuid,
                                 m_MeshCache,
                                 UploadCmd::Kind::eMesh,
                                 [this](AssetRecord<vasset::VMesh, resource::GpuMesh>& rec, const CoreUUID& u) {
                                     startMeshCpuLoadAsync(rec, u);
                                 });
    }

    AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
    AssetSystem::loadGaussianSplatAsync(const CoreUUID& uuid)
    {
        if (!m_Desc.asyncLoading)
            return loadGaussianSplatSync(uuid);
        return loadGpuAssetAsync(uuid,
                                 m_GaussianSplatCache,
                                 UploadCmd::Kind::eGaussianSplat,
                                 [this](AssetRecord<vasset::VGaussianSplat, resource::GpuGaussianSplat>& rec,
                                        const CoreUUID& u) { startGaussianSplatCpuLoadAsync(rec, u); });
    }

    AssetHandle<vasset::VSkeleton, resource::CpuAsset> AssetSystem::loadSkeletonAsync(const CoreUUID& uuid)
    {
        return loadSkeletonSync(uuid);
    }

    AssetHandle<vasset::VAnimation, resource::CpuAsset> AssetSystem::loadAnimationAsync(const CoreUUID& uuid)
    {
        return loadAnimationSync(uuid);
    }

    AssetHandle<vasset::VSkeleton, resource::CpuAsset> AssetSystem::loadSkeletonSync(const CoreUUID& uuid)
    {
        auto* rec = m_SkeletonCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady)
            return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::string uri;
            if (!resolveUUIDToUri(uuid, uri))
            {
                VULTRA_CLIENT_ERROR("loadSkeletonSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);
            }

            auto br = m_VFS.readAll(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("loadSkeletonSync: failed to read {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);
            }

            auto cpu = std::make_unique<vasset::VSkeleton>();
            auto r   = vasset::loadSkeletonFromMemory(br.value(), *cpu);
            if (!r)
            {
                VULTRA_CLIENT_ERROR("loadSkeletonSync: vasset::loadSkeletonFromMemory failed: {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);
            }

            rec->cpu = std::move(cpu);
            rec->gpuIndex.store(0u, std::memory_order_release);
            rec->state.store(AssetState::eReady, std::memory_order_release);
        }

        return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);
    }

    AssetHandle<vasset::VAnimation, resource::CpuAsset> AssetSystem::loadAnimationSync(const CoreUUID& uuid)
    {
        auto* rec = m_AnimationCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady)
            return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::string uri;
            if (!resolveUUIDToUri(uuid, uri))
            {
                VULTRA_CLIENT_ERROR("loadAnimationSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);
            }

            auto br = m_VFS.readAll(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("loadAnimationSync: failed to read {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);
            }

            auto cpu = std::make_unique<vasset::VAnimation>();
            auto r   = vasset::loadAnimationFromMemory(br.value(), *cpu);
            if (!r)
            {
                VULTRA_CLIENT_ERROR("loadAnimationSync: vasset::loadAnimationFromMemory failed: {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);
            }

            rec->cpu = std::move(cpu);
            rec->gpuIndex.store(0u, std::memory_order_release);
            rec->state.store(AssetState::eReady, std::memory_order_release);
        }

        return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);
    }

    AssetHandle<vasset::VAudio, resource::CpuAsset> AssetSystem::loadAudioSync(const CoreUUID& uuid)
    {
        auto* rec = m_AudioCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady)
            return AssetHandle<vasset::VAudio, resource::CpuAsset>(rec);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::string uri;
            if (!resolveUUIDToUri(uuid, uri))
            {
                VULTRA_CLIENT_ERROR("loadAudioSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VAudio, resource::CpuAsset>(rec);
            }

            auto br = m_VFS.readAll(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("loadAudioSync: failed to read {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VAudio, resource::CpuAsset>(rec);
            }

            auto cpu = std::make_unique<vasset::VAudio>();
            auto r   = vasset::loadAudioFromMemory(br.value(), *cpu);
            if (!r)
            {
                VULTRA_CLIENT_ERROR("loadAudioSync: vasset::loadAudioFromMemory failed: {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VAudio, resource::CpuAsset>(rec);
            }

            rec->cpu = std::move(cpu);
            rec->gpuIndex.store(0u, std::memory_order_release);
            rec->state.store(AssetState::eReady, std::memory_order_release);
        }

        return AssetHandle<vasset::VAudio, resource::CpuAsset>(rec);
    }

    AssetHandle<vasset::VAudio, resource::CpuAsset> AssetSystem::loadAudioAsync(const CoreUUID& uuid)
    {
        return loadAudioSync(uuid);
    }

    template<typename TCpu, typename TGpu, uint32_t ShardCount, typename ReadFn, typename ParseFn>
    AssetHandle<TCpu, TGpu> AssetSystem::loadGpuAssetSync(const CoreUUID&                     uuid,
                                                          AssetCache<TCpu, TGpu, ShardCount>& cache,
                                                          UploadCmd::Kind                     kind,
                                                          const char*                         logName,
                                                          ReadFn                              readFn,
                                                          ParseFn                             parseFn)
    {
        auto* rec = cache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        // Already resident on GPU
        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady &&
            rec->gpuIndex.load(std::memory_order_acquire) != std::numeric_limits<uint32_t>::max())
        {
            return AssetHandle<TCpu, TGpu>(rec);
        }

        if (rec->state.load(std::memory_order_acquire) == AssetState::eLoadingCPU)
            waitForCpuLoadTasks();

        // CPU stage (sync baseline)
        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::string uri;
            if (!resolveUUIDToUri(uuid, uri))
            {
                VULTRA_CLIENT_ERROR("{}: cannot resolve uuid {}", logName, uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<TCpu, TGpu>(rec);
            }

            auto br = readFn(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("{}: failed to read {}", logName, uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<TCpu, TGpu>(rec);
            }

            auto cpu = parseFn(uri, br.value());
            if (!cpu)
            {
                VULTRA_CLIENT_ERROR("{}: parse failed: {}", logName, uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<TCpu, TGpu>(rec);
            }

            rec->cpu = std::move(cpu);
            rec->state.store(AssetState::eCPUReady, std::memory_order_release);
        }

        // Enqueue GPU upload
        if (rec->state.load(std::memory_order_acquire) == AssetState::eCPUReady)
        {
            bool expected = false;
            if (rec->uploadQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                rec->state.store(AssetState::eUploadQueued, std::memory_order_release);

                std::scoped_lock lock(m_UploadQueueMutex);
                m_UploadQueue.push_back(UploadCmd {kind, uuid});
            }
        }

        // Sync baseline: execute uploads immediately. In async mode, the engine main loop calls update()
        // once per frame.
        update(/*frameIndex*/ 0);

        return AssetHandle<TCpu, TGpu>(rec);
    }

    AssetHandle<vasset::VTexture, resource::GpuTexture> AssetSystem::loadTextureSync(const CoreUUID& uuid)
    {
        return loadGpuAssetSync(
            uuid,
            m_TextureCache,
            UploadCmd::Kind::eTexture,
            "loadTextureSync",
            [this](std::string_view u) { return readAssetBytes(u); },
            [](std::string_view u, std::vector<std::byte>& bytes) -> std::unique_ptr<vasset::VTexture> {
                auto cpu =
                    isBuiltinTextureUri(u) ? makeTextureFromBytes(u, bytes) : std::make_unique<vasset::VTexture>();
                if (!cpu || (!isBuiltinTextureUri(u) && !vasset::loadTextureFromMemory(bytes, *cpu)))
                    return nullptr;
                return cpu;
            });
    }

    AssetHandle<vasset::VTexture, resource::GpuTexture> AssetSystem::loadTextureSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadTextureSync(uuid);
    }

    AssetHandle<vasset::VTexture, resource::GpuTexture> AssetSystem::loadTextureAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadTextureAsync(uuid);
    }

    AssetHandle<vasset::VSkeleton, resource::CpuAsset> AssetSystem::loadSkeletonSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadSkeletonSync(uuid);
    }

    AssetHandle<vasset::VSkeleton, resource::CpuAsset> AssetSystem::loadSkeletonAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadSkeletonAsync(uuid);
    }

    AssetHandle<vasset::VAnimation, resource::CpuAsset> AssetSystem::loadAnimationSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadAnimationSync(uuid);
    }

    AssetHandle<vasset::VAnimation, resource::CpuAsset> AssetSystem::loadAnimationAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadAnimationAsync(uuid);
    }

    AssetHandle<vasset::VAudio, resource::CpuAsset> AssetSystem::loadAudioSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadAudioSync(uuid);
    }

    AssetHandle<vasset::VAudio, resource::CpuAsset> AssetSystem::loadAudioAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadAudioAsync(uuid);
    }

    AssetHandle<vasset::VMesh, resource::GpuMesh> AssetSystem::loadMeshSync(const CoreUUID& uuid)
    {
        return loadGpuAssetSync(
            uuid,
            m_MeshCache,
            UploadCmd::Kind::eMesh,
            "loadMeshSync",
            [this](std::string_view u) -> vbase::Result<std::vector<std::byte>, std::string> {
                auto br = m_VFS.readAll(u);
                if (!br)
                    return vbase::Result<std::vector<std::byte>, std::string>::err("failed to read " + std::string(u));
                return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(br.value()));
            },
            [](std::string_view, std::vector<std::byte>& bytes) -> std::unique_ptr<vasset::VMesh> {
                auto cpu = std::make_unique<vasset::VMesh>();
                if (!vasset::loadMeshFromMemory(bytes, *cpu))
                    return nullptr;
                return cpu;
            });
    }

    AssetHandle<vasset::VMesh, resource::GpuMesh> AssetSystem::loadMeshSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadMeshSync(uuid);
    }

    AssetHandle<vasset::VMesh, resource::GpuMesh> AssetSystem::loadMeshAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadMeshAsync(uuid);
    }

    AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
    AssetSystem::loadGaussianSplatSync(const CoreUUID& uuid)
    {
        return loadGpuAssetSync(
            uuid,
            m_GaussianSplatCache,
            UploadCmd::Kind::eGaussianSplat,
            "loadGaussianSplatSync",
            [this](std::string_view u) -> vbase::Result<std::vector<std::byte>, std::string> {
                auto br = m_VFS.readAll(u);
                if (!br)
                    return vbase::Result<std::vector<std::byte>, std::string>::err("failed to read " + std::string(u));
                return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(br.value()));
            },
            [](std::string_view, std::vector<std::byte>& bytes) -> std::unique_ptr<vasset::VGaussianSplat> {
                auto cpu = std::make_unique<vasset::VGaussianSplat>();
                if (!vasset::loadGaussianSplatFromMemory(bytes, *cpu))
                    return nullptr;
                return cpu;
            });
    }

    AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
    AssetSystem::loadGaussianSplatSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadGaussianSplatSync(uuid);
    }

    AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
    AssetSystem::loadGaussianSplatAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadGaussianSplatAsync(uuid);
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

    vbase::Result<std::string, std::string> AssetSystem::loadTextAssetSync(std::string_view uri)
    {
        {
            std::scoped_lock lock(m_TextOverrideMutex);
            if (auto it = m_TextAssetOverrides.find(std::string(uri)); it != m_TextAssetOverrides.end())
                return vbase::Result<std::string, std::string>::ok(it->second);
        }

        if (uri.starts_with("builtin://"))
        {
            if (const auto text = builtinRenderGraphText(uri); !text.empty())
                return vbase::Result<std::string, std::string>::ok(std::string(text));
            if (isBuiltinMaterialUri(uri))
            {
                if (auto text = readBuiltinTextFile(builtinMaterialPathForUri(uri)))
                    return text;
            }
        }

        if (!ctx().config.asset.loadFromVPK)
        {
            const auto      sourcePath = std::filesystem::path(resolveUri(uri)).lexically_normal();
            std::error_code ec;
            if (shouldReadPhysicalTextSourceDirectly(sourcePath) && std::filesystem::is_regular_file(sourcePath, ec))
            {
                std::ifstream file(sourcePath, std::ios::binary | std::ios::ate);
                if (file)
                {
                    const auto  size = static_cast<std::streamsize>(file.tellg());
                    std::string text(static_cast<size_t>(std::max<std::streamsize>(size, 0)), '\0');
                    file.seekg(0);
                    if (text.empty() || file.read(text.data(), size))
                        return vbase::Result<std::string, std::string>::ok(std::move(text));
                }
            }
        }

        auto bytesResult = m_VFS.readAll(uri);
        if (!bytesResult)
            return vbase::Result<std::string, std::string>::err("Failed to read text asset: " + std::string(uri));

        const auto& bytes = bytesResult.value();
        return vbase::Result<std::string, std::string>::ok(
            std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    void AssetSystem::setTextAssetOverride(std::string_view uri, std::string text)
    {
        std::scoped_lock lock(m_TextOverrideMutex);
        m_TextAssetOverrides[std::string(uri)] = std::move(text);
        if (m_GpuResourceService)
            m_GpuResourceService->markContentDirty();
    }

    void AssetSystem::clearTextAssetOverride(std::string_view uri)
    {
        std::scoped_lock lock(m_TextOverrideMutex);
        m_TextAssetOverrides.erase(std::string(uri));
        if (m_GpuResourceService)
            m_GpuResourceService->markContentDirty();
    }

    vbase::Result<std::vector<uint8_t>, std::string> AssetSystem::loadBinaryAssetSync(std::string_view uri)
    {
        // Route through readAssetBytes so builtin:// URIs (textures, fonts) resolve via the
        // mounted builtin pack, not just the project VFS.
        auto bytesResult = readAssetBytes(uri);
        if (!bytesResult)
            return vbase::Result<std::vector<uint8_t>, std::string>::err("Failed to read binary asset: " +
                                                                         std::string(uri));

        const auto&          bytes = bytesResult.value();
        std::vector<uint8_t> out;
        out.reserve(bytes.size());
        for (const auto byte : bytes)
            out.push_back(static_cast<uint8_t>(byte));

        return vbase::Result<std::vector<uint8_t>, std::string>::ok(std::move(out));
    }
} // namespace vultra
