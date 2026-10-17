#include <vultra/scene/render_components.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    Resource& SceneTree::addResource(std::unique_ptr<Resource> resource)
    {
        if (!resource || findResource(resource->assetId()))
        {
            throw std::invalid_argument("Resource requires a unique asset ID in this scene");
        }
        const auto& type = m_Types.objects().type(resource->typeName());
        if (type.category != ObjectCategory::eResource || !type.isA("Resource"))
        {
            throw std::invalid_argument("Scene resource requires registered Resource ancestry");
        }
        auto* material = dynamic_cast<MaterialResource*>(resource.get());
        if (material)
        {
            m_Materials.reserve(m_Materials.size() + 1);
        }
        m_Resources.push_back(std::move(resource));
        auto& added     = *m_Resources.back();
        added.m_Changes = m_Changes.get();
        if (material)
        {
            m_Materials.push_back(material);
            m_Changes->mark(SceneChange::eMaterial);
        }
        return added;
    }

    Resource* SceneTree::findResource(ObjectId id) const
    {
        const auto found = std::ranges::find_if(m_Resources,
                                                [id](const auto& resource)
                                                {
                                                    return resource->id() == id;
                                                });
        return found == m_Resources.end() ? nullptr : found->get();
    }

    Resource* SceneTree::findResource(AssetId id) const
    {
        const auto found = std::ranges::find_if(m_Resources,
                                                [id](const auto& resource)
                                                {
                                                    return resource->assetId() == id;
                                                });
        return found == m_Resources.end() ? nullptr : found->get();
    }

    std::span<const std::unique_ptr<Resource>> SceneTree::resources() const
    {
        return m_Resources;
    }

    void SceneTree::removeResource(Resource& resource)
    {
        if (findResource(resource.id()) != &resource)
        {
            throw std::invalid_argument("Resource does not belong to this scene");
        }
        if (auto* material = dynamic_cast<MaterialResource*>(&resource))
        {
            const auto asset = material->assetId();
            for (auto&& [entity, mesh] : m_Registry->view<SceneMesh>().each())
            {
                std::erase_if(mesh.materials,
                              [asset](const MeshMaterialOverride& entry)
                              {
                                  return entry.material == asset;
                              });
            }
            std::erase(m_Materials, material);
            m_Changes->mark(SceneChange::eMaterial);
        }
        // Remove borrowed indices before destroying the sole resource owner.
        const auto* target = &resource;
        std::erase_if(m_Resources,
                      [target](const auto& owned)
                      {
                          return owned.get() == target;
                      });
    }

    MaterialResource& SceneTree::addMaterial(std::unique_ptr<MaterialResource> material)
    {
        return static_cast<MaterialResource&>(addResource(std::move(material)));
    }

    MaterialResource* SceneTree::findMaterial(ObjectId id) const
    {
        return dynamic_cast<MaterialResource*>(findResource(id));
    }

    MaterialResource* SceneTree::findMaterial(AssetId id) const
    {
        return dynamic_cast<MaterialResource*>(findResource(id));
    }

    std::span<MaterialResource* const> SceneTree::materials() const
    {
        return m_Materials;
    }

    void SceneTree::removeMaterial(MaterialResource& material)
    {
        removeResource(material);
    }
} // namespace vultra
