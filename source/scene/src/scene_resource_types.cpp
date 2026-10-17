#include <vultra/assets/material_properties.generated.hpp>
#include <vultra/scene/material_resource.hpp>
#include <vultra/scene/resource_properties.generated.hpp>
#include <vultra/scene/scene_types.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        std::unique_ptr<Resource> createResource(AssetId id, std::string_view state)
        {
            auto resource = std::make_unique<Resource>(id);
            deserializeProperties(resourceType(), state, resource.get());
            return resource;
        }

        std::string serializeResource(const Resource& resource)
        {
            return serializeProperties(resourceType(), &resource);
        }

        std::unique_ptr<Resource> createMaterial(AssetId id, std::string_view state)
        {
            const auto data     = Json::parse(state);
            auto       material = std::make_unique<MaterialResource>(data.at("name").get<std::string>(), id);
            if (data.contains("parameters") == data.contains("shader_material"))
            {
                throw std::invalid_argument("Material requires exactly one data category");
            }
            if (data.contains("shader_material"))
            {
                material->setShaderMaterial(MaterialInstance::parse(data.at("shader_material").dump()));
            }
            else
            {
                MaterialParameters parameters;
                deserializeProperties(materialParametersType(), data.at("parameters").dump(), &parameters);
                material->setParameters(parameters);
            }
            return material;
        }

        std::string serializeMaterial(const Resource& resource)
        {
            const auto& material = dynamic_cast<const MaterialResource&>(resource);
            auto        data     = Json::parse(serializeProperties(materialResourceType(), &material));
            if (material.kind() == MaterialResource::Kind::eShader)
            {
                data["shader_material"] = Json::parse(material.shaderMaterial().serialize());
            }
            else
            {
                data["parameters"] = Json::parse(serializeProperties(materialParametersType(), &material.parameters()));
            }
            return data.dump();
        }

        const SceneResourceType kResource {resourceType(), &createResource, &serializeResource};
        const SceneResourceType kMaterial {materialResourceType(), &createMaterial, &serializeMaterial};
    } // namespace

    void SceneTypeCatalog::addBuiltinResources()
    {
        add(kResource);
        add(kMaterial);
        // Imported geometry is registered for reference typing, but has no inline scene-document factory.
        m_Objects.add(modelResourceType());
    }

    void SceneTypeCatalog::add(const SceneResourceType& record)
    {
        if (record.type.category != ObjectCategory::eResource || !record.create || !record.serialize)
        {
            throw std::invalid_argument("Resource type requires construction and persistence callbacks");
        }
        m_Resources.reserve(m_Resources.size() + 1);
        m_Objects.add(record.type);
        if (!record.type.isA("Resource"))
        {
            m_Objects.remove(record.type);
            throw std::invalid_argument("Resource type requires registered Resource ancestry");
        }
        m_Resources.push_back(&record);
    }

    void SceneTypeCatalog::remove(const SceneResourceType& record)
    {
        const auto found = std::ranges::find(m_Resources, &record);
        if (found == m_Resources.end())
        {
            throw std::invalid_argument("Resource type does not belong to this catalog");
        }
        m_Objects.remove(record.type);
        m_Resources.erase(found);
    }

    const SceneResourceType& SceneTypeCatalog::resourceType(std::string_view name) const
    {
        const auto found = std::ranges::find_if(m_Resources,
                                                [&](const auto* record)
                                                {
                                                    return record->type.name == name;
                                                });
        if (found == m_Resources.end())
        {
            throw std::invalid_argument("Unknown or nonconstructible resource type: " + std::string(name));
        }
        return **found;
    }

    std::unique_ptr<Resource>
    SceneTypeCatalog::createResource(std::string_view type, AssetId id, std::string_view state) const
    {
        const auto&               record = resourceType(type);
        Json                      input;
        std::unique_ptr<Resource> resource;
        try
        {
            input = Json::parse(state);
            if (!input.is_object())
            {
                throw std::invalid_argument("Resource state must be a JSON object");
            }
            resource = record.create(id, state);
        }
        catch (const Json::exception& error)
        {
            throw std::invalid_argument(std::string(type) + ": " + error.what());
        }
        if (!resource || resource->typeName() != type || resource->assetId() != id)
        {
            throw std::runtime_error("Resource factory returned the wrong type/identity: " + std::string(type));
        }
        const auto expected = Json::parse(record.serialize(*resource));
        if (!expected.is_object())
        {
            throw std::invalid_argument("Resource state must be a JSON object");
        }
        for (const auto& [key, value] : input.items())
        {
            if (!expected.contains(key))
            {
                throw std::invalid_argument(std::string(type) + ": unknown resource field " + key);
            }
        }
        return resource;
    }

    std::string SceneTypeCatalog::serialize(const Resource& resource) const
    {
        auto state = resourceType(resource.typeName()).serialize(resource);
        if (!Json::parse(state).is_object())
        {
            throw std::invalid_argument("Resource state must be a JSON object");
        }
        return state;
    }
} // namespace vultra
