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

    // One record of the primitive transform buffer, scene.instances. Keep in sync with
    // builtin/shaders/resources/primitive_transform.slangh.
    struct PrimitiveTransform
    {
        glm::mat4 model;
        glm::mat4 normal;
        glm::vec4 properties;    // Tangent handedness, conservative sphere scale.
        glm::mat4 previousModel; // The model where the previous frame drew the primitive.
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
        // The caller writes only after the previous frame completes. Until the first beginFrame(), a primitive's
        // previous model follows its model, so transforms set while loading have no motion.
        [[nodiscard]] std::expected<void, Error>
                               setPrimitiveTransforms(uint32_t first, uint32_t count, const glm::mat4& transform);
        bool                   primitiveMirrored(uint32_t index) const;
        const glm::mat4&       primitiveTransform(uint32_t index) const;
        const PrimitiveBounds& primitiveBounds(uint32_t index) const;
        uint64_t               transformRevision() const;
        // Starts a frame for motion vectors: the models of the last frame become the previous models, and what
        // this frame sets moves primitives away from them. Call once per frame, after the previous frame
        // completed and before this frame's transforms are set.
        [[nodiscard]] std::expected<void, Error> beginFrame();
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
        std::vector<glm::mat4>       m_PreviousTransforms;
        bool                         m_FrameStarted = false;
        std::vector<PrimitiveBounds> m_LocalBounds;
        std::vector<PrimitiveBounds> m_WorldBounds;
        uint64_t                     m_TransformRevision = 0;
    };
} // namespace vultra
