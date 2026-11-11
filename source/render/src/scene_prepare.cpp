#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/render/rendering_server.hpp>
#include <vultra/render/scene.hpp>

#include <utility>

namespace vultra
{
    std::expected<std::unique_ptr<GpuScene>, Error>
    GpuScene::create(Device& device, const SceneData& scene, bool meshShading, uint32_t workers)
    {
        const auto textures = prepareTextures(scene, {.compression = TextureCompression::eNone});
        if (!textures)
        {
            return std::unexpected(textures.error());
        }
        auto result = create(device, scene, *textures, nullptr);
        if (!result || !meshShading)
        {
            return result;
        }
        auto meshlets = GpuMeshlets::create(device, *(*result)->vertices, scene, workers);
        if (!meshlets)
        {
            return std::unexpected(meshlets.error());
        }
        (*result)->meshlets = std::move(*meshlets);
        return result;
    }

    std::expected<std::unique_ptr<GpuScene>, Error>
    GpuScene::create(Device& device, const ImportedAsset& asset, bool meshShading, uint32_t workers)
    {
        auto result = create(device, asset.scene, asset.textures, nullptr);
        if (!result || !meshShading)
        {
            return result;
        }
        auto meshlets = GpuMeshlets::create(device, *(*result)->vertices, asset.scene, workers);
        if (!meshlets)
        {
            return std::unexpected(meshlets.error());
        }
        (*result)->meshlets = std::move(*meshlets);
        return result;
    }

    std::expected<std::unique_ptr<GpuMeshlets>, Error>
    GpuMeshlets::create(Device& device, Buffer& vertices, const SceneData& scene, uint32_t workers)
    {
        const auto data = buildMeshlets(scene, workers);
        if (!data)
        {
            return std::unexpected(data.error());
        }
        return create(device, vertices, *data);
    }

    std::expected<GpuSceneHandle, Error>
    RenderingServer::uploadScene(const SceneData& scene, bool meshShading, uint32_t workers)
    {
        auto created = GpuScene::create(m_Device, scene, meshShading, workers);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        return insert(std::move(*created));
    }

    std::expected<GpuSceneHandle, Error>
    RenderingServer::uploadScene(const ImportedAsset& asset, bool meshShading, uint32_t workers)
    {
        auto created = GpuScene::create(m_Device, asset, meshShading, workers);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        return insert(std::move(*created));
    }
} // namespace vultra
