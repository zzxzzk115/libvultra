#include <vultra/scene/scene_tree.hpp>

#include <stdexcept>
#include <string>

namespace vultra
{
    const SceneTypeCatalog& SceneTree::types() const
    {
        return m_Types;
    }

    Object* SceneTree::findObject(ObjectId id) const
    {
        if (auto* node = find(id))
        {
            return node;
        }
        return findResource(id);
    }

    const ObjectTypeInfo& SceneTree::objectType(ObjectId id) const
    {
        const auto* object = findObject(id);
        if (!object)
        {
            throw std::invalid_argument("Property target does not belong to this world");
        }
        return m_Types.objects().type(object->typeName());
    }

    PropertyValue SceneTree::readProperty(ObjectId id, std::string_view name) const
    {
        const auto* object = findObject(id);
        if (!object)
        {
            throw std::invalid_argument("Property target does not belong to this world");
        }
        const auto& property = m_Types.objects().type(object->typeName()).property(name);
        return property.read(dynamic_cast<const void*>(object));
    }

    void SceneTree::writeProperty(ObjectId               id,
                                  std::string_view       name,
                                  const PropertyValue&   value,
                                  const ProjectManifest* project)
    {
        auto* object = findObject(id);
        if (!object)
        {
            throw std::invalid_argument("Property target does not belong to this world");
        }
        const auto& property = m_Types.objects().type(object->typeName()).property(name);
        validatePropertyValue(m_Types, property, value, project, false);
        property.write(dynamic_cast<void*>(object), value);
    }

    void SceneTree::validatePropertyValue(const SceneTypeCatalog& types,
                                          const PropertyInfo&     property,
                                          const PropertyValue&    value,
                                          const ProjectManifest*  project,
                                          bool                    deferExternalAssets) const
    {
        property.validate(value);
        auto validateReference = [&](const PropertyValue& item)
        {
            const auto reference = std::get<PropertyReference>(item);
            if (!reference.id.valid())
            {
                return;
            }
            if (property.referenceCategory == ObjectCategory::eNode)
            {
                const auto* target = find(NodeId {reference.id});
                if (!target || !types.objects().type(target->typeName()).isA(property.referenceType))
                {
                    throw std::invalid_argument("Reference requires a live " + std::string(property.referenceType) +
                                                " in this world");
                }
            }
            else if (property.referenceCategory == ObjectCategory::eResource)
            {
                if (const auto* target = findResource(AssetId {reference.id}))
                {
                    if (!types.objects().type(target->typeName()).isA(property.referenceType))
                    {
                        throw std::invalid_argument("Reference has the wrong resource type");
                    }
                    return;
                }
                const auto externalType = std::string_view(property.referenceType);
                if (externalType != "ModelResource" && externalType != "Resource")
                {
                    throw std::invalid_argument("Reference requires a loaded " + std::string(externalType));
                }
                if (!project && deferExternalAssets)
                {
                    return;
                }
                if (!project)
                {
                    throw std::invalid_argument("External resource references require the owning project catalog");
                }
                const auto& asset = project->asset(AssetId {reference.id});
                if (externalType == "ModelResource")
                {
                    const auto extension = asset.path.extension();
                    if (extension != ".gltf" && extension != ".glb" && extension != ".obj" && extension != ".fbx")
                    {
                        throw std::invalid_argument("Reference is not an importable model asset");
                    }
                }
            }
        };
        if (property.kind == PropertyKind::eReference)
        {
            validateReference(value);
        }
        else if (property.kind == PropertyKind::eArray && property.elementKind == PropertyKind::eReference)
        {
            for (const auto& item : std::get<PropertyArray>(value).values)
            {
                validateReference(item);
            }
        }
    }

    void SceneTree::validateReferences(const ProjectManifest* project) const
    {
        validateReferences(m_Types, project);
    }

    void SceneTree::validateReferences(const SceneTypeCatalog& types, const ProjectManifest* project) const
    {
        auto validate = [&](const Object& object)
        {
            const auto& type = types.objects().type(object.typeName());
            for (const auto& property : type.properties)
            {
                if (hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
                {
                    validatePropertyValue(types,
                                          property,
                                          property.read(dynamic_cast<const void*>(&object)),
                                          project,
                                          true);
                }
            }
        };
        for (const auto handle : orderedEntities())
        {
            if (const auto* node = m_Registry->get<SceneNode>(handle).node)
            {
                validate(*node);
            }
        }
        for (const auto& resource : m_Resources)
        {
            validate(*resource);
        }
    }
} // namespace vultra
