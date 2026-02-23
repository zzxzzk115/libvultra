#pragma once

#include "vfilesystem/vfs/virtual_filesystem.hpp"
#include "vultra/core/base/uuid.hpp"
#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/asset/asset_cache.hpp"
#include "vultra/function/asset/asset_handle.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_scene.hpp"
#include "vultra/function/resource/gpu_texture.hpp"
#include "vultra/function/services/asset_service.hpp"

#include <vasset/uuid_resolver.hpp>
#include <vasset/vasset_registry.hpp>
#include <vasset/vmesh.hpp>
#include <vasset/vtexture.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    struct AssetSystemDesc
    {
        std::string assetRoot {"resources"};
        std::string importedFolder {"imported"};
        std::string registryFile {"resources/imported/asset_registry.tsv"};
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

        // ----- Sync loading -----
        AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshSync(const CoreUUID& uuid) override;
        AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureSync(const CoreUUID& uuid) override;

        // Convenience: load by uri/path (must be resolvable by registry/resolver)
        AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshSync(std::string_view uri) override;
        AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureSync(std::string_view uri) override;

        const vasset::VAssetRegistry& registry() const override { return m_Registry; }
        const vasset::VUUIDResolver&  resolver() const override { return m_Resolver; }

        resource::GpuScene&       gpuScene() override { return m_Scene; }
        const resource::GpuScene& gpuScene() const override { return m_Scene; }

        // Bindless texture index resolution.
        // Returns 0 for invalid UUID.
        uint32_t resolveBindlessTextureIndex(const CoreUUID& texUUID) override;

    private:
        static std::vector<std::byte> readFileBytes(const std::filesystem::path& path);

        uint32_t uploadTexture(const vasset::VTexture& cpuTex);
        uint32_t uploadMesh(const vasset::VMesh& cpuMesh, uint32_t materialOffset);

        // Creates a GpuMaterial entry and appends into gpuScene.materials.
        // Returns index.
        uint32_t createAndAppendGpuMaterial(const vasset::VMaterial& m);

        bool resolveUUIDToPath(const CoreUUID& uuid, std::filesystem::path& outPath) const;
        bool resolveUriToUUID(std::string_view uri, CoreUUID& outUUID) const;

    private:
        rhi::RenderDevice* m_RenderDevice {nullptr};
        AssetSystemDesc    m_Desc;

        vasset::VAssetRegistry m_Registry;
        vasset::VUUIDResolver  m_Resolver;

        vfilesystem::VirtualFileSystem m_VFS;

        resource::GpuScene m_Scene;

        // Caches (uuid -> record)
        AssetCache<vasset::VMesh, resource::GpuMesh, 64>       m_MeshCache;
        AssetCache<vasset::VTexture, resource::GpuTexture, 64> m_TextureCache;

        // Texture UUID -> bindless index
        std::unordered_map<CoreUUID, uint32_t> m_TexUUIDToBindlessIndex;
    };
} // namespace vultra
