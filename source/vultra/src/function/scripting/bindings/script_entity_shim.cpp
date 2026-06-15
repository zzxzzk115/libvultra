#include "vultra/function/scripting/bindings/script_entity_shim.hpp"

#include "vultra/function/world/components/animator_component.hpp"
#include "vultra/function/world/components/audio_listener_component.hpp"
#include "vultra/function/world/components/audio_source_component.hpp"
#include "vultra/function/world/components/box_shape_component.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/capsule_shape_component.hpp"
#include "vultra/function/world/components/cylinder_shape_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/environment_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/particle_emitter_component.hpp"
#include "vultra/function/world/components/reflection_probe_component.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
#include "vultra/function/world/components/sphere_shape_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/components/ui_components.hpp"
#include "vultra/function/world/world.hpp"

// Single source of truth mapping a Component token to its C++ component type and
// its Lua ref handle. Used by every generic component method below.
#define VULTRA_SCRIPT_COMPONENTS(X)                                                                 \
    X(eTransform, TransformComponent, ScriptTransformRef)                                           \
    X(eRigidBody, RigidBodyComponent, ScriptRigidBodyRef)                                           \
    X(eCamera, CameraComponent, ScriptCameraRef)                                                    \
    X(eLight, LightComponent, ScriptLightRef)                                                       \
    X(eMesh, MeshComponent, ScriptMeshRef)                                                          \
    X(eBoxShape, BoxShapeComponent, ScriptBoxShapeRef)                                              \
    X(eSphereShape, SphereShapeComponent, ScriptSphereShapeRef)                                     \
    X(eCapsuleShape, CapsuleShapeComponent, ScriptCapsuleShapeRef)                                  \
    X(eCylinderShape, CylinderShapeComponent, ScriptCylinderShapeRef)                               \
    X(eAnimator, AnimatorComponent, ScriptAnimatorRef)                                              \
    X(eAudioSource, AudioSourceComponent, ScriptAudioSourceRef)                                     \
    X(eAudioListener, AudioListenerComponent, ScriptAudioListenerRef)                               \
    X(eEnvironment, EnvironmentComponent, ScriptEnvironmentRef)                                      \
    X(eParticleEmitter, ParticleEmitterComponent, ScriptParticleEmitterRef)                         \
    X(eReflectionProbe, ReflectionProbeComponent, ScriptReflectionProbeRef)                         \
    X(eRectTransform, RectTransformComponent, ScriptRectTransformRef)                               \
    X(eUiButton, UiButtonComponent, ScriptUiButtonRef)                                              \
    X(eUiToggle, UiToggleComponent, ScriptUiToggleRef)                                              \
    X(eUiSlider, UiSliderComponent, ScriptUiSliderRef)                                              \
    X(eUiProgressBar, UiProgressBarComponent, ScriptUiProgressBarRef)

namespace vultra
{
    namespace
    {
        template<typename Component>
        bool hasComponentT(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            return world && ctx.isValid(entity) && world->registry().all_of<Component>(entity);
        }
        template<typename Component>
        void addComponentT(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (world && ctx.isValid(entity) && !world->registry().all_of<Component>(entity))
                world->registry().emplace<Component>(entity);
        }
        template<typename Component>
        bool removeComponentT(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world || !ctx.isValid(entity) || !world->registry().all_of<Component>(entity))
                return false;
            world->registry().remove<Component>(entity);
            return true;
        }

        bool componentHas(ScriptContext& ctx, entt::entity e, ScriptComponentType type)
        {
            switch (type)
            {
#define X(TOK, COMP, REF)                                                                          \
    case ScriptComponentType::TOK:                                                                 \
        return hasComponentT<COMP>(ctx, e);
                VULTRA_SCRIPT_COMPONENTS(X)
#undef X
            }
            return false;
        }
        sol::object componentGet(ScriptContext& ctx, entt::entity e, ScriptComponentType type, sol::state_view lua)
        {
            switch (type)
            {
#define X(TOK, COMP, REF)                                                                          \
    case ScriptComponentType::TOK:                                                                 \
        return hasComponentT<COMP>(ctx, e) ? sol::object(sol::make_object(lua, REF {e}))           \
                                           : sol::object(sol::lua_nil);
                VULTRA_SCRIPT_COMPONENTS(X)
#undef X
            }
            return sol::lua_nil;
        }
        sol::object componentAdd(ScriptContext& ctx, entt::entity e, ScriptComponentType type, sol::state_view lua)
        {
            switch (type)
            {
#define X(TOK, COMP, REF)                                                                          \
    case ScriptComponentType::TOK:                                                                 \
        addComponentT<COMP>(ctx, e);                                                               \
        return sol::make_object(lua, REF {e});
                VULTRA_SCRIPT_COMPONENTS(X)
#undef X
            }
            return sol::lua_nil;
        }
        bool componentRemove(ScriptContext& ctx, entt::entity e, ScriptComponentType type)
        {
            switch (type)
            {
#define X(TOK, COMP, REF)                                                                          \
    case ScriptComponentType::TOK:                                                                 \
        return removeComponentT<COMP>(ctx, e);
                VULTRA_SCRIPT_COMPONENTS(X)
#undef X
            }
            return false;
        }

