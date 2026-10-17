#pragma once

#include <vultra/core/base/property.hpp>
#include <vultra/scene/node.hpp>
#include <vultra/scene/resource.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    // State contains type-specific JSON fields. Common hierarchy/spatial/execution fields belong to SceneTree.
    struct SceneNodeType
    {
        const ObjectTypeInfo& type;
        std::unique_ptr<Node> (*create)(std::string name, NodeId id, std::string_view state);
        std::string (*serialize)(const Node& node);
    };

    struct SceneResourceType
    {
        const ObjectTypeInfo& type;
        std::unique_ptr<Resource> (*create)(AssetId id, std::string_view state);
        std::string (*serialize)(const Resource& resource);
    };

    // Explicit project catalog. Records/code are borrowed and must outlive calls and their created C++ instances.
    class SceneTypeCatalog
    {
    public:
        SceneTypeCatalog();
        void                     add(const SceneNodeType& type);
        void                     remove(const SceneNodeType& type);
        void                     add(const SceneResourceType& type);
        void                     remove(const SceneResourceType& type);
        const SceneNodeType&     nodeType(std::string_view name) const;
        const SceneResourceType& resourceType(std::string_view name) const;
        const ObjectTypeCatalog& objects() const;
        std::unique_ptr<Node> create(std::string_view type, std::string name, NodeId id, std::string_view state) const;
        std::string           serialize(const Node& node) const;
        std::unique_ptr<Resource> createResource(std::string_view type, AssetId id, std::string_view state) const;
        std::string               serialize(const Resource& resource) const;

    private:
        void                                  addBuiltinResources();
        ObjectTypeCatalog                     m_Objects;
        std::vector<const SceneNodeType*>     m_Nodes;
        std::vector<const SceneResourceType*> m_Resources;
    };
} // namespace vultra
