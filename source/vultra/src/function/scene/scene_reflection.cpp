#include "vultra/function/scene/scene_reflection.hpp"

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/environment_component.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/reflection_probe_component.hpp"
#include "vultra/function/world/components/script_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/components/xr_view_component.hpp"

#include <entt/entt.hpp>
#include <entt/meta/factory.hpp>

#include <vector>

namespace vultra
{
    void registerSceneMeta()
    {
        using namespace entt::literals;

        entt::meta_factory<CoreUUID>().type("CoreUUID"_hs);

        entt::meta_factory<glm::vec3>().type("glm::vec3"_hs);
        entt::meta_factory<glm::vec4>().type("glm::vec4"_hs);
        entt::meta_factory<glm::quat>().type("glm::quat"_hs);
        entt::meta_factory<MaterialSlotOverride>()
            .type("MaterialSlotOverride"_hs)
            .data<&MaterialSlotOverride::slot>("slot"_hs)
            .data<&MaterialSlotOverride::materialGraph>("materialGraph"_hs);
        entt::meta_factory<std::vector<MaterialSlotOverride>>().type("MaterialSlotOverrideVector"_hs);

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
            .data<&MeshComponent::materialColor>("materialColor"_hs)
            .data<&MeshComponent::materialOverrides>("materialOverrides"_hs);
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
            .data<&CameraComponent::clearMode>("clearMode"_hs)
            .data<&CameraComponent::clearColor>("clearColor"_hs)
            .data<&CameraComponent::priority>("priority"_hs)
            .data<&CameraComponent::rendererKey>("rendererKey"_hs);

        entt::meta_factory<XRViewComponent>()
            .type("XRViewComponent"_hs)
            .data<&XRViewComponent::enabled>("enabled"_hs)
            .data<&XRViewComponent::trackingOrigin>("trackingOrigin"_hs)
            .data<&XRViewComponent::stereoGraphMode>("stereoGraphMode"_hs)
            .data<&XRViewComponent::fallbackMono>("fallbackMono"_hs);

        entt::meta_factory<EnvironmentComponent>()
            .type("EnvironmentComponent"_hs)
            .data<&EnvironmentComponent::active>("active"_hs)
            .data<&EnvironmentComponent::skybox>("skybox"_hs)
            .data<&EnvironmentComponent::ambientColor>("ambientColor"_hs)
            .data<&EnvironmentComponent::ambientIntensity>("ambientIntensity"_hs)
            .data<&EnvironmentComponent::enableIBL>("enableIBL"_hs)
            .data<&EnvironmentComponent::iblColor>("iblColor"_hs)
            .data<&EnvironmentComponent::iblIntensity>("iblIntensity"_hs);

        entt::meta_factory<ReflectionProbeComponent>()
            .type("ReflectionProbeComponent"_hs)
            .data<&ReflectionProbeComponent::active>("active"_hs)
            .data<&ReflectionProbeComponent::enableIBL>("enableIBL"_hs)
            .data<&ReflectionProbeComponent::environmentMap>("environmentMap"_hs)
            .data<&ReflectionProbeComponent::shape>("shape"_hs)
            .data<&ReflectionProbeComponent::boxSize>("boxSize"_hs)
            .data<&ReflectionProbeComponent::radius>("radius"_hs)
            .data<&ReflectionProbeComponent::blendDistance>("blendDistance"_hs)
            .data<&ReflectionProbeComponent::intensity>("intensity"_hs)
            .data<&ReflectionProbeComponent::priority>("priority"_hs)
            .data<&ReflectionProbeComponent::parallaxCorrection>("parallaxCorrection"_hs);

        entt::meta_factory<LightComponent>()
            .type("LightComponent"_hs)
            .data<&LightComponent::kind>("kind"_hs)
            .data<&LightComponent::color>("color"_hs)
            .data<&LightComponent::intensity>("intensity"_hs)
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