        EntityStatusComponent& ensureStatus(ScriptContext& ctx, entt::entity entity)
        {
            auto& reg = ctx.world()->registry();
            if (auto* status = reg.try_get<EntityStatusComponent>(entity))
                return *status;
            return reg.emplace<EntityStatusComponent>(entity);
        }
    } // namespace

    bool entityGetValid(ScriptContext& ctx, const ScriptEntity& self) { return ctx.isValid(self.value); }
    std::uint32_t entityGetId(ScriptContext&, const ScriptEntity& self)
    {
        return static_cast<std::uint32_t>(self.value);
    }
    std::string entityGetName(ScriptContext& ctx, const ScriptEntity& self)
    {
        auto* world = ctx.world();
        if (!world)
            return std::string {};
        auto* name = world->registry().try_get<NameComponent>(self.value);
        return name ? name->name : std::string {};
    }
    void entitySetName(ScriptContext& ctx, const ScriptEntity& self, const std::string& value)
    {
        auto* world = ctx.world();
        if (!world || !ctx.isValid(self.value))
            return;
        auto& reg = world->registry();
        if (auto* name = reg.try_get<NameComponent>(self.value))
            name->name = value;
        else
            reg.emplace<NameComponent>(self.value, NameComponent {value});
    }
    bool entityGetActive(ScriptContext& ctx, const ScriptEntity& self)
    {
        auto* world  = ctx.world();
        auto* status = world ? world->registry().try_get<EntityStatusComponent>(self.value) : nullptr;
        return status ? status->active : true;
    }
    void entitySetActive(ScriptContext& ctx, const ScriptEntity& self, const bool& value)
    {
        if (ctx.world() && ctx.isValid(self.value))
            ensureStatus(ctx, self.value).active = value;
    }
    bool entityGetVisible(ScriptContext& ctx, const ScriptEntity& self)
    {
        auto* world  = ctx.world();
        auto* status = world ? world->registry().try_get<EntityStatusComponent>(self.value) : nullptr;
        return status ? status->visible : true;
    }
    void entitySetVisible(ScriptContext& ctx, const ScriptEntity& self, const bool& value)
    {
        if (ctx.world() && ctx.isValid(self.value))
            ensureStatus(ctx, self.value).visible = value;
    }

    ScriptTransformRef entityTransform(ScriptContext&, const ScriptEntity& self)
    {
        return ScriptTransformRef {self.value};
    }

    void entityDestroy(ScriptContext& ctx, const ScriptEntity& self)
    {
        auto* world = ctx.world();
        if (world && ctx.isValid(self.value))
            world->destroyRecursive(self.value);
    }
    ScriptEntity entityParent(ScriptContext& ctx, const ScriptEntity& self)
    {
        auto* world = ctx.world();
        return world && ctx.isValid(self.value) ? ScriptEntity {world->parent(self.value)} : ScriptEntity {};
    }
    ScriptEntity entityFirstChild(ScriptContext& ctx, const ScriptEntity& self)
    {
        auto* world = ctx.world();
        return world && ctx.isValid(self.value) ? ScriptEntity {world->firstChild(self.value)} : ScriptEntity {};
    }
    ScriptEntity entityNextSibling(ScriptContext& ctx, const ScriptEntity& self)
    {
        auto* world = ctx.world();
        return world && ctx.isValid(self.value) ? ScriptEntity {world->nextSibling(self.value)} : ScriptEntity {};
    }
    void entitySetParent(ScriptContext& ctx, const ScriptEntity& self, const ScriptEntity& parent)
    {
        auto* world = ctx.world();
        if (!world || !ctx.isValid(self.value))
            return;
        world->setParent(self.value, ctx.isValid(parent.value) ? parent.value : entt::null);
    }

    void entityRegisterComponentApi(sol::state& lua, ScriptContext& ctx)
    {
        sol::table entity = lua["Entity"];
        if (!entity.valid())
            return;

        entity["hasComponent"] = [&ctx](const ScriptEntity& self, int type) -> bool {
            return componentHas(ctx, self.value, static_cast<ScriptComponentType>(type));
        };
        entity["getComponent"] = [&ctx](const ScriptEntity& self, int type, sol::this_state ts) -> sol::object {
            return componentGet(ctx, self.value, static_cast<ScriptComponentType>(type), sol::state_view {ts});
        };
        entity["addComponent"] = [&ctx](const ScriptEntity& self, int type, sol::this_state ts) -> sol::object {
            return componentAdd(ctx, self.value, static_cast<ScriptComponentType>(type), sol::state_view {ts});
        };
        entity["removeComponent"] = [&ctx](const ScriptEntity& self, int type) -> bool {
            return componentRemove(ctx, self.value, static_cast<ScriptComponentType>(type));
        };
    }
} // namespace vultra
