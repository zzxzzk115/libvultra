#include "vultra/function/scene/scene_reflection.hpp"

#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"

#include <entt/entt.hpp>

namespace vultra
{
    const SceneComponentRegistry::Entry* SceneComponentRegistry::findByName(std::string_view name) const
    {
        auto it = m_ByName.find(std::string(name));
        return it != m_ByName.end() ? &it->second : nullptr;
    }

    const SceneComponentRegistry::Entry* SceneComponentRegistry::findByType(entt::id_type typeId) const
    {
        auto it = m_TypeToName.find(typeId);
        if (it == m_TypeToName.end())
            return nullptr;
        return findByName(it->second);
    }

    void registerSceneComponentMeta()
    {
        using namespace entt::literals;

        // NOTE: These meta registrations are what enables automatic field IO.
        entt::meta_factory<NameComponent>().type("NameComponent"_hs).data<&NameComponent::name>("name"_hs);

        entt::meta_factory<TransformComponent>()
            .type("TransformComponent"_hs)
            .data<&TransformComponent::position>("position"_hs)
            .data<&TransformComponent::rotation>("rotation"_hs)
            .data<&TransformComponent::scale>("scale"_hs);

        entt::meta_factory<MeshComponent>().type("MeshComponent"_hs).data<&MeshComponent::uuid>("uuid"_hs);
    }

} // namespace vultra
