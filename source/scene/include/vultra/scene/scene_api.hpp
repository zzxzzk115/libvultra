#pragma once

#include <vultra/core/base/api_annotations.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <cstdint>
#include <string_view>

namespace vultra
{
    struct SceneTranslation
    {
        float x;
        float y;
        float z;
    };

    // Scene access is valid only during a script update callback.
    VULTRA_BIND_SCENE ObjectId sceneRootId(const SceneTree& scene);
    VULTRA_BIND_SCENE uint64_t sceneChildCount(const SceneTree& scene, ObjectId parent);
    VULTRA_BIND_SCENE ObjectId sceneChildId(const SceneTree& scene, ObjectId parent, uint64_t index);
    VULTRA_BIND_SCENE std::string_view sceneNodeName(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE SceneTranslation sceneNodeTranslation(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void sceneSetNodeTranslation(SceneTree& scene, ObjectId node, SceneTranslation translation);
} // namespace vultra
