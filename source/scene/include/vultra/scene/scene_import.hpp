#pragma once

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <span>
#include <vector>

namespace vultra
{
    class GpuScene;

    struct SceneMeshInstance
    {
        NodeId                          node;
        AssetId                         model;
        glm::mat4                       bakedTransform;
        glm::mat4                       lastTransform;
        uint32_t                        firstPrimitive;
        uint32_t                        primitiveCount;
        uint32_t                        firstMaterial;
        std::vector<MaterialParameters> importedMaterials;
    };

    // Bake mesh nodes into one uploadable scene. Each distinct model is imported once.
    // Optional instance ranges let the runtime apply later node transforms without reuploading geometry.
    ImportedAsset importScene(const SceneTree&                tree,
                              const ProjectManifest&          project,
                              const std::filesystem::path&    projectRoot,
                              const AssetImportOptions&       options   = {},
                              std::vector<SceneMeshInstance>* instances = nullptr);

    bool sceneMeshTopologyMatches(const SceneTree& tree, std::span<const SceneMeshInstance> instances);
    // Previous GPU use must be complete. Retains geometry, textures, descriptors and the compiled graph.
    void syncSceneMaterials(const SceneTree& tree, std::span<const SceneMeshInstance> instances, GpuScene& gpu);

    // One consumer's revision cursor for a GPU scene and its import ranges. Reconstruct after replacing the upload.
    // No scene changes are consumed; independent views and scripts observe the same revisions.
    class SceneGpuSync
    {
    public:
        bool needsImport(const SceneTree& tree, std::span<const SceneMeshInstance> instances) const;
        bool environmentChanged(const SceneTree& tree) const;
        // Previous GPU use must be complete. Failed synchronization leaves the cursor dirty for retry.
        bool update(const SceneTree& tree, std::span<SceneMeshInstance> instances, GpuScene& gpu);

    private:
        ObjectId     m_Root {};
        SceneChanges m_Applied;
    };
} // namespace vultra
