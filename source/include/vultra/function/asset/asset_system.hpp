#pragma once

#include "vfilesystem/vfs/virtual_filesystem.hpp"
#include "vultra/core/base/uuid.hpp"
#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/asset/asset_cache.hpp"
#include "vultra/function/asset/asset_handle.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_resource_pool.hpp"
#include "vultra/function/resource/gpu_texture.hpp"
#include "vultra/function/services/asset_service.hpp"

#include <vasset/uuid_resolver.hpp>
#include <vasset/vasset_registry.hpp>
#include <vasset/vmesh.hpp>
#include <vasset/vtexture.hpp>

#include <mutex>
#include <string>
#include <unordered_map>

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
    };

    // Sync-only baseline. Async IO + main-thread upload will be added later without breaking APIs.
    class AssetSystem final : public EngineSubsystem, public IAssetService
    {
    public:
        ENGINE_SUBSYSTEM(AssetSystem)

        bool onInit() override;
        void onShutdown() override;

        void configure(const AssetSystemDesc& desc) override;

        // Main-thread per-frame update: drain upload queue + GC.
        void update(uint64_t frameIndex) override;

        // ----- Sync loading -----
        AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshSync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureSync(const CoreUUID& uuid) override;

        // Convenience: load by uri/path (must be resolvable by registry/resolver)
        AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshSync(std::string_view uri) override;
        AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureSync(std::string_view uri) override;

        const vasset::VAssetRegistry& registry() const override { return m_Registry; }
        const vasset::VUUIDResolver&  resolver() const override { return m_Resolver; }

        std::string resolveUri(const std::string_view uri) const override;

        resource::GpuResourcePool&       gpuResourcePool() override { return m_ResourcePool; }
        const resource::GpuResourcePool& gpuResourcePool() const override { return m_ResourcePool; }

        // Bindless texture index resolution.
        // Returns 0 for invalid UUID.
        uint32_t resolveBindlessTextureIndex(const CoreUUID& texUUID) override;

    private:
        uint32_t uploadTexture(const vasset::VTexture& cpuTex);
        uint32_t uploadMesh(const vasset::VMesh& cpuMesh, uint32_t materialOffset);

        // Creates a GpuMaterial entry and appends into the global material table.
        // Returns index.
        uint32_t createAndAppendGpuMaterial(const vasset::VMaterial& m);

        bool resolveUUIDToUri(const CoreUUID& uuid, std::string& outUri) const;
        bool resolveUriToUUID(std::string_view uri, CoreUUID& outUUID) const;

    private:
        struct UploadCmd
        {
            enum class Kind : uint8_t
            {
                eMesh = 0,
                eTexture,
            };

            Kind     kind {Kind::eMesh};
            CoreUUID uuid;
        };

        // Thread-safe upload command queue (sync bring-up).
        // NOTE: This is intentionally simple today (mutex + vector). It can be replaced with a lock-free MPSC ring
        // buffer later without changing any public APIs.
        std::mutex             m_UploadQueueMutex;
        std::vector<UploadCmd> m_UploadQueue;

    private:
        rhi::RenderDevice* m_RenderDevice {nullptr};
        AssetSystemDesc    m_Desc;

        vasset::VAssetRegistry m_Registry;
        vasset::VUUIDResolver  m_Resolver;

        vfilesystem::VirtualFileSystem m_VFS;

        resource::GpuResourcePool m_ResourcePool;

        // Caches (uuid -> record)
        AssetCache<vasset::VMesh, resource::GpuMesh, 64>       m_MeshCache;
        AssetCache<vasset::VTexture, resource::GpuTexture, 64> m_TextureCache;

        // Texture UUID -> bindless index
        std::unordered_map<CoreUUID, uint32_t> m_TexUUIDToBindlessIndex;
    };
} // namespace vultra
