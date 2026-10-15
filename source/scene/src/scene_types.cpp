#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/render_properties.generated.hpp>
#include <vultra/scene/scene_types.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        AssetId parseAssetId(const std::string& text)
        {
            const auto parsed = StableId::parse(text);
            if (!parsed)
            {
                throw std::invalid_argument("Invalid scene asset ID: " + text);
            }
            return {*parsed};
        }

        template<class T>
        std::unique_ptr<Node> createPlain(std::string name, NodeId id, std::string_view state)
        {
            if (!Json::parse(state).empty())
            {
                throw std::invalid_argument("Plain node has unknown type-specific fields");
            }
            return std::make_unique<T>(std::move(name), id);
        }

        std::string serializePlain(const Node&)
        {
            return "{}";
        }

        template<NodeKind kind, RenderLightKind lightKind = RenderLightKind::eDirectional>
        std::unique_ptr<Node> createRenderNode(std::string name, NodeId id, std::string_view state)
        {
            const auto            data = Json::parse(state);
            std::unique_ptr<Node> node;
            if constexpr (kind == NodeKind::eMeshInstance)
            {
                node = std::make_unique<MeshInstance3D>(name, parseAssetId(data.at("model").get<std::string>()), id);
                auto&       mesh      = static_cast<MeshInstance3D&>(*node);
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
            else if constexpr (kind == NodeKind::eCamera)
            {
                auto           camera   = std::make_unique<Camera3D>(name, id);
                const auto&    settings = data.at("camera");
                CameraSettings value;
                deserializeProperties(cameraSettingsType(), settings.dump(), &value);
                camera->setSettings(value);
                node = std::move(camera);
            }
            else if constexpr (kind == NodeKind::eEnvironment)
            {
                auto        environment = std::make_unique<WorldEnvironment>(name, id);
                const auto& settings    = data.at("environment");
                if (!settings.at("radiance").is_null())
                {
                    environment->setRadianceAsset(parseAssetId(settings.at("radiance").get<std::string>()));
                }
                auto properties = settings;
                properties.erase("radiance");
                EnvironmentSettings value;
                deserializeProperties(environmentSettingsType(), properties.dump(), &value);
                environment->setSettings(value);
                node = std::move(environment);
            }
            else if constexpr (kind == NodeKind::eLight)
            {
                auto          light    = std::make_unique<Light3D>(name, lightKind, id);
                const auto&   settings = data.at("light");
                LightSettings value;
                deserializeProperties(lightSettingsType(), settings.dump(), &value);
                light->setSettings(value);
                node = std::move(light);
            }
            return node;
        }

        std::string serializeRenderNode(const Node& node)
        {
            auto data = Json::object();
            if (node.kind() == NodeKind::eMeshInstance)
            {
                const auto& mesh  = static_cast<const MeshInstance3D&>(node);
                data["model"]     = mesh.model().value.toString();
                data["materials"] = Json::array();
                for (const auto& entry : mesh.materialOverrides())
                {
                    data["materials"].push_back({{"slot", entry.slot}, {"resource", entry.material.value.toString()}});
                }
            }
            else if (node.kind() == NodeKind::eCamera)
            {
                const auto& settings = static_cast<const Camera3D&>(node).settings();
                data["camera"]       = Json::parse(serializeProperties(cameraSettingsType(), &settings));
            }
            else if (node.kind() == NodeKind::eEnvironment)
            {
                const auto& environment = static_cast<const WorldEnvironment&>(node);
                data["environment"] =
                    Json::parse(serializeProperties(environmentSettingsType(), &environment.settings()));
                data["environment"]["radiance"] = nullptr;
                if (environment.radianceAsset().value.valid())
                {
                    data["environment"]["radiance"] = environment.radianceAsset().value.toString();
                }
            }
            else if (node.kind() == NodeKind::eLight)
            {
                const auto& settings = static_cast<const Light3D&>(node).settings();
                data["light"]        = Json::parse(serializeProperties(lightSettingsType(), &settings));
            }
            return data.dump();
        }

        const ObjectTypeInfo& kNode        = nodeType();
        const ObjectTypeInfo& kNode3D      = node3DType();
        const ObjectTypeInfo& kMesh        = meshInstance3DType();
        const ObjectTypeInfo& kCamera      = camera3DType();
        const ObjectTypeInfo& kEnvironment = worldEnvironmentType();
        const ObjectTypeInfo& kLight       = light3DType();
        const ObjectTypeInfo  kDirectional {"DirectionalLight3D", kLight.properties, &kLight, ObjectCategory::eNode};
        const ObjectTypeInfo  kOmni {"OmniLight3D", kLight.properties, &kLight, ObjectCategory::eNode};
        const ObjectTypeInfo  kSpot {"SpotLight3D", kLight.properties, &kLight, ObjectCategory::eNode};
        const SceneNodeType   kBuiltinNodes[] {
            {kNode, &createPlain<Node>, &serializePlain},
            {kNode3D, &createPlain<Node3D>, &serializePlain},
            {kMesh, &createRenderNode<NodeKind::eMeshInstance>, &serializeRenderNode},
            {kCamera, &createRenderNode<NodeKind::eCamera>, &serializeRenderNode},
            {kEnvironment, &createRenderNode<NodeKind::eEnvironment>, &serializeRenderNode},
            {kDirectional, &createRenderNode<NodeKind::eLight>, &serializeRenderNode},
            {kOmni, &createRenderNode<NodeKind::eLight, RenderLightKind::ePoint>, &serializeRenderNode},
            {kSpot, &createRenderNode<NodeKind::eLight, RenderLightKind::eSpot>, &serializeRenderNode}};
    } // namespace

    SceneTypeCatalog::SceneTypeCatalog()
    {
        for (const auto& record : kBuiltinNodes)
        {
            if (&record.type == &kDirectional)
            {
                m_Objects.add(kLight);
            }
            add(record);
        }
    }

    void SceneTypeCatalog::add(const SceneNodeType& record)
    {
        if (record.type.category != ObjectCategory::eNode || !record.create || !record.serialize)
        {
            throw std::invalid_argument("Scene type requires Node ancestry, construction and persistence callbacks");
        }
        m_Nodes.reserve(m_Nodes.size() + 1);
        m_Objects.add(record.type);
        if (!record.type.isA("Node"))
        {
            m_Objects.remove(record.type);
            throw std::invalid_argument("Scene type requires registered Node ancestry");
        }
        m_Nodes.push_back(&record);
    }

    void SceneTypeCatalog::remove(const SceneNodeType& record)
    {
        const auto found = std::ranges::find(m_Nodes, &record);
        if (found == m_Nodes.end())
        {
            throw std::invalid_argument("Scene type does not belong to this catalog");
        }
        m_Objects.remove(record.type);
        m_Nodes.erase(found);
    }

    const SceneNodeType& SceneTypeCatalog::nodeType(std::string_view name) const
    {
        const auto found = std::ranges::find_if(m_Nodes,
                                                [&](const auto* record)
                                                {
                                                    return record->type.name == name;
                                                });
        if (found == m_Nodes.end())
        {
            throw std::invalid_argument("Unknown or nonconstructible scene node type: " + std::string(name));
        }
        return **found;
    }

    const ObjectTypeCatalog& SceneTypeCatalog::objects() const
    {
        return m_Objects;
    }

    std::unique_ptr<Node>
    SceneTypeCatalog::create(std::string_view type, std::string name, NodeId id, std::string_view state) const
    {
        auto node = nodeType(type).create(name, id, state);
        if (!node || node->typeName() != type || node->name() != name || node->idInScene() != id)
        {
            throw std::runtime_error("Scene factory returned the wrong type/name/identity: " + std::string(type));
        }
        const auto input    = Json::parse(state);
        const auto expected = Json::parse(nodeType(type).serialize(*node));
        if (!input.is_object() || !expected.is_object())
        {
            throw std::invalid_argument("Scene type state must be a JSON object");
        }
        for (const auto& [key, value] : input.items())
        {
            if (!expected.contains(key))
            {
                throw std::invalid_argument(std::string(type) + ": unknown type-specific field " + key);
            }
        }
        return node;
    }

    std::string SceneTypeCatalog::serialize(const Node& node) const
    {
        return nodeType(node.typeName()).serialize(node);
    }
} // namespace vultra
