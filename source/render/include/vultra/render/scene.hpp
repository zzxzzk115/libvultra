#pragma once

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/error.hpp>
#include <vultra/gpu/resources.hpp>
#include <vultra/render/meshlets.hpp>

#include <array>
#include <expected>
#include <memory>
#include <vector>

namespace vultra
{
    struct ImportedAsset;
    struct PreparedTextures;

    struct PrimitiveBounds
    {
        glm::vec3 center {0};
        glm::vec3 extent {0};
    };

    class GpuScene
    {
    public:
        // Render paths and servers keep the scene's address, so it lives behind a unique_ptr.
        [[nodiscard]] static std::expected<std::unique_ptr<GpuScene>, Error>
        create(Device&                 device,
               const SceneData&        scene,
               const PreparedTextures& textures,
               const MeshletData*      meshlets = nullptr);
#ifdef VULTRA_WITH_SCENE_PREPARATION
        [[nodiscard]] static std::expected<std::unique_ptr<GpuScene>, Error>
        create(Device& device, const SceneData& scene, bool meshShading = false, uint32_t workers = 0);
        [[nodiscard]] static std::expected<std::unique_ptr<GpuScene>, Error>
        create(Device& device, const ImportedAsset& asset, bool meshShading = false, uint32_t workers = 0);
#endif
        GpuScene(const GpuScene&)            = delete;
        GpuScene& operator=(const GpuScene&) = delete;
        // The caller writes only after the previous frame completes.
        [[nodiscard]] std::expected<void, Error>
                               setPrimitiveTransforms(uint32_t first, uint32_t count, const glm::mat4& transform);
        bool                   primitiveMirrored(uint32_t index) const;
        const glm::mat4&       primitiveTransform(uint32_t index) const;
        const PrimitiveBounds& primitiveBounds(uint32_t index) const;
        uint64_t               transformRevision() const;
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
        GpuScene(Device& device, const SceneData& scene);

        Device&                      m_Device;
        std::vector<uint8_t>         m_Mirrored;
        std::vector<glm::mat4>       m_Transforms;
        std::vector<PrimitiveBounds> m_LocalBounds;
        std::vector<PrimitiveBounds> m_WorldBounds;
        uint64_t                     m_TransformRevision = 0;
    };
} // namespace vultra
