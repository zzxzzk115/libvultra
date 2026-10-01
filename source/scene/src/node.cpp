#include <vultra/scene/node.hpp>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace vultra
{
    Node::Node(std::string name, NodeId id) :
        m_PersistentId(id),
        m_Name(std::move(name))
    {
        if (!id.value.valid() || m_Name.empty())
        {
            throw std::invalid_argument("Scene node requires a stable ID and name");
        }
    }

    NodeId Node::idInScene() const
    {
        return m_PersistentId;
    }

    const std::string& Node::name() const
    {
        return m_Name;
    }

    void Node::setName(std::string name)
    {
        if (name.empty())
        {
            throw std::invalid_argument("Scene node name is empty");
        }
        m_Name = std::move(name);
    }

    const glm::mat4& Node::localTransform() const
    {
        return m_Transform;
    }

    void Node::setLocalTransform(const glm::mat4& transform)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!std::isfinite(transform[column][row]))
                {
                    throw std::invalid_argument("Scene node transform has a non-finite value");
                }
            }
        }
        m_Transform = transform;
    }

    glm::mat4 Node::globalTransform() const
    {
        return m_Parent ? m_Parent->globalTransform() * m_Transform : m_Transform;
    }

    Node* Node::parent() const
    {
        return m_Parent;
    }

    const std::vector<std::unique_ptr<Node>>& Node::children() const
    {
        return m_Children;
    }

    NodeKind Node::kind() const
    {
        return NodeKind::eGroup;
    }

    MeshInstanceNode::MeshInstanceNode(std::string name, AssetId model, NodeId id) :
        Node(std::move(name), id),
        m_Model(model)
    {
        if (!model.value.valid())
        {
            throw std::invalid_argument("Mesh instance requires a model asset ID");
        }
    }

    NodeKind MeshInstanceNode::kind() const
    {
        return NodeKind::eMeshInstance;
    }

    AssetId MeshInstanceNode::model() const
    {
        return m_Model;
    }

    void MeshInstanceNode::setModel(AssetId model)
    {
        if (!model.value.valid())
        {
            throw std::invalid_argument("Mesh instance model asset ID is empty");
        }
        m_Model = model;
    }
} // namespace vultra
