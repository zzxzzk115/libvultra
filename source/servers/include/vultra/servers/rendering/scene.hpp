#pragma once

#include <vultra/assets/scene_data.hpp>
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/servers/rendering/meshlets.hpp>

#include <array>
#include <memory>
#include <vector>

namespace vultra
{
    struct ImportedAsset;
    struct PreparedTextures;

    class GpuScene
    {
    public:
        GpuScene(Device& device, const SceneData& scene, bool meshShading = false, uint32_t workers = 0);
        GpuScene(Device& device, const ImportedAsset& asset, bool meshShading = false, uint32_t workers = 0);
        // Seven independently typed material slots; color slots use sRGB views.
        std::unique_ptr<Buffer>                                  vertices;
        std::unique_ptr<Buffer>                                  indices;
        std::vector<std::unique_ptr<Texture>>                    textures;
        std::vector<std::array<Texture*, kMaterialTextureCount>> materialTextures;
        std::vector<SurfaceMaterial>                             materials;
        std::vector<VriSamplerDesc>                              samplers;
        std::vector<ScenePrimitive>                              primitives;
        glm::vec3                                                center;
        float                                                    radius;
        std::unique_ptr<GpuMeshlets>                             meshlets;

    private:
        GpuScene(Device&                 device,
                 const SceneData&        scene,
                 const PreparedTextures& prepared,
                 bool                    meshShading,
                 uint32_t                workers);
    };
} // namespace vultra
