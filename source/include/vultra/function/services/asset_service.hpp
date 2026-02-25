#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/asset/asset_handle.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_scene.hpp"
#include "vultra/function/resource/gpu_texture.hpp"

#include <vasset/uuid_resolver.hpp>
#include <vasset/vasset_registry.hpp>
#include <vasset/vmesh.hpp>
#include <vasset/vtexture.hpp>

#include <vbase/service/service_registry.hpp>

#include <string_view>

namespace vultra
{
    struct AssetSystemDesc;

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

        // Convenience: load by uri/path (must be resolvable by registry/resolver)
        virtual AssetHandle<vasset::VMesh, resource::GpuMesh>       loadMeshSync(std::string_view uri)    = 0;
        virtual AssetHandle<vasset::VTexture, resource::GpuTexture> loadTextureSync(std::string_view uri) = 0;

        // Bindless texture index resolution.
        // Returns 0 for invalid UUID.
        virtual uint32_t resolveBindlessTextureIndex(const CoreUUID& texUUID) = 0;

        // Scene GPU tables.
        virtual resource::GpuScene&       gpuScene()       = 0;
        virtual const resource::GpuScene& gpuScene() const = 0;

        // Optional: access registry/resolver for tooling.
        virtual const vasset::VAssetRegistry& registry() const = 0;
        virtual const vasset::VUUIDResolver&  resolver() const = 0;

        virtual std::string resolveUri(const std::string_view uri) const = 0;

        // Allow overriding config (e.g., editor/runtime).
        virtual void configure(const AssetSystemDesc& desc) = 0;
    };
} // namespace vultra
