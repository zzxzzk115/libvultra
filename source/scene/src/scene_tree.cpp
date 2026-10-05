#include <vultra/platform/os/file.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
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

        bool containsLight(const Node& node)
        {
            return node.kind() == NodeKind::eLight || std::ranges::any_of(node.children(),
                                                                          [](const auto& child)
                                                                          {
                                                                              return containsLight(*child);
                                                                          });
        }

        const char* nodeType(const Node& node)
        {
            switch (node.kind())
            {
                case NodeKind::eGroup:
                    return "Node";
                case NodeKind::eMeshInstance:
                    return "MeshInstance";
                case NodeKind::eCamera:
                    return "Camera";
                case NodeKind::eEnvironment:
                    return "Environment";
                case NodeKind::eLight:
                    switch (static_cast<const LightNode&>(node).lightKind())
                    {
                        case RenderLightKind::eDirectional:
                            return "DirectionalLight";
                        case RenderLightKind::ePoint:
                            return "PointLight";
                        case RenderLightKind::eSpot:
                            return "SpotLight";
                    }
            }
            throw std::logic_error("Unknown scene node kind");
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
                auto&       mesh      = static_cast<MeshInstanceNode&>(*node);
                const auto& overrides = data.at("materials");
                if (!overrides.is_array())
                {
                    throw std::invalid_argument("Mesh material overrides must be an array");
                }
                for (const auto& entry : overrides)
                {
                    const auto& slot = entry.at("slot");
                    if (!slot.is_number_integer() || slot < 0 || slot > UINT32_MAX)
                    {
                        throw std::invalid_argument("Mesh material slot must be a uint32 index");
                    }
                    const auto index = slot.get<uint32_t>();
                    if (std::ranges::find(mesh.materialOverrides(), index, &MeshMaterialOverride::slot) !=
                        mesh.materialOverrides().end())
                    {
                        throw std::invalid_argument("Duplicate mesh material slot");
                    }
                    mesh.setMaterial(index, parseAssetId(entry.at("resource").get<std::string>()));
                }
            }
            else if (type == "Camera")
            {
                auto        camera   = std::make_unique<CameraNode>(name, id);
                const auto& settings = data.at("camera");
                camera->setSettings({settings.at("vertical_fov").get<float>(),
                                     settings.at("near").get<float>(),
                                     settings.at("far").get<float>()});
                node = std::move(camera);
            }
            else if (type == "Environment")
            {
                auto        environment = std::make_unique<EnvironmentNode>(name, id);
                const auto& settings    = data.at("environment");
                if (!settings.at("radiance").is_null())
                {
                    environment->setRadianceAsset(parseAssetId(settings.at("radiance").get<std::string>()));
                }
                environment->setSettings({settings.at("intensity").get<float>()});
                node = std::move(environment);
            }
            else if (type == "DirectionalLight" || type == "PointLight" || type == "SpotLight")
            {
                auto kind = RenderLightKind::eDirectional;
                if (type == "PointLight")
                {
                    kind = RenderLightKind::ePoint;
                }
                else if (type == "SpotLight")
                {
                    kind = RenderLightKind::eSpot;
                }
                auto        light    = std::make_unique<LightNode>(name, kind, id);
                const auto& settings = data.at("light");
                const auto  color    = settings.at("color").get<std::array<float, 3>>();
                light->setSettings({color[0],
                                    color[1],
                                    color[2],
                                    settings.at("intensity").get<float>(),
                                    settings.at("range").get<float>(),
                                    settings.at("inner_cone").get<float>(),
                                    settings.at("outer_cone").get<float>()});
                node = std::move(light);
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
                         {"type", nodeType(node)},
                         {"transform", Json::array()},
                         {"children", Json::array()}};
            if (node.kind() == NodeKind::eMeshInstance)
            {
                const auto& mesh  = static_cast<const MeshInstanceNode&>(node);
                data["model"]     = mesh.model().value.toString();
                data["materials"] = Json::array();
                for (const auto& entry : mesh.materialOverrides())
                {
                    data["materials"].push_back({{"slot", entry.slot}, {"resource", entry.material.value.toString()}});
                }
            }
            else if (node.kind() == NodeKind::eCamera)
            {
                const auto& settings = static_cast<const CameraNode&>(node).settings();
                data["camera"]       = {{"vertical_fov", settings.verticalFov},
                                        {"near", settings.nearPlane},
                                        {"far", settings.farPlane}};
            }
            else if (node.kind() == NodeKind::eEnvironment)
            {
                const auto& environment = static_cast<const EnvironmentNode&>(node);
                data["environment"]     = {{"radiance", nullptr}, {"intensity", environment.settings().intensity}};
                if (environment.radianceAsset().value.valid())
                {
                    data["environment"]["radiance"] = environment.radianceAsset().value.toString();
                }
            }
            else if (node.kind() == NodeKind::eLight)
            {
                const auto& settings = static_cast<const LightNode&>(node).settings();
                data["light"]        = {{"color", {settings.red, settings.green, settings.blue}},
                                        {"intensity", settings.intensity},
                                        {"range", settings.range},
                                        {"inner_cone", settings.innerCone},
                                        {"outer_cone", settings.outerCone}};
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
            if (node.kind() == NodeKind::eEnvironment)
            {
                const auto asset = static_cast<const EnvironmentNode&>(node).radianceAsset();
                if (asset.value.valid() && project.asset(asset).path.extension() != ".hdr")
                {
                    throw std::invalid_argument(node.name() + ": environment asset must be a Radiance HDR image");
                }
            }
            for (const auto& child : node.children())
            {
                validateNodeAssets(*child, project);
            }
        }

        Json writeParameters(const MaterialParameters& value)
        {
            return {{"base_color", {value.baseRed, value.baseGreen, value.baseBlue, value.baseAlpha}},
                    {"base_weight", value.baseWeight},
                    {"metalness", value.baseMetalness},
                    {"diffuse_roughness", value.baseDiffuseRoughness},
                    {"specular_weight", value.specularWeight},
                    {"specular_color", {value.specularRed, value.specularGreen, value.specularBlue}},
                    {"roughness", value.specularRoughness},
                    {"specular_ior", value.specularIor},
                    {"coat_weight", value.coatWeight},
                    {"coat_roughness", value.coatRoughness},
                    {"coat_ior", value.coatIor},
                    {"emission_color", {value.emissionRed, value.emissionGreen, value.emissionBlue}},
                    {"emission_luminance", value.emissionLuminance},
                    {"normal_scale", value.normalScale},
                    {"occlusion_strength", value.occlusionStrength},
                    {"alpha_cutoff", value.alphaCutoff}};
        }

        MaterialParameters readParameters(const Json& data)
        {
            const auto base     = data.at("base_color").get<std::array<float, 4>>();
            const auto specular = data.at("specular_color").get<std::array<float, 3>>();
            const auto emission = data.at("emission_color").get<std::array<float, 3>>();
            return {base[0],
                    base[1],
                    base[2],
                    base[3],
                    data.at("base_weight").get<float>(),
                    data.at("metalness").get<float>(),
                    data.at("diffuse_roughness").get<float>(),
                    data.at("specular_weight").get<float>(),
                    specular[0],
                    specular[1],
                    specular[2],
                    data.at("roughness").get<float>(),
                    data.at("specular_ior").get<float>(),
                    data.at("coat_weight").get<float>(),
                    data.at("coat_roughness").get<float>(),
                    data.at("coat_ior").get<float>(),
                    emission[0],
                    emission[1],
                    emission[2],
                    data.at("emission_luminance").get<float>(),
                    data.at("normal_scale").get<float>(),
                    data.at("occlusion_strength").get<float>(),
                    data.at("alpha_cutoff").get<float>()};
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
        bindChanges(*m_Root, m_Changes.get());
    }

    SceneTree& SceneTree::operator=(SceneTree&& other) noexcept
    {
        if (this != &other)
        {
            // Retire borrowers before replacing their notification storage.
            m_Root.reset();
            m_Materials.clear();
            m_Changes            = std::move(other.m_Changes);
            m_Root               = std::move(other.m_Root);
            m_Materials          = std::move(other.m_Materials);
            m_CurrentCamera      = other.m_CurrentCamera;
            m_CurrentEnvironment = other.m_CurrentEnvironment;
            m_SceneLighting      = other.m_SceneLighting;
        }
        return *this;
    }

    void SceneTree::bindChanges(Node& node, SceneChanges* changes)
    {
        node.m_Changes = changes;
        for (const auto& child : node.children())
        {
            bindChanges(*child, changes);
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
        bindChanges(*parent.m_Children.back(), m_Changes.get());
        m_SceneLighting |= containsLight(*parent.m_Children.back());
        m_Changes->mark(SceneChange::eStructure);
        return *parent.m_Children.back();
    }

    void SceneTree::reparent(Node& node, Node& newParent)
    {
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
        auto& destination = newParent.m_Children;
        destination.reserve(destination.size() + 1);
        auto&      source = node.parent()->m_Children;
        const auto it     = std::ranges::find_if(source,
                                             [&](const auto& child)
                                             {
                                                 return child.get() == &node;
                                             });
        auto       moved  = std::move(*it);
        source.erase(it);
        moved->m_Parent = &newParent;
        destination.push_back(std::move(moved));
        m_Changes->mark(SceneChange::eStructure);
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
        bindChanges(*result, nullptr);
        if (m_CurrentCamera.value.valid() && findPersistent(*result, m_CurrentCamera))
        {
            m_CurrentCamera = {};
        }
        if (m_CurrentEnvironment.value.valid() && findPersistent(*result, m_CurrentEnvironment))
        {
            m_CurrentEnvironment = {};
        }
        m_Changes->mark(SceneChange::eStructure);
        return result;
    }

    ObjectId SceneTree::currentCamera() const
    {
        const auto* camera = find(m_CurrentCamera);
        return camera ? camera->id() : ObjectId {};
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
        const auto* node = find(id);
        if (!node || node->kind() != NodeKind::eCamera)
        {
            throw std::invalid_argument("Current camera must be a camera node in this scene");
        }
        if (m_CurrentCamera != node->idInScene())
        {
            m_CurrentCamera = node->idInScene();
            m_Changes->mark(SceneChange::eCamera);
        }
    }

    ObjectId SceneTree::currentEnvironment() const
    {
        const auto* environment = find(m_CurrentEnvironment);
        return environment ? environment->id() : ObjectId {};
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
        const auto* node = find(id);
        if (!node || node->kind() != NodeKind::eEnvironment)
        {
            throw std::invalid_argument("Current environment must be an environment node in this scene");
        }
        if (m_CurrentEnvironment != node->idInScene())
        {
            m_CurrentEnvironment = node->idInScene();
            m_Changes->mark(SceneChange::eEnvironment);
        }
    }

    bool SceneTree::usesSceneLighting() const
    {
        return m_SceneLighting;
    }

    void SceneTree::validateAssets(const ProjectManifest& project) const
    {
        validateNodeAssets(*m_Root, project);
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

    MaterialResource& SceneTree::addMaterial(std::unique_ptr<MaterialResource> material)
    {
        if (!material || findMaterial(material->assetId()))
        {
            throw std::invalid_argument("Material requires a unique asset ID in this scene");
        }
        m_Materials.push_back(std::move(material));
        m_Materials.back()->m_Changes = m_Changes.get();
        m_Changes->mark(SceneChange::eMaterial);
        return *m_Materials.back();
    }

    MaterialResource* SceneTree::findMaterial(ObjectId id) const
    {
        const auto found = std::ranges::find_if(m_Materials,
                                                [id](const auto& value)
                                                {
                                                    return value->id() == id;
                                                });
        return found == m_Materials.end() ? nullptr : found->get();
    }

    MaterialResource* SceneTree::findMaterial(AssetId id) const
    {
        const auto found = std::ranges::find_if(m_Materials,
                                                [id](const auto& value)
                                                {
                                                    return value->assetId() == id;
                                                });
        return found == m_Materials.end() ? nullptr : found->get();
    }

    const std::vector<std::unique_ptr<MaterialResource>>& SceneTree::materials() const
    {
        return m_Materials;
    }

    void SceneTree::removeMaterial(MaterialResource& material)
    {
        if (findMaterial(material.id()) != &material)
        {
            throw std::invalid_argument("Material does not belong to this scene");
        }
        const auto asset = material.assetId();
        auto       visit = [&](const auto& self, Node& node) -> void
        {
            if (node.kind() == NodeKind::eMeshInstance)
            {
                auto&       mesh      = static_cast<MeshInstanceNode&>(node);
                const auto& overrides = mesh.materialOverrides();
                size_t      index     = 0;
                while (index < overrides.size())
                {
                    if (overrides[index].material == asset)
                    {
                        mesh.setMaterial(overrides[index].slot, {});
                    }
                    else
                    {
                        ++index;
                    }
                }
            }
            for (const auto& child : node.children())
            {
                self(self, *child);
            }
        };
        visit(visit, *m_Root);
        const auto* target = &material;
        std::erase_if(m_Materials,
                      [target](const auto& value)
                      {
                          return value.get() == target;
                      });
        m_Changes->mark(SceneChange::eMaterial);
    }

    void SceneTree::validateMaterials() const
    {
        auto visit = [&](const auto& self, const Node& node) -> void
        {
            if (node.kind() == NodeKind::eMeshInstance)
            {
                for (const auto& entry : static_cast<const MeshInstanceNode&>(node).materialOverrides())
                {
                    if (!findMaterial(entry.material))
                    {
                        throw std::invalid_argument(node.name() + ": material override references a missing resource");
                    }
                }
            }
            for (const auto& child : node.children())
            {
                self(self, *child);
            }
        };
        visit(visit, *m_Root);
    }

    std::string SceneTree::serialize() const
    {
        validateMaterials();
        Json document {{"format", "vultra.scene"},
                       {"version", kSceneVersion},
                       {"root", writeNode(*m_Root)},
                       {"scene_lighting", m_SceneLighting},
                       {"current_camera", nullptr},
                       {"current_environment", nullptr},
                       {"materials", Json::array()}};
        for (const auto& material : m_Materials)
        {
            Json entry {{"id", material->assetId().value.toString()}, {"name", material->name()}};
            if (material->kind() == MaterialResource::Kind::eShader)
            {
                entry["shader_material"] = Json::parse(material->shaderMaterial().serialize());
            }
            else
            {
                entry["parameters"] = writeParameters(material->parameters());
            }
            document["materials"].push_back(std::move(entry));
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
            if (!document.at("materials").is_array())
            {
                throw std::invalid_argument("Scene materials must be an array");
            }
            for (const auto& entry : document.at("materials"))
            {
                auto material = std::make_unique<MaterialResource>(entry.at("name").get<std::string>(),
                                                                   parseAssetId(entry.at("id").get<std::string>()));
                if (entry.contains("parameters") == entry.contains("shader_material"))
                {
                    throw std::invalid_argument("Material requires exactly one data category");
                }
                if (entry.contains("shader_material"))
                {
                    material->setShaderMaterial(MaterialInstance::parse(entry.at("shader_material").dump()));
                }
                else
                {
                    material->setParameters(readParameters(entry.at("parameters")));
                }
                tree.addMaterial(std::move(material));
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
