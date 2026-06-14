#include "vultra/function/scripting/bindings/script_entity_shim.hpp"

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

    ScriptTransformRef entityTransform(ScriptContext&, const ScriptEntity& self) { return ScriptTransformRef {self.value}; }
    ScriptRectTransformRef entityRectTransform(ScriptContext&, const ScriptEntity& self)
    {
        return ScriptRectTransformRef {self.value};
    }
    ScriptUiRef entityUi(ScriptContext&, const ScriptEntity& self) { return ScriptUiRef {self.value}; }
    ScriptUiButtonRef entityUiButton(ScriptContext&, const ScriptEntity& self) { return ScriptUiButtonRef {self.value}; }
    ScriptUiToggleRef entityUiToggle(ScriptContext&, const ScriptEntity& self) { return ScriptUiToggleRef {self.value}; }
    ScriptUiSliderRef entityUiSlider(ScriptContext&, const ScriptEntity& self) { return ScriptUiSliderRef {self.value}; }
    ScriptUiProgressBarRef entityUiProgressBar(ScriptContext&, const ScriptEntity& self)
    {
        return ScriptUiProgressBarRef {self.value};
    }
    ScriptRigidBodyRef entityRigidBody(ScriptContext&, const ScriptEntity& self) { return ScriptRigidBodyRef {self.value}; }
    ScriptMeshRef entityMesh(ScriptContext&, const ScriptEntity& self) { return ScriptMeshRef {self.value}; }
    ScriptAnimatorRef entityAnimator(ScriptContext&, const ScriptEntity& self) { return ScriptAnimatorRef {self.value}; }

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
    bool entityHasRigidBody(ScriptContext& ctx, const ScriptEntity& self) { return hasComponent<RigidBodyComponent>(ctx, self.value); }
    bool entityHasMesh(ScriptContext& ctx, const ScriptEntity& self) { return hasComponent<MeshComponent>(ctx, self.value); }
    bool entityHasAnimator(ScriptContext& ctx, const ScriptEntity& self) { return hasComponent<AnimatorComponent>(ctx, self.value); }
    bool entityHasRectTransform(ScriptContext& ctx, const ScriptEntity& self) { return hasComponent<RectTransformComponent>(ctx, self.value); }
    bool entityHasUiButton(ScriptContext& ctx, const ScriptEntity& self) { return hasComponent<UiButtonComponent>(ctx, self.value); }
    bool entityHasUiToggle(ScriptContext& ctx, const ScriptEntity& self) { return hasComponent<UiToggleComponent>(ctx, self.value); }
    bool entityHasUiSlider(ScriptContext& ctx, const ScriptEntity& self) { return hasComponent<UiSliderComponent>(ctx, self.value); }
    bool entityHasUiProgressBar(ScriptContext& ctx, const ScriptEntity& self) { return hasComponent<UiProgressBarComponent>(ctx, self.value); }
} // namespace vultra
