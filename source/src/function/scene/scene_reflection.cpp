#include "vultra/function/scene/scene_reflection.hpp"

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"

#include <entt/entt.hpp>
#include <entt/meta/factory.hpp>

namespace vultra
{
    void registerSceneMeta()
    {
        using namespace entt::literals;

        entt::meta_factory<CoreUUID>().type("CoreUUID"_hs);

        entt::meta_factory<glm::vec3>().type("glm::vec3"_hs);
        entt::meta_factory<glm::quat>().type("glm::quat"_hs);

        entt::meta_factory<IDComponent>().type("IDComponent"_hs).data<&IDComponent::uuid>("uuid"_hs);

        entt::meta_factory<NameComponent>().type("NameComponent"_hs).data<&NameComponent::name>("name"_hs);

        entt::meta_factory<TransformComponent>()
            .type("TransformComponent"_hs)
            .data<&TransformComponent::position>("position"_hs)
            .data<&TransformComponent::rotation>("rotation"_hs)
            .data<&TransformComponent::scale>("scale"_hs);

        entt::meta_factory<MeshComponent>().type("MeshComponent"_hs).data<&MeshComponent::mesh>("mesh"_hs);
        entt::meta_factory<GaussianSplatComponent>()
            .type("GaussianSplatComponent"_hs)
            .data<&GaussianSplatComponent::gaussianSplat>("gaussianSplat"_hs);
    }
} // namespace vultra
