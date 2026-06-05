#include "vultra/function/scripting/bindings/script_world_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/world/components/animator_component.hpp"
#include "vultra/function/world/components/box_shape_component.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
#include "vultra/function/world/components/sphere_shape_component.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/vec4.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        ScriptEntity makeEntity(entt::entity entity) { return ScriptEntity {entity}; }

        entt::entity entityValue(const ScriptEntity& entity) { return entity.value; }

        ScriptVec3 toScriptVec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }
        ScriptVec4 toScriptVec4(const glm::vec4& v) { return {v.x, v.y, v.z, v.w}; }
        glm::vec3  toGlmVec3(const ScriptVec3& v) { return {v.x, v.y, v.z}; }
        glm::vec4  toGlmVec4(const ScriptVec4& v) { return {v.x, v.y, v.z, v.w}; }

        NameComponent& ensureName(World& world, entt::entity entity)
        {
            auto& reg = world.registry();
            if (auto* name = reg.try_get<NameComponent>(entity))
                return *name;
            return reg.emplace<NameComponent>(entity);
        }

        ScriptEntity createEntity(ScriptContext& ctx, sol::optional<std::string> name)
        {
            auto* world = ctx.world();
            if (!world)
                return {};

            const auto entity = world->createEntity();
            if (name && !name->empty())
                ensureName(*world, entity).name = *name;
            return makeEntity(entity);
        }

        ScriptEntity findByName(ScriptContext& ctx, const std::string& name)
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

        bool hasPrefix(const std::string& value, const std::string& prefix)
        {
            return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
        }

        sol::table findByNamePrefix(sol::this_state luaState, ScriptContext& ctx, const std::string& prefix)
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

        sol::table allEntities(sol::this_state luaState, ScriptContext& ctx)
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

        void destroyEntity(ScriptContext& ctx, const ScriptEntity& entity)
        {
            auto* world = ctx.world();
            if (world && ctx.isValid(entityValue(entity)))
                world->destroyRecursive(entityValue(entity));
        }

        CameraComponent& requireCamera(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* camera = world->registry().try_get<CameraComponent>(entity);
            if (!camera)
                throw std::runtime_error("Entity has no CameraComponent");
            return *camera;
        }

        template<typename Component>
        Component& requireComponent(ScriptContext& ctx, entt::entity entity, const char* componentName)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* component = world->registry().try_get<Component>(entity);
            if (!component)
                throw std::runtime_error(std::string("Entity has no ") + componentName);
            return *component;
        }

        MaterialSlotOverride& materialSlotOverride(MeshComponent& mesh, const uint32_t slot)
        {
            const auto it = std::find_if(mesh.materialOverrides.begin(),
                                         mesh.materialOverrides.end(),
                                         [slot](const MaterialSlotOverride& value) { return value.slot == slot; });
            if (it != mesh.materialOverrides.end())
                return *it;
            auto& value = mesh.materialOverrides.emplace_back();
            value.slot  = slot;
            return value;
        }

        MaterialPropertyBlockEntry& materialProperty(MaterialSlotOverride& slotOverride, const std::string& name)
        {
            const auto it = std::find_if(slotOverride.properties.begin(),
                                         slotOverride.properties.end(),
                                         [&name](const MaterialPropertyBlockEntry& value) { return value.name == name; });
            if (it != slotOverride.properties.end())
                return *it;
            auto& value = slotOverride.properties.emplace_back();
            value.name  = name;
            return value;
        }

        void removeEmptyMaterialSlotOverride(MeshComponent& mesh, const uint32_t slot)
        {
            std::erase_if(mesh.materialOverrides, [slot](const MaterialSlotOverride& value) {
                return value.slot == slot && value.material.empty() && value.materialGraph.empty() &&
                       value.properties.empty();
            });
        }

        void setMeshMaterial(ScriptContext& ctx, const ScriptMeshRef& self, const uint32_t slot, const std::string& uri)
        {
            auto& mesh          = requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent");
            auto& slotOverride  = materialSlotOverride(mesh, slot);
            slotOverride.material = uri;
            slotOverride.materialGraph.clear();
            removeEmptyMaterialSlotOverride(mesh, slot);
        }

        void setMeshMaterialFloat(ScriptContext& ctx,
                                  const ScriptMeshRef& self,
                                  const uint32_t slot,
                                  const std::string& name,
                                  const float value)
        {
            if (name.empty())
                return;
            auto& entry      = materialProperty(materialSlotOverride(requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent"), slot), name);
            entry.type       = MaterialPropertyBlockValueType::eFloat;
            entry.floatValue = value;
        }

        void setMeshMaterialColor(ScriptContext& ctx,
                                  const ScriptMeshRef& self,
                                  const uint32_t slot,
                                  const std::string& name,
                                  const ScriptVec4& value)
        {
            if (name.empty())
                return;
            auto& entry      = materialProperty(materialSlotOverride(requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent"), slot), name);
            entry.type       = MaterialPropertyBlockValueType::eColor;
            entry.colorValue = toGlmVec4(value);
        }

        void setMeshMaterialTexture(ScriptContext& ctx,
                                    const ScriptMeshRef& self,
                                    const uint32_t slot,
                                    const std::string& name,
                                    const std::string& uri)
        {
            if (name.empty())
                return;
            auto& entry     = materialProperty(materialSlotOverride(requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent"), slot), name);
            entry.type      = MaterialPropertyBlockValueType::eTexture2D;
            entry.textureUri = uri;
        }

        void clearMeshMaterialProperty(ScriptContext& ctx,
                                       const ScriptMeshRef& self,
                                       const uint32_t slot,
                                       const std::string& name)
        {
            auto& mesh = requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent");
            const auto it = std::find_if(mesh.materialOverrides.begin(),
                                         mesh.materialOverrides.end(),
                                         [slot](const MaterialSlotOverride& value) { return value.slot == slot; });
            if (it == mesh.materialOverrides.end())
                return;
            std::erase_if(it->properties,
                          [&name](const MaterialPropertyBlockEntry& value) { return value.name == name; });
            removeEmptyMaterialSlotOverride(mesh, slot);
        }

        void clearMeshMaterialProperties(ScriptContext& ctx, const ScriptMeshRef& self, const uint32_t slot)
        {
            auto& mesh = requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent");
            const auto it = std::find_if(mesh.materialOverrides.begin(),
                                         mesh.materialOverrides.end(),
                                         [slot](const MaterialSlotOverride& value) { return value.slot == slot; });
            if (it == mesh.materialOverrides.end())
                return;
            it->properties.clear();
            removeEmptyMaterialSlotOverride(mesh, slot);
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

    void registerScriptWorldBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptCameraRef>(
            "CameraRef",
            "valid",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptCameraRef& self) {
                auto* world = ctx.world();
                return world && world->registry().all_of<CameraComponent>(self.entity);
            }),
            "primary",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptCameraRef& self) { return requireCamera(ctx, self.entity).primary; },
                [&ctx](const ScriptCameraRef& self, bool value) { requireCamera(ctx, self.entity).primary = value; }),
            "projection",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptCameraRef& self) { return requireCamera(ctx, self.entity).projection; },
                [&ctx](const ScriptCameraRef& self, uint32_t value) { requireCamera(ctx, self.entity).projection = value; }),
            "fovYDegrees",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptCameraRef& self) { return requireCamera(ctx, self.entity).fovYDegrees; },
                [&ctx](const ScriptCameraRef& self, float value) { requireCamera(ctx, self.entity).fovYDegrees = value; }),
            "orthographicHeight",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptCameraRef& self) { return requireCamera(ctx, self.entity).orthographicHeight; },
                [&ctx](const ScriptCameraRef& self, float value) {
                    requireCamera(ctx, self.entity).orthographicHeight = value;
                }),
            "cullingMask",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptCameraRef& self) { return requireCamera(ctx, self.entity).cullingMask; },
                [&ctx](const ScriptCameraRef& self, uint32_t value) {
                    requireCamera(ctx, self.entity).cullingMask = value;
                }),
            "rendererKey",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptCameraRef& self) { return requireCamera(ctx, self.entity).rendererKey; },
                [&ctx](const ScriptCameraRef& self, const std::string& value) {
                    requireCamera(ctx, self.entity).rendererKey = value;
                }));

        lua.new_usertype<ScriptLightRef>(
            "Light",
            "valid",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptLightRef& self) {
                auto* world = ctx.world();
                return world && world->registry().all_of<LightComponent>(self.entity);
            }),
            "kind",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptLightRef& self) {
                    return requireComponent<LightComponent>(ctx, self.entity, "LightComponent").kind;
                },
                [&ctx](const ScriptLightRef& self, uint32_t value) {
                    requireComponent<LightComponent>(ctx, self.entity, "LightComponent").kind = value;
                }),
            "color",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptLightRef& self) {
                    return toScriptVec3(requireComponent<LightComponent>(ctx, self.entity, "LightComponent").color);
                },
                [&ctx](const ScriptLightRef& self, const ScriptVec3& value) {
                    requireComponent<LightComponent>(ctx, self.entity, "LightComponent").color = toGlmVec3(value);
                }),
            "intensity",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptLightRef& self) {
                    return requireComponent<LightComponent>(ctx, self.entity, "LightComponent").intensity;
                },
                [&ctx](const ScriptLightRef& self, float value) {
                    requireComponent<LightComponent>(ctx, self.entity, "LightComponent").intensity = value;
                }),
            "range",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptLightRef& self) {
                    return requireComponent<LightComponent>(ctx, self.entity, "LightComponent").range;
                },
                [&ctx](const ScriptLightRef& self, float value) {
                    requireComponent<LightComponent>(ctx, self.entity, "LightComponent").range = value;
                }),
            "castsShadow",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptLightRef& self) {
                    return requireComponent<LightComponent>(ctx, self.entity, "LightComponent").castsShadow;
                },
                [&ctx](const ScriptLightRef& self, bool value) {
                    requireComponent<LightComponent>(ctx, self.entity, "LightComponent").castsShadow = value;
                }));

        lua.new_usertype<ScriptMeshRef>(
            "Mesh",
            "valid",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptMeshRef& self) {
                auto* world = ctx.world();
                return world && world->registry().all_of<MeshComponent>(self.entity);
            }),
            "builtinGeometry",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptMeshRef& self) {
                    return requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent").builtinGeometry;
                },
                [&ctx](const ScriptMeshRef& self, uint32_t value) {
                    requireComponent<MeshComponent>(ctx, self.entity, "MeshComponent").builtinGeometry = value;
                }),
            "setMaterial",
            [&ctx](const ScriptMeshRef& self, uint32_t slot, const std::string& uri) {
                setMeshMaterial(ctx, self, slot, uri);
            },
            "setMaterialFloat",
            [&ctx](const ScriptMeshRef& self, uint32_t slot, const std::string& name, float value) {
                setMeshMaterialFloat(ctx, self, slot, name, value);
            },
            "setMaterialColor",
            [&ctx](const ScriptMeshRef& self, uint32_t slot, const std::string& name, const ScriptVec4& value) {
                setMeshMaterialColor(ctx, self, slot, name, value);
            },
            "setMaterialTexture",
            [&ctx](const ScriptMeshRef& self, uint32_t slot, const std::string& name, const std::string& uri) {
                setMeshMaterialTexture(ctx, self, slot, name, uri);
            },
            "clearMaterialProperty",
            [&ctx](const ScriptMeshRef& self, uint32_t slot, const std::string& name) {
                clearMeshMaterialProperty(ctx, self, slot, name);
            },
            "clearMaterialProperties",
            [&ctx](const ScriptMeshRef& self, uint32_t slot) { clearMeshMaterialProperties(ctx, self, slot); });

        lua.new_usertype<ScriptBoxShapeRef>(
            "BoxShape",
            "valid",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptBoxShapeRef& self) {
                auto* world = ctx.world();
                return world && world->registry().all_of<BoxShapeComponent>(self.entity);
            }),
            "halfExtents",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptBoxShapeRef& self) {
                    return toScriptVec3(requireComponent<BoxShapeComponent>(ctx, self.entity, "BoxShapeComponent").halfExtents);
                },
                [&ctx](const ScriptBoxShapeRef& self, const ScriptVec3& value) {
                    requireComponent<BoxShapeComponent>(ctx, self.entity, "BoxShapeComponent").halfExtents = toGlmVec3(value);
                }));

        lua.new_usertype<ScriptSphereShapeRef>(
            "SphereShape",
            "valid",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptSphereShapeRef& self) {
                auto* world = ctx.world();
                return world && world->registry().all_of<SphereShapeComponent>(self.entity);
            }),
            "radius",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptSphereShapeRef& self) {
                    return requireComponent<SphereShapeComponent>(ctx, self.entity, "SphereShapeComponent").radius;
                },
                [&ctx](const ScriptSphereShapeRef& self, float value) {
                    requireComponent<SphereShapeComponent>(ctx, self.entity, "SphereShapeComponent").radius = value;
                }));

        auto world = script_binding::getOrCreateTable(lua, "World");
        world.set_function("create", [&ctx](sol::optional<std::string> name) { return createEntity(ctx, name); });
        world.set_function("destroy", [&ctx](const ScriptEntity& entity) { destroyEntity(ctx, entity); });
        world.set_function("count", [&ctx]() {
            auto* w = ctx.world();
            if (!w)
                return 0u;

            uint32_t count = 0;
            for (auto entity : w->registry().view<IDComponent>())
            {
                (void)entity;
                ++count;
            }
            return count;
        });
        world.set_function("findByName", [&ctx](const std::string& name) { return findByName(ctx, name); });
        world.set_function("findByNamePrefix", [&ctx](sol::this_state luaState, const std::string& prefix) {
            return findByNamePrefix(luaState, ctx, prefix);
        });
        world.set_function("entities", [&ctx](sol::this_state luaState) { return allEntities(luaState, ctx); });
        world.set_function("addRigidBody", [&ctx](const ScriptEntity& entity) {
            return addComponent<RigidBodyComponent, ScriptRigidBodyRef>(ctx, entity);
        });
        world.set_function("removeRigidBody",
                           [&ctx](const ScriptEntity& entity) { return removeComponent<RigidBodyComponent>(ctx, entity); });
        world.set_function("addCamera",
                           [&ctx](const ScriptEntity& entity) { return addComponent<CameraComponent, ScriptCameraRef>(ctx, entity); });
        world.set_function("removeCamera",
                           [&ctx](const ScriptEntity& entity) { return removeComponent<CameraComponent>(ctx, entity); });
        world.set_function("addLight",
                           [&ctx](const ScriptEntity& entity) { return addComponent<LightComponent, ScriptLightRef>(ctx, entity); });
        world.set_function("removeLight",
                           [&ctx](const ScriptEntity& entity) { return removeComponent<LightComponent>(ctx, entity); });
        world.set_function("addMesh",
                           [&ctx](const ScriptEntity& entity) { return addComponent<MeshComponent, ScriptMeshRef>(ctx, entity); });
        world.set_function("removeMesh",
                           [&ctx](const ScriptEntity& entity) { return removeComponent<MeshComponent>(ctx, entity); });
        world.set_function("addBoxShape", [&ctx](const ScriptEntity& entity) {
            return addComponent<BoxShapeComponent, ScriptBoxShapeRef>(ctx, entity);
        });
        world.set_function("removeBoxShape",
                           [&ctx](const ScriptEntity& entity) { return removeComponent<BoxShapeComponent>(ctx, entity); });
        world.set_function("addSphereShape", [&ctx](const ScriptEntity& entity) {
            return addComponent<SphereShapeComponent, ScriptSphereShapeRef>(ctx, entity);
        });
        world.set_function("removeSphereShape",
                           [&ctx](const ScriptEntity& entity) { return removeComponent<SphereShapeComponent>(ctx, entity); });
        world.set_function("addAnimator", [&ctx](const ScriptEntity& entity) {
            return addComponent<AnimatorComponent, ScriptAnimatorRef>(ctx, entity);
        });
        world.set_function("removeAnimator",
                           [&ctx](const ScriptEntity& entity) { return removeComponent<AnimatorComponent>(ctx, entity); });

        auto camera = script_binding::getOrCreateTable(lua, "Camera");
        camera.set_function("findPrimary", [&ctx]() {
            auto* world = ctx.world();
            if (!world)
                return ScriptEntity {};

            auto view = world->registry().view<CameraComponent>();
            for (auto entity : view)
            {
                if (view.get<CameraComponent>(entity).primary)
                    return ScriptEntity {entity};
            }
            return ScriptEntity {};
        });

        auto layer = script_binding::getOrCreateTable(lua, "Layer");
        layer["Default"] = kRenderLayerDefaultMask;
        layer["UI"]      = kRenderLayerUiMask;
        layer["All"]     = kRenderLayerAllMask;
    }
} // namespace vultra
