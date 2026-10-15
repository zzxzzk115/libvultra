#include <vultra/scene/scene_tree.hpp>

#include <stdexcept>
#include <string>

namespace vultra
{
    const SceneTypeCatalog& SceneTree::types() const
    {
        return m_Types;
    }

    PropertyValue SceneTree::readProperty(ObjectId id, std::string_view name) const
    {
        const auto* node = find(id);
        if (!node)
        {
            throw std::invalid_argument("Property target does not belong to this world");
        }
        const auto& property = m_Types.objects().type(node->typeName()).property(name);
        return property.read(dynamic_cast<const void*>(node));
    }

    void SceneTree::writeProperty(ObjectId               id,
                                  std::string_view       name,
                                  const PropertyValue&   value,
                                  const ProjectManifest* project)
    {
        auto* node = find(id);
        if (!node)
        {
            throw std::invalid_argument("Property target does not belong to this world");
        }
        const auto& property = m_Types.objects().type(node->typeName()).property(name);
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
                if (!target || !m_Types.objects().type(target->typeName()).isA(property.referenceType))
                {
                    throw std::invalid_argument("Reference requires a live " + std::string(property.referenceType) +
                                                " in this world");
                }
            }
            else if (property.referenceCategory == ObjectCategory::eResource)
            {
                if (findMaterial(AssetId {reference.id}))
                {
                    if (std::string_view(property.referenceType) != "MaterialResource" &&
                        std::string_view(property.referenceType) != "Resource")
                    {
                        throw std::invalid_argument("Reference has the wrong resource type");
                    }
                    return;
                }
                if (!project)
                {
                    throw std::invalid_argument("External resource references require the owning project catalog");
                }
                const auto& asset = project->asset(AssetId {reference.id});
                if (std::string_view(property.referenceType) == "ModelResource")
                {
                    const auto extension = asset.path.extension();
                    if (extension != ".gltf" && extension != ".glb" && extension != ".obj" && extension != ".fbx")
                    {
                        throw std::invalid_argument("Reference is not an importable model asset");
                    }
                }
                else if (std::string_view(property.referenceType) != "Resource")
                {
                    throw std::invalid_argument("External resource type is not registered");
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
        property.write(dynamic_cast<void*>(node), value);
    }
} // namespace vultra
