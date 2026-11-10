#pragma once

#include <vultra/assets/scene_data.hpp>
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/servers/rendering/meshlets.hpp>

#include <array>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vultra
{
    struct ImportedAsset;
    struct PreparedTextures;
    class RayScene;

    namespace detail
    {
        struct RayGeometry;
    }

    struct PrimitiveBounds
    {
        glm::vec3 center {0};
        glm::vec3 extent {0};
    };

    class GpuScene
    {
    public:
        // Creates an indexed GPU scene without requiring mesh-shader support.
        static std::expected<std::unique_ptr<GpuScene>, std::string> create(Device&                 device,
                                                                            const SceneData&        scene,
                                                                            const PreparedTextures& textures,
                                                                            const MeshletData*      meshlets = nullptr);
        GpuScene(Device&                 device,
                 const SceneData&        scene,
                 const PreparedTextures& textures,
                 const MeshletData*      meshlets = nullptr);
        ~GpuScene();
#ifdef VULTRA_WITH_SCENE_PREPARATION
        GpuScene(Device& device, const SceneData& scene, bool meshShading = false, uint32_t workers = 0);
        GpuScene(Device& device, const ImportedAsset& asset, bool meshShading = false, uint32_t workers = 0);
#endif
        // The caller writes only after the previous frame completes.
        void setPrimitiveTransforms(uint32_t first, uint32_t count, const glm::mat4& transform);
        std::expected<void, std::string>
             setPrimitiveTransformsExpected(uint32_t first, uint32_t count, const glm::mat4& transform);
        bool primitiveMirrored(uint32_t index) const;
        std::expected<bool, std::string>                   primitiveMirroredExpected(uint32_t index) const;
        const glm::mat4&                                   primitiveTransform(uint32_t index) const;
        std::expected<const glm::mat4*, std::string>       primitiveTransformExpected(uint32_t index) const;
        const PrimitiveBounds&                             primitiveBounds(uint32_t index) const;
        std::expected<const PrimitiveBounds*, std::string> primitiveBoundsExpected(uint32_t index) const;
        uint64_t                                           transformRevision() const;

        // After a completed in-place vertex-position write, publish conservative local bounds for every primitive.
        // Topology, buffer identities and other vertex attributes stay fixed. Meshlet scenes are not supported.
        // Do not render until this succeeds and every borrowing RayScene has completed update().
        std::expected<void, std::string>
                 notifyVertexPositionsChangedExpected(std::span<const PrimitiveBounds> localBounds);
        uint64_t geometryRevision() const;

        // Lazily owns the default inline-query AS. Call between completed GPU uses; the borrowed pointer
        // and descriptor survive renderer recreation and stay valid until this scene is destroyed.
        // Repeated requests update transforms/material flags and notified vertex positions.
        // Topology or geometry buffer replacement requires a new scene.
        // Other RayScene owners use the same scene-owned BLAS with independent TLAS/instance policies.
        std::expected<RayScene*, std::string> raySceneExpected();

        bool belongsTo(const Device& device) const
        {
            return &device == &m_Device;
        }

        // Each nonzero entry certifies that all vertex and sampled base-color texture alpha
        // values for that material are exactly one. Clear the certificate before any GPU
        // geometry or texture update that could invalidate it, and before replacing a texture.
        // Install certificates before compiling a forward graph; an empty span clears them.
        std::expected<void, std::string> setImmutableOpaqueAlpha(std::span<const uint8_t> materials);
        bool                             hasImmutableOpaqueAlpha(uint32_t material) const;
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
        friend class RayScene;
        struct Uninitialized
        {};

        struct InitializationError
        {
            std::string message;
            bool        invalidInput;
        };

        GpuScene(Device& device, Uninitialized);
        std::expected<void, InitializationError>
        initialize(const SceneData& scene, const PreparedTextures& textures, const MeshletData* meshlets = nullptr);
        Device&                                  m_Device;
        std::vector<uint8_t>                     m_Mirrored;
        std::vector<glm::mat4>                   m_Transforms;
        std::vector<PrimitiveBounds>             m_LocalBounds;
        std::vector<PrimitiveBounds>             m_WorldBounds;
        uint64_t                                 m_TransformRevision = 0;
        uint64_t                                 m_GeometryRevision  = 0;
        std::vector<uint8_t>                     m_ImmutableOpaqueAlpha;
        std::unique_ptr<detail::RayGeometry>     m_RayGeometry;
        std::unique_ptr<RayScene>                m_RayScene;
    };
} // namespace vultra
