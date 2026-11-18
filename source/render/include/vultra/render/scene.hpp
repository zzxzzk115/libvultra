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

    // A model of a scene: geometry stored once, which instances draw. Its primitives and materials are ranges of the
    // scene's; the primitives' material numbers are the scene's too.
    struct SceneModel
    {
        uint32_t firstPrimitive = 0;
        uint32_t primitiveCount = 0;
        uint32_t firstMaterial  = 0;
        uint32_t materialCount  = 0;
    };

    // An entity that draws primitives of a model with its own transform: primitives [firstPrimitive,
    // firstPrimitive + primitiveCount) of the model, counted from the model's first. A primitive's material is
    // firstMaterial plus its material's place among the model's materials, so an instance with materials of its own
    // points at copies.
    struct SceneInstance
    {
        uint32_t model          = 0;
        uint32_t firstPrimitive = 0;
        uint32_t primitiveCount = 0;
        uint32_t firstMaterial  = 0;
    };

    // One primitive of one instance: what a stage draws and culls.
    struct SceneDraw
    {
        uint32_t instance  = 0;
        uint32_t primitive = 0; // Into GpuScene::primitives.
        uint32_t material  = 0; // Into GpuScene::materials, resolved for the instance.
    };

    // One record of scene.instances. Keep in sync with builtin/shaders/resources/instance_data.slangh.
    struct InstanceData
    {
        glm::mat4  model;
        glm::mat4  normal;
        glm::vec4  properties;    // Tangent handedness, conservative sphere scale.
        glm::mat4  previousModel; // Where the previous frame drew the instance.
        glm::uvec4 references;    // Model, first primitive (of the scene), primitive count, first material.
    };

    // One record of scene.models. Keep in sync with builtin/shaders/resources/instance_data.slangh.
    struct ModelData
    {
        uint32_t firstPrimitive;
        uint32_t primitiveCount;
        uint32_t firstMaterial;
        uint32_t materialCount;
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
        // Render paths and servers keep the scene's address, so it lives behind a unique_ptr. Without models the
        // scene is one model; without instances each model is drawn once, whole.
        [[nodiscard]] static std::expected<std::unique_ptr<GpuScene>, Error>
        create(Device&                        device,
               const SceneData&               scene,
               const PreparedTextures&        textures,
               const MeshletData*             meshlets  = nullptr,
               std::span<const SceneModel>    models    = {},
               std::span<const SceneInstance> instances = {});
#ifdef VULTRA_WITH_SCENE_PREPARATION
        [[nodiscard]] static std::expected<std::unique_ptr<GpuScene>, Error>
        create(Device& device, const SceneData& scene, bool meshShading = false, uint32_t workers = 0);
        [[nodiscard]] static std::expected<std::unique_ptr<GpuScene>, Error>
        create(Device& device, const ImportedAsset& asset, bool meshShading = false, uint32_t workers = 0);
#endif
        GpuScene(const GpuScene&)            = delete;
        GpuScene& operator=(const GpuScene&) = delete;
        // The caller writes only after the previous frame completes. Until the first beginFrame(), an instance's
        // previous model follows its model, so transforms set while loading have no motion.
        [[nodiscard]] std::expected<void, Error> setInstanceTransform(uint32_t instance, const glm::mat4& transform);
        bool                                     instanceMirrored(uint32_t instance) const;
        const glm::mat4&                         instanceTransform(uint32_t instance) const;
        // The box around a draw where its instance is now.
        const PrimitiveBounds& drawBounds(uint32_t draw) const;
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
        std::unique_ptr<Buffer> instanceData;    // scene.instances: an InstanceData per instance.
        std::unique_ptr<Buffer> modelData;       // scene.models: a ModelData per model.
        std::unique_ptr<Buffer> materialHeaders; // scene.materials: a MaterialHeader per material.
        std::unique_ptr<Buffer> materialData;    // scene.material_data: the parameter blocks, as 32-bit words.
        std::vector<std::unique_ptr<Texture>>                    textures;
        std::vector<std::array<Texture*, kMaterialTextureCount>> materialTextures;
        std::vector<SurfaceMaterial>                             materials;
        std::vector<VriSamplerDesc>                              samplers;
        std::vector<ScenePrimitive>                              primitives;
        std::vector<SceneModel>                                  models;
        std::vector<SceneInstance>                               instances;
        std::vector<SceneDraw>                                   draws; // Every primitive of every instance.
        glm::vec3                                                center;
        float                                                    radius;
        std::unique_ptr<GpuMeshlets>                             meshlets;

    private:
        GpuScene(Device& device, const SceneData& scene);
        // Creates new materialHeaders and materialData buffers and writes them.
        [[nodiscard]] std::expected<void, Error> uploadMaterials();

        Device&                      m_Device;
        std::vector<uint8_t>         m_Mirrored; // Per instance, like the transforms.
        std::vector<glm::mat4>       m_Transforms;
        std::vector<glm::mat4>       m_PreviousTransforms;
        std::vector<uint32_t>        m_FirstDraw; // Per instance: its first draw; its draws follow in order.
        bool                         m_FrameStarted = false;
        std::vector<PrimitiveBounds> m_LocalBounds; // Per primitive.
        std::vector<PrimitiveBounds> m_WorldBounds; // Per draw.
        uint64_t                     m_TransformRevision = 0;
        std::vector<MaterialHeader>  m_MaterialHeaders;
        std::vector<uint32_t>        m_MaterialWords;
    };
} // namespace vultra
