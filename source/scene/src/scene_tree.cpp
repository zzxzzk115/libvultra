#include <vultra/assets/source_file.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/render_properties.generated.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scene/scene_types.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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

        bool containsLight(const Node& node)
        {
            return node.kind() == NodeKind::eLight || std::ranges::any_of(node.children(),
                                                                          [](const auto& child)
                                                                          {
                                                                              return containsLight(*child);
                                                                          });
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

        std::unique_ptr<Node> readNode(const Json& data, const SceneTypeCatalog& types)
        {
            const auto id    = parseNodeId(data.at("id").get<std::string>());
            const auto name  = data.at("name").get<std::string>();
            const auto type  = data.at("type").get<std::string>();
            auto       state = data;
            for (const auto* field :
                 {"id", "name", "type", "owner", "children", "transform", "process_mode", "process_priority"})
            {
                state.erase(field);
            }
            auto        node   = types.create(type, name, id, state.dump());
            const auto& schema = nodeType();
            for (const auto* property : {"processMode", "processPriority"})
            {
                const auto& info = schema.property(property);
                info.write(node.get(), deserializeProperty(info, data.at(Json::json_pointer(info.jsonPath)).dump()));
            }
            if (auto* spatial = dynamic_cast<Node3D*>(node.get()))
            {
                const auto& info = node3DType().property("localTransform");
                info.write(spatial, deserializeProperty(info, data.at("transform").dump()));
            }
            else if (data.contains("transform"))
            {
                throw std::invalid_argument("Non-spatial node cannot have a transform");
            }
            return node;
        }

        void readChildren(SceneTree& tree, Node& parent, const Json& data, const SceneTypeCatalog& types)
        {
            for (const auto& childData : data.at("children"))
            {
                auto& child = tree.addChild(parent, readNode(childData, types));
                readChildren(tree, child, childData, types);
            }
        }

        Json writeNode(const Node& node, const SceneTypeCatalog& types)
        {
            Json data;
            if (const auto* spatial = dynamic_cast<const Node3D*>(&node))
            {
                data = Json::parse(serializeProperties(node3DType(), spatial));
            }
            else
            {
                data = Json::parse(serializeProperties(nodeType(), &node));
            }
            data["id"]       = node.idInScene().value.toString();
            data["type"]     = node.typeName();
            data["children"] = Json::array();
            const auto state = Json::parse(types.serialize(node));
            if (!state.is_object())
            {
                throw std::invalid_argument("Scene type state must be a JSON object");
            }
            for (const auto& [key, value] : state.items())
            {
                if (data.contains(key) || key == "transform")
                {
                    throw std::invalid_argument("Scene type cannot overwrite common field: " + key);
                }
                data[key] = value;
            }
            for (const auto& child : node.children())
            {
                data["children"].push_back(writeNode(*child, types));
            }
            return data;
        }

        void readOwners(SceneTree& tree, Node& node, const Json& data)
        {
            const auto& property = nodeType().property("ownerId");
            property.write(&node, deserializeProperty(property, data.at("owner").dump()));
            for (size_t index = 0; index < node.children().size(); ++index)
            {
                readOwners(tree, *node.children()[index], data.at("children").at(index));
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
        m_SceneLighting = containsLight(*m_Root);
        connectComponentSignals();
        bindTree(*m_Root, this);
    }

    SceneTree::~SceneTree()
    {
        stop();
        disconnectComponentSignals();
        m_Nodes.clear();
        m_Root.reset();
        m_Materials.clear();
        m_Resources.clear();
        for (auto& systems : m_Systems)
        {
            systems.clear();
        }
    }

    SceneTree::SceneTree(std::unique_ptr<Node> root, const SceneTypeCatalog& types) :
        SceneTree(std::move(root))
    {
        m_Types = types;
    }

    SceneTree::SceneTree(SceneTree&& other)
    {
        *this = std::move(other);
    }

    SceneTree& SceneTree::operator=(SceneTree&& other)
    {
        if (this != &other)
        {
            requireMovable();
            other.requireMovable();
            stop();
            disconnectComponentSignals();
            other.disconnectComponentSignals();
            // Retire facades before replacing their component and notification storage.
            m_Nodes.clear();
            m_Root.reset();
            m_Materials.clear();
            m_Resources.clear();
            m_Changes            = std::move(other.m_Changes);
            m_Registry           = std::move(other.m_Registry);
            m_NodeEntities       = std::move(other.m_NodeEntities);
            m_PersistentEntities = std::move(other.m_PersistentEntities);
            m_Nodes              = std::move(other.m_Nodes);
            m_EntityOrder        = std::move(other.m_EntityOrder);
            m_TransformOrder     = std::move(other.m_TransformOrder);
            m_OrderRevision      = other.m_OrderRevision;
            m_TransformRevision  = other.m_TransformRevision;
            m_TransformStructure = other.m_TransformStructure;
            m_ProcessStructure   = other.m_ProcessStructure;
            m_ProcessMetadata    = other.m_ProcessMetadata;
            m_Systems            = std::move(other.m_Systems);
            m_NextSystemId       = other.m_NextSystemId;
            m_Root               = std::move(other.m_Root);
            m_Resources          = std::move(other.m_Resources);
            m_Materials          = std::move(other.m_Materials);
            m_CurrentCamera      = other.m_CurrentCamera;
            m_CurrentEnvironment = other.m_CurrentEnvironment;
            m_SceneLighting      = other.m_SceneLighting;
            m_Types              = std::move(other.m_Types);
            m_Input              = std::move(other.m_Input);
            m_Commands           = std::move(other.m_Commands);
            m_DeletionQueue      = std::move(other.m_DeletionQueue);
            m_ProcessOrder       = std::move(other.m_ProcessOrder);
            m_Accumulator        = other.m_Accumulator;
            m_TimeScale          = other.m_TimeScale;
            m_PhysicsFrame       = other.m_PhysicsFrame;
            m_Paused             = other.m_Paused;
            connectComponentSignals();
            for (auto&& [entity, metadata] : m_Registry->view<SceneNode>().each())
            {
                if (metadata.node)
                {
                    metadata.node->m_Tree = this;
                }
            }
        }
        return *this;
    }

    void SceneTree::clearInvalidOwners(Node& node)
    {
        if (node.owner())
        {
            auto* ancestor = node.parent();
            while (ancestor && ancestor != node.owner())
            {
                ancestor = ancestor->parent();
            }
            if (!ancestor)
            {
                node.setOwner(nullptr);
            }
        }
        for (const auto& child : node.children())
        {
            clearInvalidOwners(*child);
        }
    }

    const SceneChanges& SceneTree::changes() const
    {
        return *m_Changes;
    }

    Node& SceneTree::root() const
    {
        return *m_Root;
    }

    Node* SceneTree::find(ObjectId id) const
    {
        const auto handle = entity(id);
        return handle == entt::null ? nullptr : m_Registry->get<SceneNode>(handle).node;
    }

    const entt::registry& SceneTree::registry() const
    {
        return *m_Registry;
    }

    entt::entity SceneTree::entity(ObjectId id) const
    {
        const auto found = m_NodeEntities.find(id.value);
        return found == m_NodeEntities.end() ? entt::null : found->second;
    }

    void SceneTree::setLocalTransforms(std::span<const SceneTransformUpdate> updates)
    {
        for (const auto& update : updates)
        {
            const auto handle = entity(update.node);
            if (handle == entt::null || !m_Registry->all_of<SceneTransform>(handle))
            {
                throw std::invalid_argument("Transform update requires a live spatial node in this scene: " +
                                            std::to_string(update.node.value));
            }
            SceneTransform::validate(update.local);
        }
        bool changed = false;
        for (const auto& update : updates)
        {
            auto& local = m_Registry->get<SceneTransform>(entity(update.node)).local;
            if (local != update.local)
            {
                local   = update.local;
                changed = true;
            }
        }
        if (changed)
        {
            m_Changes->mark(SceneChange::eTransform);
        }
    }

    Node* SceneTree::find(NodeId id) const
    {
        const auto handle = entity(id);
        return handle == entt::null ? nullptr : m_Registry->get<SceneNode>(handle).node;
    }

    Node& SceneTree::addChild(Node& parent, std::unique_ptr<Node> child)
    {
        requireStructuralBoundary(&parent);
        if (!child || child->parent() || find(parent.id()) != &parent)
        {
            throw std::invalid_argument("Attach a new child to a node in this scene tree");
        }
        std::vector<NodeId> ids;
        collectIds(*child, ids);
        for (const auto id : ids)
        {
            if (entity(id) != entt::null)
            {
                throw std::invalid_argument("Scene node ID already belongs to this tree");
            }
        }
        auto& destination = m_Registry->get<SceneHierarchy>(parent.entity()).children;
        destination.reserve(destination.size() + 1);
        child->m_Detached->parent = &parent;
        auto&      result         = *child;
        const auto id             = result.id();
        m_Nodes.try_emplace(id.value, std::move(child));
        try
        {
            bindTree(result, this);
        }
        catch (...)
        {
            m_Nodes.erase(id.value);
            throw;
        }
        // Child attachment may grow hierarchy storage; resolve the parent again before publishing membership.
        m_Registry->get<SceneHierarchy>(parent.entity()).children.push_back(result.entity());
        m_SceneLighting |= containsLight(result);
        m_Changes->mark(SceneChange::eStructure);
        if (m_Running)
        {
            const auto wasDispatching = m_Dispatching;
            const auto wasLifecycle   = m_Lifecycle;
            m_Dispatching             = true;
            m_Lifecycle               = true;
            try
            {
                enterBranch(result);
                readyBranch(result);
                m_Dispatching = wasDispatching;
                m_Lifecycle   = wasLifecycle;
            }
            catch (...)
            {
                m_Dispatching = false;
                m_Lifecycle   = false;
                stop();
                throw;
            }
        }
        if (m_StopRequested && !m_Dispatching && !m_Flushing)
        {
            stop();
        }
        return result;
    }

    void SceneTree::reparent(Node& node, Node& newParent, bool keepGlobalTransform)
    {
        requireStructuralBoundary();
        if (&node == m_Root.get() || find(node.id()) != &node || find(newParent.id()) != &newParent)
        {
            throw std::invalid_argument("Reparent nodes in the same scene tree");
        }
        for (auto* ancestor = &newParent; ancestor; ancestor = ancestor->parent())
        {
            if (ancestor == &node)
            {
                throw std::invalid_argument("Reparent would create a scene cycle");
            }
        }
        if (node.parent() == &newParent)
        {
            return;
        }
        auto*     spatial = dynamic_cast<Node3D*>(&node);
        glm::mat4 local {1};
        if (spatial && keepGlobalTransform)
        {
            const auto* spatialParent = dynamic_cast<const Node3D*>(&newParent);
            const auto  global        = spatial->globalTransform();
            local = spatialParent ? Node3D::localForParent(global, spatialParent->globalTransform()) : global;
            SceneTransform::validate(local);
        }
        auto& destination = m_Registry->get<SceneHierarchy>(newParent.entity()).children;
        destination.reserve(destination.size() + 1);
        if (m_Running)
        {
            m_Dispatching = true;
            exitBranch(node);
            m_Dispatching = false;
        }
        auto& source = m_Registry->get<SceneHierarchy>(node.parent()->entity()).children;
        std::erase(source, node.entity());
        m_Registry->get<SceneHierarchy>(node.entity()).parent = newParent.entity();
        destination.push_back(node.entity());
        if (spatial && keepGlobalTransform)
        {
            spatial->setLocalTransform(local);
        }
        clearInvalidOwners(node);
        m_Changes->mark(SceneChange::eStructure);
        if (m_Running)
        {
            m_Dispatching = true;
            try
            {
                enterBranch(node);
                readyBranch(node);
                m_Dispatching = false;
            }
            catch (...)
            {
                m_Dispatching = false;
                stop();
                throw;
            }
        }
        if (m_StopRequested && !m_Flushing)
        {
            stop();
        }
    }

    std::unique_ptr<Node> SceneTree::remove(Node& node)
    {
        requireStructuralBoundary();
        if (&node == m_Root.get() || !node.parent() || find(node.id()) != &node)
        {
            throw std::invalid_argument("Remove a non-root node from this scene tree");
        }
        if (m_Running)
        {
            m_Dispatching = true;
            exitBranch(node);
            m_Dispatching = false;
        }
        // Runtime-only descendants belong to this world and are not part of a detachable authoring subtree.
        auto runtimeChildren = branchEntities(node.entity());
        std::erase_if(runtimeChildren,
                      [this](entt::entity handle)
                      {
                          return m_Registry->get<SceneNode>(handle).node != nullptr;
                      });
        retireRuntimeEntities(runtimeChildren);
        const auto nodeHandle = node.entity();
        auto&      children   = m_Registry->get<SceneHierarchy>(node.parent()->entity()).children;
        std::erase(children, nodeHandle);
        m_Registry->get<SceneHierarchy>(nodeHandle).parent = entt::null;
        clearInvalidOwners(node);
        auto owned  = m_Nodes.extract(node.id().value);
        auto result = std::move(owned.mapped());
        bindTree(*result, nullptr);
        if (m_CurrentCamera.value.valid() && entity(m_CurrentCamera) == entt::null)
        {
            m_CurrentCamera = {};
        }
        if (m_CurrentEnvironment.value.valid() && entity(m_CurrentEnvironment) == entt::null)
        {
            m_CurrentEnvironment = {};
        }
        m_Changes->mark(SceneChange::eStructure);
        if (m_StopRequested && !m_Flushing)
        {
            stop();
        }
        return result;
    }

    ObjectId SceneTree::currentCamera() const
    {
        const auto handle = entity(m_CurrentCamera);
        return handle == entt::null ? ObjectId {} : m_Registry->get<SceneNode>(handle).id;
    }

    void SceneTree::setCurrentCamera(ObjectId id)
    {
        if (!id.value)
        {
            if (m_CurrentCamera.value.valid())
            {
                m_CurrentCamera = {};
                m_Changes->mark(SceneChange::eCamera);
            }
            return;
        }
        const auto handle = entity(id);
        if (handle == entt::null || !m_Registry->all_of<SceneCamera, SceneTransform>(handle))
        {
            throw std::invalid_argument("Current camera must be a camera node in this scene");
        }
        if (m_CurrentCamera != m_Registry->get<SceneNode>(handle).persistentId)
        {
            m_CurrentCamera = m_Registry->get<SceneNode>(handle).persistentId;
            m_Changes->mark(SceneChange::eCamera);
        }
    }

    ObjectId SceneTree::currentEnvironment() const
    {
        const auto handle = entity(m_CurrentEnvironment);
        return handle == entt::null ? ObjectId {} : m_Registry->get<SceneNode>(handle).id;
    }

    void SceneTree::setCurrentEnvironment(ObjectId id)
    {
        if (!id.value)
        {
            if (m_CurrentEnvironment.value.valid())
            {
                m_CurrentEnvironment = {};
                m_Changes->mark(SceneChange::eEnvironment);
            }
            return;
        }
        const auto handle = entity(id);
        if (handle == entt::null || !m_Registry->all_of<SceneEnvironment>(handle))
        {
            throw std::invalid_argument("Current environment must be an environment node in this scene");
        }
        if (m_CurrentEnvironment != m_Registry->get<SceneNode>(handle).persistentId)
        {
            m_CurrentEnvironment = m_Registry->get<SceneNode>(handle).persistentId;
            m_Changes->mark(SceneChange::eEnvironment);
        }
    }

    bool SceneTree::usesSceneLighting() const
    {
        return m_SceneLighting;
    }

    void SceneTree::validateAssets(const ProjectManifest& project) const
    {
        validateReferences(&project);
        for (const auto& [entity, node, mesh] : m_Registry->view<const SceneNode, const SceneMesh>().each())
        {
            project.asset(mesh.model);
        }
        for (const auto& [entity, node, environment] :
             m_Registry->view<const SceneNode, const SceneEnvironment>().each())
        {
            if (environment.radiance.value.valid() && project.asset(environment.radiance).path.extension() != ".hdr")
            {
                throw std::invalid_argument(node.name + ": environment asset must be a Radiance HDR image");
            }
        }
        validateMaterials();
        for (const auto& material : m_Materials)
        {
            if (material->kind() == MaterialResource::Kind::eShader)
            {
                const auto& instance  = material->shaderMaterial();
                const auto  extension = project.asset(instance.shader).path.extension();
                if (extension != ".vshader" && extension != ".vshaderc")
                {
                    throw std::invalid_argument("Game material requires a .vshader/.vshaderc asset: " +
                                                material->name());
                }
                for (const auto& [name, value] : instance.overrides())
                {
                    if (const auto* texture = std::get_if<ShaderTextureValue>(&value);
                        texture && texture->asset.value.valid())
                    {
                        project.asset(texture->asset);
                    }
                }
            }
        }
    }

    void SceneTree::validateMaterials() const
    {
        for (const auto& [entity, node, mesh] : m_Registry->view<const SceneNode, const SceneMesh>().each())
        {
            for (const auto& entry : mesh.materials)
            {
                if (!findMaterial(entry.material))
                {
                    throw std::invalid_argument(node.name + ": material override references a missing resource");
                }
            }
        }
    }

    std::string SceneTree::serialize() const
    {
        return serialize(m_Types);
    }

    std::string SceneTree::serialize(const SceneTypeCatalog& types) const
    {
        validateReferences(types, nullptr);
        for (const auto& [entity, node] : m_Registry->view<const SceneNode>().each())
        {
            if (!node.node)
            {
                throw std::invalid_argument("Scene documents persist authored nodes; runtime-only ECS entities need an "
                                            "explicit snapshot schema");
            }
        }
        validateMaterials();
        Json document {{"format", "vultra.scene"},
                       {"version", kSceneVersion},
                       {"root", writeNode(*m_Root, types)},
                       {"scene_lighting", m_SceneLighting},
                       {"current_camera", nullptr},
                       {"current_environment", nullptr},
                       {"resources", Json::array()}};
        for (const auto& resource : m_Resources)
        {
            document["resources"].push_back({{"id", resource->assetId().value.toString()},
                                             {"type", resource->typeName()},
                                             {"data", Json::parse(types.serialize(*resource))}});
        }
        if (m_CurrentCamera.value.valid())
        {
            document["current_camera"] = m_CurrentCamera.value.toString();
        }
        if (m_CurrentEnvironment.value.valid())
        {
            document["current_environment"] = m_CurrentEnvironment.value.toString();
        }
        return document.dump(2) + "\n";
    }

    SceneTree SceneTree::parse(std::string_view text)
    {
        return parse(text, SceneTypeCatalog {});
    }

    SceneTree SceneTree::parse(std::string_view text, const SceneTypeCatalog& types)
    {
        try
        {
            const auto document = Json::parse(text);
            if (document.at("format") != "vultra.scene" || document.at("version") != kSceneVersion)
            {
                throw std::invalid_argument("Unsupported scene format or version");
            }
            const auto& rootData = document.at("root");
            SceneTree   tree(readNode(rootData, types), types);
            readChildren(tree, tree.root(), rootData, types);
            readOwners(tree, tree.root(), rootData);
            if (!document.at("resources").is_array())
            {
                throw std::invalid_argument("Scene resources must be an array");
            }
            for (const auto& entry : document.at("resources"))
            {
                tree.addResource(types.createResource(entry.at("type").get<std::string>(),
                                                      parseAssetId(entry.at("id").get<std::string>()),
                                                      entry.at("data").dump()));
            }
            tree.validateMaterials();
            if (!document.at("current_camera").is_null())
            {
                const auto* camera = tree.find(parseNodeId(document.at("current_camera").get<std::string>()));
                if (!camera)
                {
                    throw std::invalid_argument("Current camera references a missing node");
                }
                tree.setCurrentCamera(camera->id());
            }
            if (!document.at("current_environment").is_null())
            {
                const auto* environment = tree.find(parseNodeId(document.at("current_environment").get<std::string>()));
                if (!environment)
                {
                    throw std::invalid_argument("Current environment references a missing node");
                }
                tree.setCurrentEnvironment(environment->id());
            }
            const auto lighting = document.at("scene_lighting").get<bool>();
            if (!lighting && tree.m_SceneLighting)
            {
                throw std::invalid_argument("Authored lights require scene lighting");
            }
            tree.m_SceneLighting = lighting;
            tree.validateReferences();
            return tree;
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(std::string("Read scene: ") + error.what());
        }
    }

    void SceneTree::save(const std::filesystem::path& file) const
    {
        save(file, m_Types);
    }

    void SceneTree::save(const std::filesystem::path& file, const SceneTypeCatalog& types) const
    {
        const auto text = serialize(types);
        writeFileAtomically(file, std::as_bytes(std::span(text)));
    }

    SceneTree SceneTree::load(const std::filesystem::path& file, const AssetSource* source)
    {
        return load(file, SceneTypeCatalog {}, source);
    }

    SceneTree
    SceneTree::load(const std::filesystem::path& file, const SceneTypeCatalog& types, const AssetSource* source)
    {
        try
        {
            const auto bytes = readSourceFile(file, {}, source);
            return parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), types);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("Read scene " + file.string() + ": " + error.what());
        }
    }
} // namespace vultra
