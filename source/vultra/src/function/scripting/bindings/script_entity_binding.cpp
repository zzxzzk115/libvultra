#include "vultra/function/scripting/bindings/script_entity_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/bindings/script_generated_binding.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/world/components/animator_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
#include "vultra/function/world/components/ui_components.hpp"
#include "vultra/function/world/world.hpp"

namespace vultra
{
    namespace
    {
        template<typename Component>
        bool hasComponent(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            return world && world->registry().all_of<Component>(entity);
        }

        EntityStatusComponent& ensureStatus(ScriptContext& ctx, entt::entity entity)
        {
            auto& reg = ctx.world()->registry();
            if (auto* status = reg.try_get<EntityStatusComponent>(entity))
                return *status;
            return reg.emplace<EntityStatusComponent>(entity);
        }
    } // namespace

    void registerScriptEntityBindings(sol::state& lua, ScriptContext& ctx)
    {
        auto entityType = lua.new_usertype<ScriptEntity>(
            "Entity",
            "valid",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptEntity& self) { return ctx.isValid(self.value); }),
            "id",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) {
                return static_cast<uint32_t>(self.value);
            }),
            "name",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptEntity& self) {
                    auto* world = ctx.world();
                    if (!world)
                        return std::string {};
                    auto* name = world->registry().try_get<NameComponent>(self.value);
                    return name ? name->name : std::string {};
                },
                [&ctx](const ScriptEntity& self, const std::string& value) {
                    auto* world = ctx.world();
                    if (!world || !ctx.isValid(self.value))
                        return;
                    auto& reg = world->registry();
                    if (auto* name = reg.try_get<NameComponent>(self.value))
                        name->name = value;
                    else
                        reg.emplace<NameComponent>(self.value, NameComponent {value});
                }),
            "active",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptEntity& self) {
                    auto* world = ctx.world();
                    auto* status = world ? world->registry().try_get<EntityStatusComponent>(self.value) : nullptr;
                    return status ? status->active : true;
                },
                [&ctx](const ScriptEntity& self, bool value) {
                    if (ctx.world() && ctx.isValid(self.value))
                        ensureStatus(ctx, self.value).active = value;
                }),
            "visible",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptEntity& self) {
                    auto* world = ctx.world();
                    auto* status = world ? world->registry().try_get<EntityStatusComponent>(self.value) : nullptr;
                    return status ? status->visible : true;
                },
                [&ctx](const ScriptEntity& self, bool value) {
                    if (ctx.world() && ctx.isValid(self.value))
                        ensureStatus(ctx, self.value).visible = value;
                }),
            "transform",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptTransformRef {self.value}; }),
            "rectTransform",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptRectTransformRef {self.value}; }),
            "ui",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptUiRef {self.value}; }),
            "uiButton",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptUiButtonRef {self.value}; }),
            "uiToggle",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptUiToggleRef {self.value}; }),
            "uiSlider",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptUiSliderRef {self.value}; }),
            "uiProgressBar",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptUiProgressBarRef {self.value}; }),
            "rigidBody",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptRigidBodyRef {self.value}; }),
            "mesh",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptMeshRef {self.value}; }),
            // camera / light / boxShape / sphereShape / capsuleShape /
            // cylinderShape / environment / audioSource / audioListener /
            // reflectionProbe / particleEmitter accessors are generated; see
            // applyGeneratedEntityAccessors below.
            "animator",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptAnimatorRef {self.value}; }),
            "destroy",
            [&ctx](const ScriptEntity& self) {
                auto* world = ctx.world();
                if (world && ctx.isValid(self.value))
                    world->destroyRecursive(self.value);
            },
            "parent",
            [&ctx](const ScriptEntity& self) {
                auto* world = ctx.world();
                return world && ctx.isValid(self.value) ? ScriptEntity {world->parent(self.value)} : ScriptEntity {};
            },
            "firstChild",
            [&ctx](const ScriptEntity& self) {
                auto* world = ctx.world();
                return world && ctx.isValid(self.value) ? ScriptEntity {world->firstChild(self.value)} : ScriptEntity {};
            },
            "nextSibling",
            [&ctx](const ScriptEntity& self) {
                auto* world = ctx.world();
                return world && ctx.isValid(self.value) ? ScriptEntity {world->nextSibling(self.value)} : ScriptEntity {};
            },
            "setParent",
            [&ctx](const ScriptEntity& self, const ScriptEntity& parent) {
                auto* world = ctx.world();
                if (!world || !ctx.isValid(self.value))
                    return;
                world->setParent(self.value, ctx.isValid(parent.value) ? parent.value : entt::null);
            },
            "hasRigidBody",
            [&ctx](const ScriptEntity& self) { return hasComponent<RigidBodyComponent>(ctx, self.value); },
            "hasMesh",
            [&ctx](const ScriptEntity& self) { return hasComponent<MeshComponent>(ctx, self.value); },
            "hasAnimator",
            [&ctx](const ScriptEntity& self) { return hasComponent<AnimatorComponent>(ctx, self.value); },
            "hasRectTransform",
            [&ctx](const ScriptEntity& self) { return hasComponent<RectTransformComponent>(ctx, self.value); },
            "hasUiButton",
            [&ctx](const ScriptEntity& self) { return hasComponent<UiButtonComponent>(ctx, self.value); },
            "hasUiToggle",
            [&ctx](const ScriptEntity& self) { return hasComponent<UiToggleComponent>(ctx, self.value); },
            "hasUiSlider",
            [&ctx](const ScriptEntity& self) { return hasComponent<UiSliderComponent>(ctx, self.value); },
            "hasUiProgressBar",
            [&ctx](const ScriptEntity& self) { return hasComponent<UiProgressBarComponent>(ctx, self.value); });

        // generated entity.<accessor> + entity:has<Name>() for annotated
        // components (camera, light, shapes, environment, audio, probe,
        // particle) -- single source of truth with the C++ components
        applyGeneratedEntityAccessors(entityType, ctx);
    }
} // namespace vultra
