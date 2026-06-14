#include "vultra/function/scripting/bindings/script_world_shim.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/world/components/animator_component.hpp"
#include "vultra/function/world/components/box_shape_component.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/layer_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
#include "vultra/function/world/components/sphere_shape_component.hpp"
#include "vultra/function/world/world.hpp"

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        ScriptEntity makeEntity(entt::entity entity) { return ScriptEntity {entity}; }
        glm::vec4    toGlmVec4(const ScriptVec4& v) { return {v.x, v.y, v.z, v.w}; }

        NameComponent& ensureName(World& world, entt::entity entity)
        {
            auto& reg = world.registry();
            if (auto* name = reg.try_get<NameComponent>(entity))
                return *name;
            return reg.emplace<NameComponent>(entity);
        }

        bool hasPrefix(const std::string& value, const std::string& prefix)
        {
            return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
        }

        template<typename Component>
        Component& requireComponent(ScriptContext& ctx, entt::entity entity, const char* name)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");
            auto* component = world->registry().try_get<Component>(entity);
            if (!component)
                throw std::runtime_error(std::string("Entity has no ") + name);
            return *component;
        }

        MaterialSlotOverride& materialSlotOverride(MeshComponent& mesh, const uint32_t slot)
        {
            const auto it = std::find_if(mesh.materialOverrides.begin(), mesh.materialOverrides.end(),
                                         [slot](const MaterialSlotOverride& v) { return v.slot == slot; });
            if (it != mesh.materialOverrides.end())
                return *it;
            auto& value = mesh.materialOverrides.emplace_back();
            value.slot  = slot;
            return value;
        }

        MaterialPropertyBlockEntry& materialProperty(MaterialSlotOverride& slotOverride, const std::string& name)
        {
            const auto it = std::find_if(slotOverride.properties.begin(), slotOverride.properties.end(),
                                         [&name](const MaterialPropertyBlockEntry& v) { return v.name == name; });
            if (it != slotOverride.properties.end())
                return *it;
            auto& value = slotOverride.properties.emplace_back();
            value.name  = name;
            return value;
        }

        void removeEmptyMaterialSlotOverride(MeshComponent& mesh, const uint32_t slot)
        {
            std::erase_if(mesh.materialOverrides, [slot](const MaterialSlotOverride& v) {
                return v.slot == slot && v.material.empty() && v.materialGraph.empty() && v.properties.empty();
            });
        }

        template<typename Component, typename Ref>
        Ref addComponent(ScriptContext& ctx, const ScriptEntity& entity)
        {
            auto* world = ctx.world();
            if (!world || !ctx.isValid(entity.value))
                return {};
            auto& reg = world->registry();
            if (!reg.all_of<Component>(entity.value))
                reg.emplace<Component>(entity.value);
            return Ref {entity.value};
        }

        template<typename Component>
        bool removeComponent(ScriptContext& ctx, const ScriptEntity& entity)
        {
            auto* world = ctx.world();
            if (!world || !ctx.isValid(entity.value))
                return false;
            auto& reg = world->registry();
            if (!reg.all_of<Component>(entity.value))
                return false;
            reg.remove<Component>(entity.value);
            return true;
        }
    } // namespace

    ScriptEntity worldCreate(ScriptContext& ctx, sol::optional<std::string> name)
    {
        auto* world = ctx.world();
        if (!world)
            return {};
        const auto entity = world->createEntity();
        if (name && !name->empty())
            ensureName(*world, entity).name = *name;
        return makeEntity(entity);
    }
    void worldDestroy(ScriptContext& ctx, const ScriptEntity& entity)
    {
        auto* world = ctx.world();
        if (world && ctx.isValid(entity.value))
            world->destroyRecursive(entity.value);
    }
    std::uint32_t worldCount(ScriptContext& ctx)
    {
        auto* w = ctx.world();
        if (!w)
            return 0u;
        std::uint32_t count = 0;
        for (auto entity : w->registry().view<IDComponent>())
        {
            (void)entity;
            ++count;
        }
        return count;
    }
    ScriptEntity worldFindByName(ScriptContext& ctx, const std::string& name)
    {
        auto* world = ctx.world();
        if (!world)
            return {};
        auto view = world->registry().view<NameComponent>();
        for (auto entity : view)
        {
            if (view.get<NameComponent>(entity).name == name)
                return makeEntity(entity);
        }
        return {};
    }
    sol::table worldFindByNamePrefix(ScriptContext& ctx, sol::this_state luaState, const std::string& prefix)
    {
        sol::state_view lua(luaState);
        auto            result = lua.create_table();
        auto*           world  = ctx.world();
        if (!world)
            return result;
        uint32_t index = 1;
        auto     view  = world->registry().view<NameComponent>();
        for (auto entity : view)
        {
            if (hasPrefix(view.get<NameComponent>(entity).name, prefix))
                result[index++] = makeEntity(entity);
        }
        return result;
    }
    sol::table worldEntities(ScriptContext& ctx, sol::this_state luaState)
    {
        sol::state_view lua(luaState);
        auto            result = lua.create_table();
        auto*           world  = ctx.world();
        if (!world)
            return result;
        uint32_t index = 1;
        for (auto entity : world->registry().view<IDComponent>())
            result[index++] = makeEntity(entity);
        return result;
    }

    ScriptRigidBodyRef worldAddRigidBody(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return addComponent<RigidBodyComponent, ScriptRigidBodyRef>(ctx, entity);
    }
    bool worldRemoveRigidBody(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return removeComponent<RigidBodyComponent>(ctx, entity);
    }
    ScriptCameraRef worldAddCamera(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return addComponent<CameraComponent, ScriptCameraRef>(ctx, entity);
    }
    bool worldRemoveCamera(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return removeComponent<CameraComponent>(ctx, entity);
    }
    ScriptLightRef worldAddLight(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return addComponent<LightComponent, ScriptLightRef>(ctx, entity);
    }
    bool worldRemoveLight(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return removeComponent<LightComponent>(ctx, entity);
    }
    ScriptMeshRef worldAddMesh(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return addComponent<MeshComponent, ScriptMeshRef>(ctx, entity);
    }
    bool worldRemoveMesh(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return removeComponent<MeshComponent>(ctx, entity);
    }
    ScriptBoxShapeRef worldAddBoxShape(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return addComponent<BoxShapeComponent, ScriptBoxShapeRef>(ctx, entity);
    }
    bool worldRemoveBoxShape(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return removeComponent<BoxShapeComponent>(ctx, entity);
    }
    ScriptSphereShapeRef worldAddSphereShape(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return addComponent<SphereShapeComponent, ScriptSphereShapeRef>(ctx, entity);
    }
    bool worldRemoveSphereShape(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return removeComponent<SphereShapeComponent>(ctx, entity);
    }
    ScriptAnimatorRef worldAddAnimator(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return addComponent<AnimatorComponent, ScriptAnimatorRef>(ctx, entity);
    }
    bool worldRemoveAnimator(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return removeComponent<AnimatorComponent>(ctx, entity);
    }

    std::uint32_t meshGetBuiltinGeometry(ScriptContext& ctx, const ScriptMeshRef& self)
    {
        return requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent").builtinGeometry;
    }
    void meshSetBuiltinGeometry(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t value)
    {
        requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent").builtinGeometry = value;
    }
    void meshSetMaterial(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot, const std::string& uri)
    {
        auto& mesh            = requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent");
        auto& slotOverride    = materialSlotOverride(mesh, slot);
        slotOverride.material = uri;
        slotOverride.materialGraph.clear();
        removeEmptyMaterialSlotOverride(mesh, slot);
    }
    void meshSetMaterialFloat(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot,
                              const std::string& name, float value)
    {
        if (name.empty())
            return;
        auto& entry = materialProperty(
            materialSlotOverride(requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent"), slot), name);
        entry.type       = MaterialPropertyBlockValueType::eFloat;
        entry.floatValue = value;
    }
    void meshSetMaterialColor(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot,
                              const std::string& name, const ScriptVec4& value)
    {
        if (name.empty())
            return;
        auto& entry = materialProperty(
            materialSlotOverride(requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent"), slot), name);
        entry.type       = MaterialPropertyBlockValueType::eColor;
        entry.colorValue = toGlmVec4(value);
    }
    void meshSetMaterialTexture(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot,
                                const std::string& name, const std::string& uri)
    {
        if (name.empty())
            return;
        auto& entry = materialProperty(
            materialSlotOverride(requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent"), slot), name);
        entry.type       = MaterialPropertyBlockValueType::eTexture2D;
        entry.textureUri = uri;
    }
    void meshClearMaterialProperty(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot,
                                   const std::string& name)
    {
        auto&      mesh = requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent");
        const auto it   = std::find_if(mesh.materialOverrides.begin(), mesh.materialOverrides.end(),
                                       [slot](const MaterialSlotOverride& v) { return v.slot == slot; });
        if (it == mesh.materialOverrides.end())
            return;
        std::erase_if(it->properties, [&name](const MaterialPropertyBlockEntry& v) { return v.name == name; });
        removeEmptyMaterialSlotOverride(mesh, slot);
    }
    void meshClearMaterialProperties(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot)
    {
        auto&      mesh = requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent");
        const auto it   = std::find_if(mesh.materialOverrides.begin(), mesh.materialOverrides.end(),
                                       [slot](const MaterialSlotOverride& v) { return v.slot == slot; });
        if (it == mesh.materialOverrides.end())
            return;
        it->properties.clear();
        removeEmptyMaterialSlotOverride(mesh, slot);
    }

    void worldRegisterLayerTable(sol::state& lua, ScriptContext& ctx)
    {
        (void)ctx;
        auto layer       = script_binding::getOrCreateTable(lua, "Layer");
        layer["Default"] = kRenderLayerDefaultMask;
        layer["UI"]      = kRenderLayerUiMask;
        layer["All"]     = kRenderLayerAllMask;
    }
} // namespace vultra
