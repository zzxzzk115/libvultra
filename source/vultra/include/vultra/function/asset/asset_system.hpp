#pragma once

#include "vfilesystem/vfs/virtual_filesystem.hpp"
#include "vultra/core/base/uuid.hpp"
#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/asset/asset_cache.hpp"
#include "vultra/function/asset/asset_handle.hpp"
#include "vultra/function/resource/gpu_gaussian_splat.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_texture.hpp"
#include "vultra/function/resource/cpu_asset.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"

#include <vasset/uuid_resolver.hpp>
#include <vasset/vasset_registry.hpp>
#include <vasset/vgaussiansplat.hpp>
#include <vasset/vanimation.hpp>
#include <vasset/vmesh.hpp>
#include <vasset/vtexture.hpp>

#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    struct AssetSystemDesc
    {
        std::string assetRoot {"resources"};
        std::string importedFolder {"imported"};
        std::string registryFile {"asset_registry.tsv"};
        std::string scheme {"res"};
        std::string vpkFile {"resources.vpk"};

        // Whether to keep decoded CPU assets after GPU upload.
        bool keepCpuCopy {false};

        // Editor boot can import assets before reconfiguring the runtime registry. In that path configure() should
        // only load/mount the already refreshed registry to avoid blocking the render thread.
        bool enableImportScan {true};

        // When disabled, the Async service methods become blocking loads. This keeps small demos/examples fully
        // resident after scene instantiation while preserving async streaming for editor/project runtime paths.
        bool asyncLoading {true};

        // Conservative runtime release for CPU-only cached assets. GPU resources need generation-safe pool reclaim
        // before they can be released automatically without stale material/scene indices.
        bool     releaseZeroRefCpuAssets {true};
        uint64_t zeroRefCpuAssetIdleFrames {600};
    };

    // Sync-only baseline. Async IO + main-thread upload will be added later without breaking APIs.
    class AssetSystem final : public EngineSubsystem, public IAssetService
    {
    public:
        ENGINE_SUBSYSTEM(AssetSystem)

        ~AssetSystem() override;

        bool onInit() override;
        void onShutdown() override;

        void configure(const AssetSystemDesc& desc) override;

        // Main-thread per-frame update: drain upload queue + GC.
        void update(uint64_t frameIndex) override;

        // ----- Sync loading -----
        AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshSync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureSync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
        loadGaussianSplatSync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VSkeleton, resource::CpuAsset>  loadSkeletonSync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VAnimation, resource::CpuAsset> loadAnimationSync(const CoreUUID& uuid) override;

        AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshAsync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureAsync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
        loadGaussianSplatAsync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VSkeleton, resource::CpuAsset>  loadSkeletonAsync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VAnimation, resource::CpuAsset> loadAnimationAsync(const CoreUUID& uuid) override;

        // Convenience: load by uri/path (must be resolvable by registry/resolver)
        AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshSync(std::string_view uri) override;
        AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureSync(std::string_view uri) override;
        AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
        loadGaussianSplatSync(std::string_view uri) override;
        AssetHandle<vasset::VSkeleton, resource::CpuAsset>  loadSkeletonSync(std::string_view uri) override;
        AssetHandle<vasset::VAnimation, resource::CpuAsset> loadAnimationSync(std::string_view uri) override;

        AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshAsync(std::string_view uri) override;
        AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureAsync(std::string_view uri) override;
        AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
        loadGaussianSplatAsync(std::string_view uri) override;
        AssetHandle<vasset::VSkeleton, resource::CpuAsset>  loadSkeletonAsync(std::string_view uri) override;
        AssetHandle<vasset::VAnimation, resource::CpuAsset> loadAnimationAsync(std::string_view uri) override;

        vbase::Result<std::string, std::string> loadTextAssetSync(std::string_view uri) override;
        void setTextAssetOverride(std::string_view uri, std::string text) override;
        void clearTextAssetOverride(std::string_view uri) override;
        vbase::Result<std::vector<uint8_t>, std::string> loadBinaryAssetSync(std::string_view uri) override;

        const vasset::VAssetRegistry& registry() const override { return m_Registry; }
        const vasset::VUUIDResolver&  resolver() const override { return m_Resolver; }

        AssetMemoryStats memoryStats() const override;

        std::string resolveUri(const std::string_view uri) const override;
        bool        reimportAsset(std::string_view uri, bool forceReimport = true) override;
        std::vector<AssetDiagnostic> lastImportDiagnostics() const override { return m_LastImportDiagnostics; }
        bool        reloadRegistry() override;

        // Bindless texture index resolution.
        // Returns 0 for invalid UUID.
        uint32_t resolveBindlessTextureIndex(const CoreUUID& texUUID) override;
        bool     meshPreviewReady(const CoreUUID& meshUUID) override;
        bool     materialRefreshPending() const override;

    private:
        struct UploadCmd
        {
            enum class Kind : uint8_t
            {
                eMesh = 0,
                eTexture,
                eGaussianSplat,
            };

            Kind     kind {Kind::eMesh};
            CoreUUID uuid;
        };

        uint32_t uploadTexture(const vasset::VTexture& cpuTex);
        uint32_t uploadMesh(const vasset::VMesh& cpuMesh, uint32_t materialOffset);
        uint32_t uploadGaussianSplat(const vasset::VGaussianSplat& cpuSplat);

        // Creates a GpuMaterial entry and appends into the global material table.
        // Returns index.
        uint32_t createAndAppendGpuMaterial(const vasset::VMaterial& m);

        bool resolveUUIDToUri(const CoreUUID& uuid, std::string& outUri) const;
        bool resolveUriToUUID(std::string_view uri, CoreUUID& outUUID) const;
        vbase::Result<std::vector<std::byte>, std::string> readTextureAssetBytes(std::string_view uri);
        void enqueueUploadOnce(UploadCmd::Kind kind, const CoreUUID& uuid, std::atomic_bool& queuedFlag);
        void collectFinishedCpuLoadTasks();
        void waitForCpuLoadTasks();
        void startMeshCpuLoadAsync(AssetRecord<vasset::VMesh, resource::GpuMesh>& rec, const CoreUUID& uuid);
        void startTextureCpuLoadAsync(AssetRecord<vasset::VTexture, resource::GpuTexture>& rec, const CoreUUID& uuid);
        void startGaussianSplatCpuLoadAsync(AssetRecord<vasset::VGaussianSplat, resource::GpuGaussianSplat>& rec,
                                            const CoreUUID& uuid);
        uint32_t resolveBindlessTextureIndexAsync(const CoreUUID& texUUID);
        bool     materialTextureDependenciesReady(const vasset::VMaterial& material);
        bool     refreshGpuMaterialParams(uint32_t materialIndex, const vasset::VMaterial& material);
        void     refreshPendingMaterialParams();
        void     releaseZeroRefCpuAssets(uint64_t frameIndex);

    private:
        // Thread-safe upload command queue (sync bring-up).
        // NOTE: This is intentionally simple today (mutex + vector). It can be replaced with a lock-free MPSC ring
        // buffer later without changing any public APIs.
        std::mutex             m_UploadQueueMutex;
        std::vector<UploadCmd> m_UploadQueue;

        struct CpuLoadTask
        {
            std::unique_ptr<vtask::TaskSet> task;
            std::atomic_bool                done {false};
        };
        std::unique_ptr<vtask::Scheduler>         m_CpuLoadScheduler;
        std::mutex                                m_CpuLoadTasksMutex;
        std::vector<std::unique_ptr<CpuLoadTask>> m_CpuLoadTasks;

        struct PendingMaterialRefresh
        {
            uint32_t          materialIndex {0};
            vasset::VMaterial material;
        };
        std::vector<PendingMaterialRefresh> m_PendingMaterialRefreshes;

    private:
        rhi::RenderDevice* m_RenderDevice {nullptr};
        AssetSystemDesc    m_Desc;

        vasset::VAssetRegistry m_Registry;
        vasset::VUUIDResolver  m_Resolver;

        vfilesystem::VirtualFileSystem m_VFS;

        IGpuResourceService* m_GpuResourceService {nullptr};
        uint64_t             m_LastUpdateFrame {0};

        // Caches (uuid -> record)
        AssetCache<vasset::VMesh, resource::GpuMesh, 64>                   m_MeshCache;
        AssetCache<vasset::VTexture, resource::GpuTexture, 64>             m_TextureCache;
        AssetCache<vasset::VGaussianSplat, resource::GpuGaussianSplat, 32> m_GaussianSplatCache;
        AssetCache<vasset::VSkeleton, resource::CpuAsset, 16>              m_SkeletonCache;
        AssetCache<vasset::VAnimation, resource::CpuAsset, 32>             m_AnimationCache;

        // Texture UUID -> bindless index
        std::unordered_map<CoreUUID, uint32_t> m_TexUUIDToBindlessIndex;

        std::mutex                                   m_TextOverrideMutex;
        std::unordered_map<std::string, std::string> m_TextAssetOverrides;
        std::vector<AssetDiagnostic>                 m_LastImportDiagnostics;
    };
} // namespace vultra
