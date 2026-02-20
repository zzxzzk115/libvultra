#include "vultra/function/asset/asset_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/renderer/mesh.hpp"
#include "vultra/function/resource/raw_resource_loader.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#include <vasset/editor_filesystem.hpp>
#include <vasset/vasset_importers.hpp>
#include <vasset/vmesh.hpp>
#include <vasset/vpk.hpp>
#include <vasset/vtexture.hpp>

#include <vfilesystem/backends/physical_filesystem.hpp>
#include <vfilesystem/core/path.hpp>

namespace vultra
{
    bool AssetSystem::onInit()
    {
        ctx().services.provide<IAssetService>(this);

        if (ctx().config.loadFromVPK)
        {
            // For production, mount the VPK file (read-only).
            auto vpkFileSystem = std::make_shared<vasset::VpkFileSystem>("resources.vpk");
            vpkFileSystem->openPackage();
            m_VFS.mount(vpkFileSystem, "res");
        }
        else
        {
            // Try to load existing registry from disk. This will populate the registry with previously imported assets,
            // allowing us to load them without re-importing.
            std::string registryPath = "resources/imported/asset_registry.tsv";
            if (!std::filesystem::exists(registryPath) || !m_Registry.load(registryPath))
            {
                VULTRA_CORE_WARN("Failed to load asset registry from file: {}", registryPath);
                // Proceed with an empty registry, which will cause assets to be re-imported.
                m_Registry.setAssetRootPath("resources");
                m_Registry.setImportedFolderName("imported");
                vasset::VAssetImporter importer {m_Registry};
                importer.importOrReimportAssetFolder("resources");
                m_Registry.save(registryPath);
            }
            else
            {
                VULTRA_CORE_INFO("Loaded asset registry from file: {}", registryPath);
            }

            m_Resolver.loadFromAssetRegistry(m_Registry);

            // For development, mount the editor remap filesystem, which allows transparent access to source assets and
            // imported assets.
            m_VFS.mount(std::make_shared<vasset::EditorRemapFileSystem>(
                            std::make_shared<vfilesystem::PhysicalFileSystem>(vfilesystem::Path {"resources"})),
                        "res");
        }

        m_Resolver.setScheme("res");

        return true;
    }

    void AssetSystem::onShutdown()
    {
        m_MeshCache.clear();
        m_TextureCache.clear();
    }

    void AssetSystem::setResolver(vasset::VUUIDResolver resolver) { m_Resolver = std::move(resolver); }

    vbase::UUID AssetSystem::resolveUUID(std::string_view sourceUri) const
    {
        vbase::UUID uuid {};

        if (!m_Resolver.reverseResolve(sourceUri, uuid))
            return {};

        return uuid;
    }

    std::shared_ptr<gfx::Mesh> AssetSystem::loadMesh(std::string_view sourceUri)
    {
        return loadMeshInternal(resolveUUID(sourceUri));
    }

    std::shared_ptr<gfx::Mesh> AssetSystem::loadMesh(vbase::UUID uuid) { return loadMeshInternal(uuid); }

    std::shared_ptr<gfx::Mesh> AssetSystem::loadMeshInternal(vbase::UUID uuid)
    {
        if (!uuid)
            return {};

        if (auto it = m_MeshCache.find(uuid); it != m_MeshCache.end())
            return it->second;

        std::string uri;

        if (!m_Resolver.resolve(uuid, uri))
            return {};

        auto file = m_VFS.open(uri, vfilesystem::FileMode::eRead);
        if (!file)
        {
            VULTRA_CORE_ERROR("Failed to open asset file: {}", uri);
            return {};
        }

        auto bytes = file.value()->readAllBytes();

        vasset::VMesh cpu {};

        if (!vasset::loadMeshFromMemory(bytes, cpu))
            return {};

        auto& rd = ctx().services.require<IRenderBackendService>().renderDevice();

        auto mesh = gfx::Mesh::create(rd, cpu);

        m_MeshCache[uuid] = mesh;

        VULTRA_CORE_INFO(
            "Loaded mesh asset: {} ({} vertices, {} materials)", uri, cpu.vertexCount, cpu.materials.size());

        return mesh;
    }

    std::shared_ptr<rhi::Texture> AssetSystem::loadTexture(std::string_view sourceUri)
    {
        return loadTextureInternal(resolveUUID(sourceUri));
    }

    std::shared_ptr<rhi::Texture> AssetSystem::loadTexture(vbase::UUID uuid) { return loadTextureInternal(uuid); }

    std::shared_ptr<rhi::Texture> AssetSystem::loadTextureInternal(vbase::UUID uuid)
    {
        if (!uuid)
            return {};

        if (auto it = m_TextureCache.find(uuid); it != m_TextureCache.end())
            return it->second;

        std::string uri;

        if (!m_Resolver.resolve(uuid, uri))
            return {};

        auto file = m_VFS.open(uri, vfilesystem::FileMode::eRead);
        if (!file)
        {
            VULTRA_CORE_ERROR("Failed to open texture file: {}", uri);
            return {};
        }

        auto bytes = file.value()->readAllBytes();

        vasset::VTexture cpu {};

        if (!vasset::loadTextureFromMemory(bytes, cpu))
            return {};

        auto& rd = ctx().services.require<IRenderBackendService>().renderDevice();

        auto tex = resource::loadTextureVTexture(cpu, rd);
        if (!tex)
        {
            VULTRA_CORE_ERROR("Failed to load texture from VTexture data: {}", tex.error());
            return {};
        }

        auto texture         = createRef<rhi::Texture>(std::move(tex.value()));
        m_TextureCache[uuid] = texture;

        VULTRA_CORE_INFO("Loaded texture asset: {} ({}x{})", uri, cpu.width, cpu.height);

        return texture;
    }
} // namespace vultra