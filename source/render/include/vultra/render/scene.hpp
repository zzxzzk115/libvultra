#pragma once

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/error.hpp>
#include <vultra/gpu/resources.hpp>
#include <vultra/render/meshlets.hpp>

#include <array>
#include <expected>
#include <memory>
#include <span>
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

    // The material type of model files, vultra.openpbr, is number 0 in every program's vultra.materials.
    inline constexpr uint32_t kOpenPbrMaterialType = 0;
    // The words of a vultra.openpbr parameter block (OpenPbrParameters in
    // builtin/shaders/resources/gpu_material.slangh).
    inline constexpr uint32_t kOpenPbrParameterWords = 24;

    // One record of scene.materials, indexed by material number. Keep in sync with VultraMaterialHeader in
    // builtin/shaders/resources/gpu_material.slangh.
    struct MaterialHeader
    {
        uint32_t type   = kOpenPbrMaterialType; // The type's number in the program's vultra.materials.
        uint32_t offset = 0;                    // The first word of the parameter block in scene.material_data.
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
        // Gives a material another type: its number in the vultra.materials of the programs that draw this scene,
        // and its parameter block as 32-bit words. Every material starts as vultra.openpbr with the values of its
        // SurfaceMaterial; its textures and sidedness stay those of the SurfaceMaterial. vultra.openpbr takes no
        // words: its block is written from the SurfaceMaterial, and words for it are an error. Replaces
        // materialHeaders and materialData, so call it before the scene is bound to a graph.
        [[nodiscard]] std::expected<void, Error>
                              setMaterialType(uint32_t material, uint32_t type, std::span<const uint32_t> parameters);
        const MaterialHeader& materialHeader(uint32_t material) const;
        // Writes the vultra.openpbr blocks of scene.material_data from `materials` again, so that changes to them
        // show. RenderPath::prepare() calls it every frame, after the previous frame completed.
        [[nodiscard]] std::expected<void, Error> writeMaterials();
        // Seven independently typed material slots; color slots use sRGB views.
        std::unique_ptr<Buffer> vertices;
        std::unique_ptr<Buffer> indices;
        std::unique_ptr<Buffer> transforms;
        std::unique_ptr<Buffer> materialHeaders; // scene.materials: a MaterialHeader per material.
        std::unique_ptr<Buffer> materialData;    // scene.material_data: the parameter blocks, as 32-bit words.
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
        // Creates new materialHeaders and materialData buffers and writes them.
        [[nodiscard]] std::expected<void, Error> uploadMaterials();

        Device&                      m_Device;
        std::vector<uint8_t>         m_Mirrored;
        std::vector<glm::mat4>       m_Transforms;
        std::vector<glm::mat4>       m_PreviousTransforms;
        bool                         m_FrameStarted = false;
        std::vector<PrimitiveBounds> m_LocalBounds;
        std::vector<PrimitiveBounds> m_WorldBounds;
        uint64_t                     m_TransformRevision = 0;
        std::vector<MaterialHeader>  m_MaterialHeaders;
        std::vector<uint32_t>        m_MaterialWords;
    };
} // namespace vultra
