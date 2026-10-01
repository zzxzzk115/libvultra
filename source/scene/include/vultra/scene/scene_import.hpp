#pragma once

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/scene_tree.hpp>

namespace vultra
{
    // Bake mesh nodes into one uploadable scene. Each distinct model is imported once.
    ImportedAsset importScene(const SceneTree&             tree,
                              const ProjectManifest&       project,
                              const std::filesystem::path& projectRoot,
                              const AssetImportOptions&    options = {});
} // namespace vultra
