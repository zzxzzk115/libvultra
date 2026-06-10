#pragma once

#include "editor_app/editor_context.hpp"

#include <entt/entity/entity.hpp>

#include <filesystem>
#include <string>

namespace vultra
{
    class World;
} // namespace vultra

namespace vultra_app
{
    struct PrefabCreateResult
    {
        bool         success {false};
        std::string  message;
        entt::entity instanceRoot {entt::null};
        std::string  prefabUri; // res:// uri of the created prefab
    };

    // Saves the subtree rooted at `root` to a .vprefab at `savePath` (must be inside the project
    // asset root), registers it as an asset, then replaces the source subtree with an instance of
    // the new prefab. Does NOT record undo or status itself beyond setting result.message; the
    // caller (command handler) owns history/status.
    PrefabCreateResult createPrefabFromEntity(EditorContext&               ctx,
                                              vultra::World&               world,
                                              entt::entity                 root,
                                              const std::filesystem::path& savePath);

    // Bakes a prefab instance back into plain entities (removes PrefabInstanceComponent from the
    // instance root). Returns false with `message` set on failure.
    bool unpackPrefab(EditorContext& ctx, vultra::World& world, entt::entity instanceRoot, std::string& message);
} // namespace vultra_app
