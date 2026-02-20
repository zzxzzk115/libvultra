#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/asset_service.hpp"

#include <vasset/uuid_resolver.hpp>
#include <vasset/vasset_registry.hpp>

#include <vfilesystem/vfs/virtual_filesystem.hpp>

#include <unordered_map>

namespace vultra
{
    class AssetSystem final : public EngineSubsystem, public IAssetService
    {
    public:
        ENGINE_SUBSYSTEM(AssetSystem)

        bool onInit() override;
        void onShutdown() override;

        void setResolver(vasset::VUUIDResolver resolver);

        Ref<gfx::Mesh> loadMesh(std::string_view sourceUri) override;
        Ref<gfx::Mesh> loadMesh(vbase::UUID uuid) override;

        Ref<rhi::Texture> loadTexture(std::string_view sourceUri) override;
        Ref<rhi::Texture> loadTexture(vbase::UUID uuid) override;

    private:
        vbase::UUID resolveUUID(std::string_view sourceUri) const;

        Ref<gfx::Mesh>    loadMeshInternal(vbase::UUID uuid);
        Ref<rhi::Texture> loadTextureInternal(vbase::UUID uuid);

    private:
        vasset::VUUIDResolver  m_Resolver;
        vasset::VAssetRegistry m_Registry;

        std::unordered_map<vbase::UUID, Ref<gfx::Mesh>>    m_MeshCache;
        std::unordered_map<vbase::UUID, Ref<rhi::Texture>> m_TextureCache;

        vfilesystem::VirtualFileSystem m_VFS;
    };
} // namespace vultra