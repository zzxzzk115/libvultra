#include <vultra/platform/os/file.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <vector>

namespace vultra
{
    namespace
    {
        using Json                  = nlohmann::json;
        constexpr int kSceneVersion = 1;

        Node* findObject(Node& node, ObjectId id)
        {
            if (node.id() == id)
            {
                return &node;
            }
            for (const auto& child : node.children())
            {
                if (auto* found = findObject(*child, id))
                {
                    return found;
                }
            }
            return nullptr;
        }

        Node* findPersistent(Node& node, NodeId id)
        {
            if (node.idInScene() == id)
            {
                return &node;
            }
            for (const auto& child : node.children())
            {
                if (auto* found = findPersistent(*child, id))
                {
                    return found;
                }
            }
            return nullptr;
        }

        void collectIds(const Node& node, std::vector<NodeId>& ids)
        {
            if (std::ranges::find(ids, node.idInScene()) != ids.end())
            {
                throw std::invalid_argument("Duplicate scene node ID");
            }
            ids.push_back(node.idInScene());
            for (const auto& child : node.children())
            {
                collectIds(*child, ids);
            }
        }

        NodeId parseNodeId(const std::string& text)
        {
            const auto parsed = StableId::parse(text);
            if (!parsed)
            {
                throw std::invalid_argument("Invalid scene node ID: " + text);
            }
            return {*parsed};
        }

        AssetId parseAssetId(const std::string& text)
        {
            const auto parsed = StableId::parse(text);
            if (!parsed)
            {
                throw std::invalid_argument("Invalid scene asset ID: " + text);
            }
            return {*parsed};
        }

        std::unique_ptr<Node> readNode(const Json& data)
        {
            const auto            id   = parseNodeId(data.at("id").get<std::string>());
            const auto            name = data.at("name").get<std::string>();
            const auto            type = data.at("type").get<std::string>();
            std::unique_ptr<Node> node;
            if (type == "Node")
            {
                node = std::make_unique<Node>(name, id);
            }
            else if (type == "MeshInstance")
            {
                node = std::make_unique<MeshInstanceNode>(name, parseAssetId(data.at("model").get<std::string>()), id);
            }
            else
            {
                throw std::invalid_argument("Unknown scene node type: " + type);
            }
            const auto values = data.at("transform").get<std::vector<float>>();
            if (values.size() != 16)
            {
                throw std::invalid_argument("Scene node transform requires 16 values");
            }
            glm::mat4 transform {1};
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    transform[column][row] = values[size_t(column * 4 + row)];
                }
            }
            node->setLocalTransform(transform);
            return node;
        }

        void readChildren(SceneTree& tree, Node& parent, const Json& data)
        {
            for (const auto& childData : data.at("children"))
            {
                auto& child = tree.addChild(parent, readNode(childData));
                readChildren(tree, child, childData);
            }
        }

        Json writeNode(const Node& node)
        {
            Json data = {{"id", node.idInScene().value.toString()},
                         {"name", node.name()},
                         {"type", node.kind() == NodeKind::eMeshInstance ? "MeshInstance" : "Node"},
                         {"transform", Json::array()},
                         {"children", Json::array()}};
            if (node.kind() == NodeKind::eMeshInstance)
            {
                data["model"] = static_cast<const MeshInstanceNode&>(node).model().value.toString();
            }
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    data["transform"].push_back(node.localTransform()[column][row]);
                }
            }
            for (const auto& child : node.children())
            {
                data["children"].push_back(writeNode(*child));
            }
            return data;
        }

        void validateNodeAssets(const Node& node, const ProjectManifest& project)
        {
            if (node.kind() == NodeKind::eMeshInstance)
            {
                project.asset(static_cast<const MeshInstanceNode&>(node).model());
            }
            for (const auto& child : node.children())
            {
                validateNodeAssets(*child, project);
            }
        }
    } // namespace

    SceneTree::SceneTree(std::unique_ptr<Node> root) :
        m_Root(std::move(root))
    {
        if (!m_Root || m_Root->parent())
        {
            throw std::invalid_argument("SceneTree requires an unattached root node");
        }
        std::vector<NodeId> ids;
        collectIds(*m_Root, ids);
    }

    Node& SceneTree::root() const
    {
        return *m_Root;
    }

    Node* SceneTree::find(ObjectId id) const
    {
        return id.value ? findObject(*m_Root, id) : nullptr;
    }

    Node* SceneTree::find(NodeId id) const
    {
        return id.value.valid() ? findPersistent(*m_Root, id) : nullptr;
    }

    Node& SceneTree::addChild(Node& parent, std::unique_ptr<Node> child)
    {
        if (!child || child->parent() || find(parent.id()) != &parent)
        {
            throw std::invalid_argument("Attach a new child to a node in this scene tree");
        }
        std::vector<NodeId> ids;
        collectIds(*child, ids);
        for (const auto id : ids)
        {
            if (find(id))
            {
                throw std::invalid_argument("Scene node ID already belongs to this tree");
            }
        }
        child->m_Parent = &parent;
        parent.m_Children.push_back(std::move(child));
        return *parent.m_Children.back();
    }

    std::unique_ptr<Node> SceneTree::remove(Node& node)
    {
        if (&node == m_Root.get() || !node.parent() || find(node.id()) != &node)
        {
            throw std::invalid_argument("Remove a non-root node from this scene tree");
        }
        auto&      children = node.parent()->m_Children;
        const auto it       = std::ranges::find_if(children,
                                                   [&](const auto& child)
                                                   {
                                                 return child.get() == &node;
                                                   });
        auto       result   = std::move(*it);
        children.erase(it);
        result->m_Parent = nullptr;
        return result;
    }

    void SceneTree::validateAssets(const ProjectManifest& project) const
    {
        validateNodeAssets(*m_Root, project);
    }

    std::string SceneTree::serialize() const
    {
        return Json({{"format", "vultra.scene"}, {"version", kSceneVersion}, {"root", writeNode(*m_Root)}}).dump(2) +
               "\n";
    }

    SceneTree SceneTree::parse(std::string_view text)
    {
        try
        {
            const auto document = Json::parse(text);
            if (document.at("format") != "vultra.scene" || document.at("version") != kSceneVersion)
            {
                throw std::invalid_argument("Unsupported scene format or version");
            }
            const auto& rootData = document.at("root");
            SceneTree   tree(readNode(rootData));
            readChildren(tree, tree.root(), rootData);
            return tree;
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(std::string("Read scene: ") + error.what());
        }
    }

    void SceneTree::save(const std::filesystem::path& file) const
    {
        const auto text = serialize();
        writeFileAtomically(file, std::as_bytes(std::span(text)));
    }

    SceneTree SceneTree::load(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Open scene: " + file.string());
        }
        const std::string text(std::istreambuf_iterator<char> {input}, {});
        if (input.bad())
        {
            throw std::runtime_error("Read scene: " + file.string());
        }
        try
        {
            return parse(text);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("Read scene " + file.string() + ": " + error.what());
        }
    }
} // namespace vultra
