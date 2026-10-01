#pragma once

#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/object.hpp>

#include <glm/mat4x4.hpp>

#include <memory>
#include <string>
#include <vector>

namespace vultra
{
    struct NodeId
    {
        StableId    value;
        friend bool operator==(const NodeId&, const NodeId&) = default;
    };

    enum class NodeKind
    {
        eGroup,
        eMeshInstance
    };

    // Tree-owned, editable scene state. Its stable ID survives save/load; ObjectId does not.
    class Node : public Object
    {
    public:
        explicit Node(std::string name, NodeId id = {StableId::generate()});
        ~Node() override             = default;
        Node(const Node&)            = delete;
        Node& operator=(const Node&) = delete;

        NodeId                                    idInScene() const;
        const std::string&                        name() const;
        void                                      setName(std::string name);
        const glm::mat4&                          localTransform() const;
        void                                      setLocalTransform(const glm::mat4& transform);
        glm::mat4                                 globalTransform() const;
        Node*                                     parent() const;
        const std::vector<std::unique_ptr<Node>>& children() const;
        virtual NodeKind                          kind() const;

    private:
        friend class SceneTree;
        NodeId                             m_PersistentId;
        std::string                        m_Name;
        glm::mat4                          m_Transform {1};
        Node*                              m_Parent = nullptr;
        std::vector<std::unique_ptr<Node>> m_Children;
    };

    class MeshInstanceNode final : public Node
    {
    public:
        MeshInstanceNode(std::string name, AssetId model, NodeId id = {StableId::generate()});
        NodeKind kind() const override;
        AssetId  model() const;
        void     setModel(AssetId model);

    private:
        AssetId m_Model;
    };
} // namespace vultra
