#include "vultra/function/scene/scene_reflection.hpp"

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/script_component.hpp"
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
        entt::meta_factory<glm::vec4>().type("glm::vec4"_hs);
        entt::meta_factory<glm::quat>().type("glm::quat"_hs);

        entt::meta_factory<IDComponent>().type("IDComponent"_hs).data<&IDComponent::uuid>("uuid"_hs);

        entt::meta_factory<NameComponent>().type("NameComponent"_hs).data<&NameComponent::name>("name"_hs);

        entt::meta_factory<EntityStatusComponent>()
            .type("EntityStatusComponent"_hs)
            .data<&EntityStatusComponent::active>("active"_hs)
            .data<&EntityStatusComponent::visible>("visible"_hs)
            .data<&EntityStatusComponent::locked>("locked"_hs)
            .data<&EntityStatusComponent::selectable>("selectable"_hs);

        entt::meta_factory<TransformComponent>()
            .type("TransformComponent"_hs)
            .data<&TransformComponent::position>("position"_hs)
            .data<&TransformComponent::rotation>("rotation"_hs)
            .data<&TransformComponent::scale>("scale"_hs);

        entt::meta_factory<MeshComponent>()
            .type("MeshComponent"_hs)
            .data<&MeshComponent::mesh>("mesh"_hs)
            .data<&MeshComponent::builtinGeometry>("builtinGeometry"_hs)
            .data<&MeshComponent::materialColor>("materialColor"_hs);
        entt::meta_factory<GaussianSplatComponent>()
            .type("GaussianSplatComponent"_hs)
            .data<&GaussianSplatComponent::gaussianSplat>("gaussianSplat"_hs);

        entt::meta_factory<CameraComponent>()
            .type("CameraComponent"_hs)
            .data<&CameraComponent::primary>("primary"_hs)
            .data<&CameraComponent::projection>("projection"_hs)
            .data<&CameraComponent::fovYDegrees>("fovYDegrees"_hs)
            .data<&CameraComponent::orthographicHeight>("orthographicHeight"_hs)
            .data<&CameraComponent::zNear>("zNear"_hs)
            .data<&CameraComponent::zFar>("zFar"_hs)
            .data<&CameraComponent::clearColor>("clearColor"_hs)
            .data<&CameraComponent::priority>("priority"_hs)
            .data<&CameraComponent::rendererKey>("rendererKey"_hs);

        entt::meta_factory<LightComponent>()
            .type("LightComponent"_hs)
            .data<&LightComponent::kind>("kind"_hs)
            .data<&LightComponent::color>("color"_hs)
            .data<&LightComponent::intensity>("intensity"_hs)
            .data<&LightComponent::direction>("direction"_hs)
            .data<&LightComponent::range>("range"_hs)
            .data<&LightComponent::radius>("radius"_hs)
            .data<&LightComponent::width>("width"_hs)
            .data<&LightComponent::height>("height"_hs)
            .data<&LightComponent::innerConeDegrees>("innerConeDegrees"_hs)
            .data<&LightComponent::outerConeDegrees>("outerConeDegrees"_hs)
            .data<&LightComponent::castsShadow>("castsShadow"_hs)
            .data<&LightComponent::twoSided>("twoSided"_hs);

        entt::meta_factory<ScriptComponent>()
            .type("ScriptComponent"_hs)
            .data<&ScriptComponent::scriptUri>("scriptUri"_hs)
            .data<&ScriptComponent::enabled>("enabled"_hs);
    }
} // namespace vultra
