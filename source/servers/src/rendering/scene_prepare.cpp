#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>
#include <vultra/servers/rendering/scene.hpp>

namespace vultra
{
    GpuScene::GpuScene(Device& device, const SceneData& scene, bool meshShading, uint32_t workers) :
        GpuScene(device, scene, prepareTextures(scene, {.compression = TextureCompression::eNone}), nullptr)
    {
        if (meshShading)
        {
            meshlets = std::make_unique<GpuMeshlets>(device, *vertices, scene, workers);
        }
    }

    GpuScene::GpuScene(Device& device, const ImportedAsset& asset, bool meshShading, uint32_t workers) :
        GpuScene(device, asset.scene, asset.textures, nullptr)
    {
        if (meshShading)
        {
            meshlets = std::make_unique<GpuMeshlets>(device, *vertices, asset.scene, workers);
        }
    }

    GpuMeshlets::GpuMeshlets(Device& device, Buffer& vertices, const SceneData& scene, uint32_t workers) :
        GpuMeshlets(device, vertices, buildMeshlets(scene, workers))
    {
    }

    GpuSceneHandle RenderingServer::uploadScene(const SceneData& scene, bool meshShading, uint32_t workers)
    {
        return insert(std::make_unique<GpuScene>(m_Device, scene, meshShading, workers));
    }

    GpuSceneHandle RenderingServer::uploadScene(const ImportedAsset& asset, bool meshShading, uint32_t workers)
    {
        return insert(std::make_unique<GpuScene>(m_Device, asset, meshShading, workers));
    }

} // namespace vultra
