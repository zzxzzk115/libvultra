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
        // The caller writes only after the previous frame completes.
        void             setPrimitiveTransforms(uint32_t first, uint32_t count, const glm::mat4& transform);
        bool             primitiveMirrored(uint32_t index) const;
        const glm::mat4& primitiveTransform(uint32_t index) const;
        uint64_t         transformRevision() const;
        // Seven independently typed material slots; color slots use sRGB views.
        std::unique_ptr<Buffer>                                  vertices;
        std::unique_ptr<Buffer>                                  indices;
        std::unique_ptr<Buffer>                                  transforms;
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
        Device&                m_Device;
        std::vector<uint8_t>   m_Mirrored;
        std::vector<glm::mat4> m_Transforms;
        uint64_t               m_TransformRevision = 0;
    };
} // namespace vultra
