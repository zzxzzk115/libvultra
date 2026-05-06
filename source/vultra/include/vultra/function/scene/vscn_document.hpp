#pragma once

#include "vultra/core/base/uuid.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    struct SceneProperty
    {
        std::string component; // e.g. "TransformComponent"
        std::string field;     // e.g. "position"
        std::string value;     // raw text (kept for readability/round-tripping)
    };

    struct SceneNode
    {
        CoreUUID    id;
        std::string name;

        // Prefab support: if set, this node is an instance of another .vscn.
        // During instantiation we load and instantiate the prefab's root subtree,
        // then apply this node's properties as overrides.
        std::string prefabUri;

        std::vector<SceneProperty>              properties;
        std::vector<std::unique_ptr<SceneNode>> children;
    };

    struct SceneDocument
    {
        uint32_t                   version {1};
        bool                       isManifest {false};
        std::unordered_map<std::string, std::string> assets;
        std::unique_ptr<SceneNode> root;
    };
} // namespace vultra
