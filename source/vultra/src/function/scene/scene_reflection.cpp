#include "vultra/function/scene/scene_reflection.hpp"

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/world/components/animator_component.hpp"
#include "vultra/function/world/components/audio_listener_component.hpp"
#include "vultra/function/world/components/audio_source_component.hpp"
#include "vultra/function/world/components/box_shape_component.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/capsule_shape_component.hpp"
#include "vultra/function/world/components/character_controller_component.hpp"
#include "vultra/function/world/components/cylinder_shape_component.hpp"
#include "vultra/function/world/components/mesh_shape_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/environment_component.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/layer_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/particle_emitter_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/nav_agent_component.hpp"
#include "vultra/function/world/components/persistent_component.hpp"
#include "vultra/function/world/components/reflection_probe_component.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
#include "vultra/function/world/components/script_component.hpp"
#include "vultra/function/world/components/sphere_shape_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/components/ui_components.hpp"
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

        entt::meta_factory<glm::vec2>().type("glm::vec2"_hs);
        entt::meta_factory<glm::vec3>().type("glm::vec3"_hs);
        entt::meta_factory<glm::vec4>().type("glm::vec4"_hs);
        entt::meta_factory<glm::quat>().type("glm::quat"_hs);
        entt::meta_factory<MaterialPropertyBlockEntry>()
            .type("MaterialPropertyBlockEntry"_hs)
            .data<&MaterialPropertyBlockEntry::name>("name"_hs)
            .data<&MaterialPropertyBlockEntry::type>("type"_hs)
            .data<&MaterialPropertyBlockEntry::floatValue>("floatValue"_hs)
            .data<&MaterialPropertyBlockEntry::colorValue>("colorValue"_hs)
            .data<&MaterialPropertyBlockEntry::textureUri>("textureUri"_hs);
        entt::meta_factory<std::vector<MaterialPropertyBlockEntry>>().type("MaterialPropertyBlockEntryVector"_hs);
        entt::meta_factory<MaterialSlotOverride>()
            .type("MaterialSlotOverride"_hs)
            .data<&MaterialSlotOverride::slot>("slot"_hs)
            .data<&MaterialSlotOverride::material>("material"_hs)
            .data<&MaterialSlotOverride::materialGraph>("materialGraph"_hs)
            .data<&MaterialSlotOverride::properties>("properties"_hs);
        entt::meta_factory<std::vector<MaterialSlotOverride>>().type("MaterialSlotOverrideVector"_hs);

        entt::meta_factory<IDComponent>().type("IDComponent"_hs).data<&IDComponent::uuid>("uuid"_hs);

        entt::meta_factory<NameComponent>().type("NameComponent"_hs).data<&NameComponent::name>("name"_hs);

        entt::meta_factory<EntityStatusComponent>()
            .type("EntityStatusComponent"_hs)
            .data<&EntityStatusComponent::active>("active"_hs)
            .data<&EntityStatusComponent::visible>("visible"_hs)
            .data<&EntityStatusComponent::locked>("locked"_hs)
            .data<&EntityStatusComponent::selectable>("selectable"_hs);

        entt::meta_factory<LayerComponent>().type("LayerComponent"_hs).data<&LayerComponent::mask>("mask"_hs);

        entt::meta_factory<TransformComponent>()
            .type("TransformComponent"_hs)
            .data<&TransformComponent::position>("position"_hs)
            .data<&TransformComponent::rotation>("rotation"_hs)
            .data<&TransformComponent::scale>("scale"_hs);

        entt::meta_factory<RigidBodyComponent>()
            .type("RigidBodyComponent"_hs)
            .data<&RigidBodyComponent::motionType>("motionType"_hs)
            .data<&RigidBodyComponent::objectLayer>("objectLayer"_hs)
            .data<&RigidBodyComponent::isSensor>("isSensor"_hs)
            .data<&RigidBodyComponent::motionQuality>("motionQuality"_hs)
            .data<&RigidBodyComponent::allowSleeping>("allowSleeping"_hs)
            .data<&RigidBodyComponent::friction>("friction"_hs)
            .data<&RigidBodyComponent::restitution>("restitution"_hs)
            .data<&RigidBodyComponent::linearDamping>("linearDamping"_hs)
            .data<&RigidBodyComponent::angularDamping>("angularDamping"_hs)
            .data<&RigidBodyComponent::gravityFactor>("gravityFactor"_hs)
            .data<&RigidBodyComponent::linearVelocity>("linearVelocity"_hs)
            .data<&RigidBodyComponent::angularVelocity>("angularVelocity"_hs)
            .data<&RigidBodyComponent::mass>("mass"_hs)
            .data<&RigidBodyComponent::overrideMass>("overrideMass"_hs)
            .data<&RigidBodyComponent::maxLinearVelocity>("maxLinearVelocity"_hs)
            .data<&RigidBodyComponent::maxAngularVelocity>("maxAngularVelocity"_hs);

        entt::meta_factory<BoxShapeComponent>().type("BoxShapeComponent"_hs).data<&BoxShapeComponent::halfExtents>(
            "halfExtents"_hs);
        entt::meta_factory<SphereShapeComponent>().type("SphereShapeComponent"_hs).data<&SphereShapeComponent::radius>(
            "radius"_hs);
        entt::meta_factory<CapsuleShapeComponent>()
            .type("CapsuleShapeComponent"_hs)
            .data<&CapsuleShapeComponent::halfHeightOfCylinder>("halfHeightOfCylinder"_hs)
            .data<&CapsuleShapeComponent::radius>("radius"_hs);

        entt::meta_factory<CylinderShapeComponent>()
            .type("CylinderShapeComponent"_hs)
            .data<&CylinderShapeComponent::halfHeight>("halfHeight"_hs)
            .data<&CylinderShapeComponent::radius>("radius"_hs);
        entt::meta_factory<MeshShapeComponent>().type("MeshShapeComponent"_hs).data<&MeshShapeComponent::convex>(
            "convex"_hs);

        entt::meta_factory<CharacterControllerComponent>()
            .type("CharacterControllerComponent"_hs)
            .data<&CharacterControllerComponent::radius>("radius"_hs)
            .data<&CharacterControllerComponent::height>("height"_hs)
            .data<&CharacterControllerComponent::maxSlopeAngleDegrees>("maxSlopeAngleDegrees"_hs)
            .data<&CharacterControllerComponent::stepHeight>("stepHeight"_hs)
            .data<&CharacterControllerComponent::gravityFactor>("gravityFactor"_hs)
            .data<&CharacterControllerComponent::mass>("mass"_hs)
            .data<&CharacterControllerComponent::jumpSpeed>("jumpSpeed"_hs)
            .data<&CharacterControllerComponent::objectLayer>("objectLayer"_hs)
            .data<&CharacterControllerComponent::inputMove>("inputMove"_hs)
            .data<&CharacterControllerComponent::jumpRequested>("jumpRequested"_hs)
            .data<&CharacterControllerComponent::velocity>("velocity"_hs)
            .data<&CharacterControllerComponent::grounded>("grounded"_hs);

        entt::meta_factory<NavAgentComponent>()
            .type("NavAgentComponent"_hs)
            .data<&NavAgentComponent::radius>("radius"_hs)
            .data<&NavAgentComponent::height>("height"_hs)
            .data<&NavAgentComponent::speed>("speed"_hs)
            .data<&NavAgentComponent::stoppingDistance>("stoppingDistance"_hs)
            .data<&NavAgentComponent::targetPosition>("targetPosition"_hs)
            .data<&NavAgentComponent::hasTarget>("hasTarget"_hs)
            .data<&NavAgentComponent::moving>("moving"_hs);

        entt::meta_factory<PersistentComponent>()
            .type("PersistentComponent"_hs)
            .data<&PersistentComponent::keepOnLoad>("keepOnLoad"_hs);

        entt::meta_factory<MeshComponent>()
            .type("MeshComponent"_hs)
            .data<&MeshComponent::mesh>("mesh"_hs)
            .data<&MeshComponent::builtinGeometry>("builtinGeometry"_hs)
            .data<&MeshComponent::materialOverrides>("materialOverrides"_hs);
        entt::meta_factory<AnimatorComponent>()
            .type("AnimatorComponent"_hs)
            .data<&AnimatorComponent::mode>("mode"_hs)
            .data<&AnimatorComponent::skeleton>("skeleton"_hs)
            .data<&AnimatorComponent::animation>("animation"_hs)
            .data<&AnimatorComponent::playOnStart>("playOnStart"_hs)
            .data<&AnimatorComponent::playing>("playing"_hs)
            .data<&AnimatorComponent::loop>("loop"_hs)
            .data<&AnimatorComponent::speed>("speed"_hs)
            .data<&AnimatorComponent::time>("time"_hs)
            .data<&AnimatorComponent::graph>("graph"_hs)
            .data<&AnimatorComponent::applyRootMotion>("applyRootMotion"_hs);

        entt::meta_factory<AudioSourceComponent>()
            .type("AudioSourceComponent"_hs)
            .data<&AudioSourceComponent::clip>("clip"_hs)
            .data<&AudioSourceComponent::volume>("volume"_hs)
            .data<&AudioSourceComponent::pitch>("pitch"_hs)
            .data<&AudioSourceComponent::loop>("loop"_hs)
            .data<&AudioSourceComponent::playOnStart>("playOnStart"_hs)
            .data<&AudioSourceComponent::playing>("playing"_hs)
            .data<&AudioSourceComponent::spatial>("spatial"_hs)
            .data<&AudioSourceComponent::minDistance>("minDistance"_hs)
            .data<&AudioSourceComponent::maxDistance>("maxDistance"_hs)
            .data<&AudioSourceComponent::rolloff>("rolloff"_hs);

        entt::meta_factory<AudioListenerComponent>()
            .type("AudioListenerComponent"_hs)
            .data<&AudioListenerComponent::primary>("primary"_hs);

        entt::meta_factory<GaussianSplatComponent>()
            .type("GaussianSplatComponent"_hs)
            .data<&GaussianSplatComponent::gaussianSplat>("gaussianSplat"_hs);

        entt::meta_factory<CameraComponent>()
            .type("CameraComponent"_hs)
            .data<&CameraComponent::primary>("primary"_hs)
            .data<&CameraComponent::projection>("projection"_hs)
            .data<&CameraComponent::fovY>("fovY"_hs)
            // legacy .vscn key: the reader resolves fields by the name stored
            // in the file, so old scenes keep loading after the rename; the
            // writer (scene_system field list) emits only "fovY"
            .data<&CameraComponent::fovY>("fovYDegrees"_hs)
            .data<&CameraComponent::orthographicHeight>("orthographicHeight"_hs)
            .data<&CameraComponent::zNear>("zNear"_hs)
            .data<&CameraComponent::zFar>("zFar"_hs)
            .data<&CameraComponent::clearMode>("clearMode"_hs)
            .data<&CameraComponent::clearColor>("clearColor"_hs)
            .data<&CameraComponent::priority>("priority"_hs)
            .data<&CameraComponent::cullingMask>("cullingMask"_hs)
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

        entt::meta_factory<ParticleEmitterComponent>()
            .type("ParticleEmitterComponent"_hs)
            .data<&ParticleEmitterComponent::playing>("playing"_hs)
            .data<&ParticleEmitterComponent::worldSpace>("worldSpace"_hs)
            .data<&ParticleEmitterComponent::gpu>("gpu"_hs)
            .data<&ParticleEmitterComponent::maxParticles>("maxParticles"_hs)
            .data<&ParticleEmitterComponent::emissionRate>("emissionRate"_hs)
            .data<&ParticleEmitterComponent::lifetime>("lifetime"_hs)
            .data<&ParticleEmitterComponent::lifetimeVariance>("lifetimeVariance"_hs)
            .data<&ParticleEmitterComponent::spawnRadius>("spawnRadius"_hs)
            .data<&ParticleEmitterComponent::startVelocity>("startVelocity"_hs)
            .data<&ParticleEmitterComponent::velocityVariance>("velocityVariance"_hs)
            .data<&ParticleEmitterComponent::gravity>("gravity"_hs)
            .data<&ParticleEmitterComponent::startSize>("startSize"_hs)
            .data<&ParticleEmitterComponent::endSize>("endSize"_hs)
            .data<&ParticleEmitterComponent::startColor>("startColor"_hs)
            .data<&ParticleEmitterComponent::endColor>("endColor"_hs);

        entt::meta_factory<ScriptComponent>()
            .type("ScriptComponent"_hs)
            .data<&ScriptComponent::scriptUri>("scriptUri"_hs)
            .data<&ScriptComponent::enabled>("enabled"_hs);

        entt::meta_factory<CanvasComponent>()
            .type("CanvasComponent"_hs)
            .data<&CanvasComponent::enabled>("enabled"_hs)
            .data<&CanvasComponent::sortOrder>("sortOrder"_hs)
            .data<&CanvasComponent::referenceResolutionPx>("referenceResolutionPx"_hs)
            .data<&CanvasComponent::scaleMode>("scaleMode"_hs)
            .data<&CanvasComponent::renderMode>("renderMode"_hs)
            .data<&CanvasComponent::pixelsPerUnit>("pixelsPerUnit"_hs);

        entt::meta_factory<RectTransformComponent>()
            .type("RectTransformComponent"_hs)
            .data<&RectTransformComponent::anchorMin>("anchorMin"_hs)
            .data<&RectTransformComponent::anchorMax>("anchorMax"_hs)
            .data<&RectTransformComponent::pivot>("pivot"_hs)
            .data<&RectTransformComponent::anchoredPositionPx>("anchoredPositionPx"_hs)
            .data<&RectTransformComponent::sizeDeltaPx>("sizeDeltaPx"_hs)
            .data<&RectTransformComponent::rotation>("rotation"_hs)
            // legacy .vscn key (see CameraComponent::fovY note above)
            .data<&RectTransformComponent::rotation>("rotationDegrees"_hs)
            .data<&RectTransformComponent::scale>("scale"_hs);

        entt::meta_factory<UiPanelComponent>()
            .type("UiPanelComponent"_hs)
            .data<&UiPanelComponent::enabled>("enabled"_hs)
            .data<&UiPanelComponent::color>("color"_hs)
            .data<&UiPanelComponent::borderRadiusPx>("borderRadiusPx"_hs);

        entt::meta_factory<UiImageComponent>()
            .type("UiImageComponent"_hs)
            .data<&UiImageComponent::enabled>("enabled"_hs)
            .data<&UiImageComponent::texture>("texture"_hs)
            .data<&UiImageComponent::tint>("tint"_hs)
            .data<&UiImageComponent::fitMode>("fitMode"_hs);

        entt::meta_factory<UiTextComponent>()
            .type("UiTextComponent"_hs)
            .data<&UiTextComponent::enabled>("enabled"_hs)
            .data<&UiTextComponent::text>("text"_hs)
            .data<&UiTextComponent::localizationKey>("localizationKey"_hs)
            .data<&UiTextComponent::color>("color"_hs)
            .data<&UiTextComponent::fontSizePx>("fontSizePx"_hs)
            .data<&UiTextComponent::horizontalAlign>("horizontalAlign"_hs)
            .data<&UiTextComponent::verticalAlign>("verticalAlign"_hs)
            .data<&UiTextComponent::font>("font"_hs);

        entt::meta_factory<UiButtonComponent>()
            .type("UiButtonComponent"_hs)
            .data<&UiButtonComponent::enabled>("enabled"_hs)
            .data<&UiButtonComponent::interactable>("interactable"_hs)
            .data<&UiButtonComponent::targetGraphic>("targetGraphic"_hs)
            .data<&UiButtonComponent::normalColor>("normalColor"_hs)
            .data<&UiButtonComponent::hoveredColor>("hoveredColor"_hs)
            .data<&UiButtonComponent::pressedColor>("pressedColor"_hs);

        entt::meta_factory<UiToggleComponent>()
            .type("UiToggleComponent"_hs)
            .data<&UiToggleComponent::enabled>("enabled"_hs)
            .data<&UiToggleComponent::interactable>("interactable"_hs)
            .data<&UiToggleComponent::checked>("checked"_hs)
            .data<&UiToggleComponent::offColor>("offColor"_hs)
            .data<&UiToggleComponent::onColor>("onColor"_hs)
            .data<&UiToggleComponent::checkColor>("checkColor"_hs);

        entt::meta_factory<UiSliderComponent>()
            .type("UiSliderComponent"_hs)
            .data<&UiSliderComponent::enabled>("enabled"_hs)
            .data<&UiSliderComponent::interactable>("interactable"_hs)
            .data<&UiSliderComponent::value>("value"_hs)
            .data<&UiSliderComponent::minValue>("minValue"_hs)
            .data<&UiSliderComponent::maxValue>("maxValue"_hs)
            .data<&UiSliderComponent::trackColor>("trackColor"_hs)
            .data<&UiSliderComponent::fillColor>("fillColor"_hs)
            .data<&UiSliderComponent::handleColor>("handleColor"_hs);

        entt::meta_factory<UiProgressBarComponent>()
            .type("UiProgressBarComponent"_hs)
            .data<&UiProgressBarComponent::enabled>("enabled"_hs)
            .data<&UiProgressBarComponent::value>("value"_hs)
            .data<&UiProgressBarComponent::minValue>("minValue"_hs)
            .data<&UiProgressBarComponent::maxValue>("maxValue"_hs)
            .data<&UiProgressBarComponent::trackColor>("trackColor"_hs)
            .data<&UiProgressBarComponent::fillColor>("fillColor"_hs);

        entt::meta_factory<UiLayoutComponent>()
            .type("UiLayoutComponent"_hs)
            .data<&UiLayoutComponent::enabled>("enabled"_hs)
            .data<&UiLayoutComponent::kind>("kind"_hs)
            .data<&UiLayoutComponent::paddingPx>("paddingPx"_hs)
            .data<&UiLayoutComponent::marginPx>("marginPx"_hs)
            .data<&UiLayoutComponent::spacingPx>("spacingPx"_hs)
            .data<&UiLayoutComponent::cellSizePx>("cellSizePx"_hs);

        entt::meta_factory<UiInputFieldComponent>()
            .type("UiInputFieldComponent"_hs)
            .data<&UiInputFieldComponent::enabled>("enabled"_hs)
            .data<&UiInputFieldComponent::interactable>("interactable"_hs)
            .data<&UiInputFieldComponent::text>("text"_hs)
            .data<&UiInputFieldComponent::placeholder>("placeholder"_hs)
            .data<&UiInputFieldComponent::maxLength>("maxLength"_hs)
            .data<&UiInputFieldComponent::fontSizePx>("fontSizePx"_hs)
            .data<&UiInputFieldComponent::font>("font"_hs)
            .data<&UiInputFieldComponent::normalColor>("normalColor"_hs)
            .data<&UiInputFieldComponent::focusedColor>("focusedColor"_hs)
            .data<&UiInputFieldComponent::textColor>("textColor"_hs)
            .data<&UiInputFieldComponent::placeholderColor>("placeholderColor"_hs)
            .data<&UiInputFieldComponent::caretColor>("caretColor"_hs);
    }
} // namespace vultra
