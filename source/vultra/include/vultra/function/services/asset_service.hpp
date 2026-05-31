#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/asset/asset_handle.hpp"
#include "vultra/function/resource/gpu_gaussian_splat.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_texture.hpp"

#include <vasset/uuid_resolver.hpp>
#include <vasset/vasset_registry.hpp>
#include <vasset/vgaussiansplat.hpp>
#include <vasset/vmesh.hpp>
#include <vasset/vtexture.hpp>

#include <vbase/core/result.hpp>
#include <vbase/service/service_registry.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    struct AssetSystemDesc;

    struct AssetMemoryStats
    {
        uint64_t cpuCacheBytes {0};
    };

    struct AssetDiagnostic
    {
        std::string path;
        size_t      line {0};
        size_t      column {0};
        std::string message;
    };

    // Asset service interface (engine-facing).
    // Pattern: WindowSystem/InputSystem/... -> provide<IService>(this)
    class IAssetService
    {
    public:
        SERVICE_REGISTER(IAssetService)

        virtual ~IAssetService() = default;

        // (Sync baseline) Load assets by UUID.
        virtual AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshSync(const CoreUUID& uuid)    = 0;
        virtual AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureSync(const CoreUUID& uuid) = 0;
        virtual AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
        loadGaussianSplatSync(const CoreUUID& uuid) = 0;

        // Non-blocking runtime requests. CPU loading is scheduled on worker threads; GPU upload is finalized from
        // update() on the main/render thread. Returned handles may be valid but not ready yet.
        virtual AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshAsync(const CoreUUID& uuid)    = 0;
        virtual AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureAsync(const CoreUUID& uuid) = 0;
        virtual AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
        loadGaussianSplatAsync(const CoreUUID& uuid) = 0;

        // Convenience: load by uri/path (must be resolvable by registry/resolver)
        virtual AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshSync(std::string_view uri)    = 0;
        virtual AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureSync(std::string_view uri) = 0;
        virtual AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
        loadGaussianSplatSync(std::string_view uri) = 0;

        virtual AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshAsync(std::string_view uri)    = 0;
        virtual AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureAsync(std::string_view uri) = 0;
        virtual AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
        loadGaussianSplatAsync(std::string_view uri) = 0;

        // Text assets: scene documents, manifests, Lua scripts, etc.
        virtual vbase::Result<std::string, std::string> loadTextAssetSync(std::string_view uri) = 0;

        // Editor-only live overrides. Runtime loaders read these before disk/VFS,
        // allowing tools to preview unsaved text assets without writing them.
        virtual void setTextAssetOverride(std::string_view uri, std::string text) = 0;
        virtual void clearTextAssetOverride(std::string_view uri) = 0;

        // Binary assets: cooked shader libraries and other opaque runtime payloads.
        virtual vbase::Result<std::vector<uint8_t>, std::string> loadBinaryAssetSync(std::string_view uri) = 0;

        // Bindless texture index resolution.
        // Returns 0 for invalid UUID.
        virtual uint32_t resolveBindlessTextureIndex(const CoreUUID& texUUID) = 0;

        // Preview/thumbnail readiness helpers. They request missing texture dependencies asynchronously and return
        // false until meshes are GPU-ready, their material textures are GPU-ready, and pending material refreshes drain.
        [[nodiscard]] virtual bool meshPreviewReady(const CoreUUID& meshUUID) = 0;
        [[nodiscard]] virtual bool materialRefreshPending() const = 0;

        // Optional: access registry/resolver for tooling.
        virtual const vasset::VAssetRegistry& registry() const = 0;
        virtual const vasset::VUUIDResolver&  resolver() const = 0;

        [[nodiscard]] virtual AssetMemoryStats memoryStats() const = 0;

        virtual std::string resolveUri(const std::string_view uri) const = 0;

        // Editor/development import path. Production builds may return false when import support is not linked.
        virtual bool reimportAsset(std::string_view uri, bool forceReimport = true) = 0;
        virtual std::vector<AssetDiagnostic> lastImportDiagnostics() const = 0;

        // Reload the asset registry/resolver without clearing resident runtime assets.
        // Editor background imports/deletes use this so the open scene keeps its GPU resources.
        virtual bool reloadRegistry() = 0;

        // Allow overriding config (e.g., editor/runtime).
        // Per-frame update.
        // - Drain GPU upload queue (main thread).
        // - Perform garbage collection / eviction.
        // Sync bring-up can still enqueue uploads and have update() execute them immediately.
        virtual void update(uint64_t frameIndex) = 0;

        virtual void configure(const AssetSystemDesc& desc) = 0;
    };
} // namespace vultra
