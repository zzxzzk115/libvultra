#include "vultra/function/scripting/bindings/script_physics_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/physics_service.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/vec3.hpp>
#include <sol/sol.hpp>

#include <optional>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        ScriptVec3 toScriptVec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }

        glm::vec3 toGlmVec3(const ScriptVec3& v) { return {v.x, v.y, v.z}; }
        ScriptEntity makeEntity(entt::entity entity) { return ScriptEntity {entity}; }

        ScriptPhysicsRaycastHit toScript(const std::optional<PhysicsRaycastHit>& hit)
        {
            if (!hit)
                return {};
            return ScriptPhysicsRaycastHit {
                .hit      = true,
                .entity   = makeEntity(hit->entity),
                .point    = toScriptVec3(hit->point),
                .normal   = toScriptVec3(hit->normal),
                .fraction = hit->fraction,
                .distance = hit->distance,
            };
        }

        RigidBodyComponent& requireRigidBody(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* rb = world->registry().try_get<RigidBodyComponent>(entity);
            if (!rb)
                throw std::runtime_error("Entity has no RigidBodyComponent");
            return *rb;
        }

        sol::table overlapSphere(sol::this_state luaState,
                                 ScriptContext& ctx,
                                 const ScriptVec3& center,
                                 float radius,
                                 sol::optional<bool> activeOnly)
        {
            sol::state_view lua(luaState);
            auto result = lua.create_table();
            if (!ctx.physicsService || radius < 0.0f)
                return result;

            const bool requireActive = activeOnly.value_or(true);
            uint32_t index = 1;
            for (auto entity : ctx.physicsService->overlapSphere(toGlmVec3(center), radius, requireActive))
                result[index++] = makeEntity(entity);

            return result;
        }
    } // namespace

    void registerScriptPhysicsBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptPhysicsRaycastHit>("PhysicsRaycastHit",
                                                  "hit",
                                                  &ScriptPhysicsRaycastHit::hit,
                                                  "entity",
                                                  &ScriptPhysicsRaycastHit::entity,
                                                  "point",
                                                  &ScriptPhysicsRaycastHit::point,
                                                  "normal",
                                                  &ScriptPhysicsRaycastHit::normal,
                                                  "fraction",
                                                  &ScriptPhysicsRaycastHit::fraction,
                                                  "distance",
                                                  &ScriptPhysicsRaycastHit::distance);
        lua.new_usertype<ScriptPhysicsContactPair>("PhysicsContactPair",
                                                   "a",
                                                   &ScriptPhysicsContactPair::a,
                                                   "b",
                                                   &ScriptPhysicsContactPair::b);

        lua.new_usertype<ScriptRigidBodyRef>(
            "RigidBody",
            "valid",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptRigidBodyRef& self) {
                auto* world = ctx.world();
                return world && world->registry().all_of<RigidBodyComponent>(self.entity);
            }),
            "linearVelocity",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) {
                    return ctx.physicsService ? toScriptVec3(ctx.physicsService->linearVelocity(self.entity)) :
                                                toScriptVec3(requireRigidBody(ctx, self.entity).linearVelocity);
                },
                [&ctx](const ScriptRigidBodyRef& self, const ScriptVec3& value) {
                    const auto velocity = toGlmVec3(value);
                    if (ctx.physicsService && ctx.physicsService->setLinearVelocity(self.entity, velocity))
                        return;
                    requireRigidBody(ctx, self.entity).linearVelocity = velocity;
                }),
            "angularVelocity",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) {
                    return ctx.physicsService ? toScriptVec3(ctx.physicsService->angularVelocity(self.entity)) :
                                                toScriptVec3(requireRigidBody(ctx, self.entity).angularVelocity);
                },
                [&ctx](const ScriptRigidBodyRef& self, const ScriptVec3& value) {
                    const auto velocity = toGlmVec3(value);
                    if (ctx.physicsService && ctx.physicsService->setAngularVelocity(self.entity, velocity))
                        return;
                    requireRigidBody(ctx, self.entity).angularVelocity = velocity;
                }),
            "motionType",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).motionType; },
                [&ctx](const ScriptRigidBodyRef& self, uint32_t value) {
                    requireRigidBody(ctx, self.entity).motionType = value;
                }),
            "objectLayer",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).objectLayer; },
                [&ctx](const ScriptRigidBodyRef& self, uint32_t value) {
                    requireRigidBody(ctx, self.entity).objectLayer = value;
                }),
            "isSensor",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).isSensor; },
                [&ctx](const ScriptRigidBodyRef& self, bool value) {
                    requireRigidBody(ctx, self.entity).isSensor = value;
                }),
            "motionQuality",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).motionQuality; },
                [&ctx](const ScriptRigidBodyRef& self, uint32_t value) {
                    requireRigidBody(ctx, self.entity).motionQuality = value;
                }),
            "allowSleeping",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).allowSleeping; },
                [&ctx](const ScriptRigidBodyRef& self, bool value) {
                    requireRigidBody(ctx, self.entity).allowSleeping = value;
                }),
            "mass",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).mass; },
                [&ctx](const ScriptRigidBodyRef& self, float value) { requireRigidBody(ctx, self.entity).mass = value; }),
            "overrideMass",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).overrideMass; },
                [&ctx](const ScriptRigidBodyRef& self, bool value) {
                    requireRigidBody(ctx, self.entity).overrideMass = value;
                }),
            "friction",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).friction; },
                [&ctx](const ScriptRigidBodyRef& self, float value) {
                    requireRigidBody(ctx, self.entity).friction = value;
                }),
            "restitution",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).restitution; },
                [&ctx](const ScriptRigidBodyRef& self, float value) {
                    requireRigidBody(ctx, self.entity).restitution = value;
                }),
            "linearDamping",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).linearDamping; },
                [&ctx](const ScriptRigidBodyRef& self, float value) {
                    requireRigidBody(ctx, self.entity).linearDamping = value;
                }),
            "angularDamping",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).angularDamping; },
                [&ctx](const ScriptRigidBodyRef& self, float value) {
                    requireRigidBody(ctx, self.entity).angularDamping = value;
                }),
            "gravityFactor",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).gravityFactor; },
                [&ctx](const ScriptRigidBodyRef& self, float value) {
                    requireRigidBody(ctx, self.entity).gravityFactor = value;
                }),
            "maxLinearVelocity",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).maxLinearVelocity; },
                [&ctx](const ScriptRigidBodyRef& self, float value) {
                    requireRigidBody(ctx, self.entity).maxLinearVelocity = value;
                }),
            "maxAngularVelocity",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRigidBodyRef& self) { return requireRigidBody(ctx, self.entity).maxAngularVelocity; },
                [&ctx](const ScriptRigidBodyRef& self, float value) {
                    requireRigidBody(ctx, self.entity).maxAngularVelocity = value;
                }),
            "activate",
            [&ctx](const ScriptRigidBodyRef& self) {
                return ctx.physicsService ? ctx.physicsService->activate(self.entity) : false;
            },
            "addForce",
            [&ctx](const ScriptRigidBodyRef& self, const ScriptVec3& force) {
                return ctx.physicsService ? ctx.physicsService->addForce(self.entity, toGlmVec3(force)) : false;
            },
            "addImpulse",
            [&ctx](const ScriptRigidBodyRef& self, const ScriptVec3& impulse) {
                return ctx.physicsService ? ctx.physicsService->addImpulse(self.entity, toGlmVec3(impulse)) : false;
            },
            "setPosition",
            [&ctx](const ScriptRigidBodyRef& self, const ScriptVec3& position, sol::optional<bool> activate) {
                return ctx.physicsService ?
                           ctx.physicsService->setPosition(self.entity, toGlmVec3(position), activate.value_or(true)) :
                           false;
            });

        auto physics = script_binding::getOrCreateTable(lua, "Physics");
        physics.set_function("enabled", [&ctx]() { return ctx.physicsService ? ctx.physicsService->enabled() : false; });
        physics.set_function("setEnabled", [&ctx](bool enabled) {
            if (ctx.physicsService)
                ctx.physicsService->setEnabled(enabled);
        });
        physics.set_function("bodyCount", [&ctx]() {
            return ctx.physicsService ? ctx.physicsService->bodyCount() : 0u;
        });
        physics.set_function("hasBody", [&ctx](const ScriptEntity& entity) {
            return ctx.physicsService ? ctx.physicsService->hasBody(entity.value) : false;
        });
        physics.set_function("fixedTimeStep", [&ctx]() {
            return ctx.physicsService ? ctx.physicsService->fixedTimeStep() : 0.0f;
        });
        physics.set_function("setFixedTimeStep", [&ctx](float seconds) {
            if (ctx.physicsService)
                ctx.physicsService->setFixedTimeStep(seconds);
        });
        physics.set_function("addForce", [&ctx](const ScriptEntity& entity, const ScriptVec3& force) {
            return ctx.physicsService ? ctx.physicsService->addForce(entity.value, toGlmVec3(force)) : false;
        });
        physics.set_function("addImpulse", [&ctx](const ScriptEntity& entity, const ScriptVec3& impulse) {
            return ctx.physicsService ? ctx.physicsService->addImpulse(entity.value, toGlmVec3(impulse)) : false;
        });
        physics.set_function("setPosition", [&ctx](const ScriptEntity& entity,
                                                   const ScriptVec3& position,
                                                   sol::optional<bool> activate) {
            return ctx.physicsService ?
                       ctx.physicsService->setPosition(entity.value, toGlmVec3(position), activate.value_or(true)) :
                       false;
        });
        physics.set_function("raycast", [&ctx](const ScriptVec3& origin,
                                               const ScriptVec3& direction,
                                               float maxDistance,
                                               sol::optional<bool> activeOnly) {
            return ctx.physicsService ?
                       toScript(ctx.physicsService->raycast(
                           toGlmVec3(origin), toGlmVec3(direction), maxDistance, activeOnly.value_or(true))) :
                       ScriptPhysicsRaycastHit {};
        });
        physics.set_function("overlapSphere", [&ctx](sol::this_state luaState,
                                                      const ScriptVec3& center,
                                                      float radius,
                                                      sol::optional<bool> activeOnly) {
            return overlapSphere(luaState, ctx, center, radius, activeOnly);
        });
        physics.set_function("overlapBox", [&ctx](sol::this_state luaState,
                                                  const ScriptVec3& center,
                                                  const ScriptVec3& halfExtents,
                                                  sol::optional<bool> activeOnly) {
            sol::state_view lua(luaState);
            auto result = lua.create_table();
            if (!ctx.physicsService)
                return result;
            uint32_t index = 1;
            for (auto entity :
                 ctx.physicsService->overlapBox(toGlmVec3(center), toGlmVec3(halfExtents), activeOnly.value_or(true)))
                result[index++] = makeEntity(entity);
            return result;
        });
        physics.set_function("contactPairs", [&ctx](sol::this_state luaState, sol::optional<bool> activeOnly) {
            sol::state_view lua(luaState);
            auto result = lua.create_table();
            if (!ctx.physicsService)
                return result;
            uint32_t index = 1;
            for (const auto& pair : ctx.physicsService->contactPairs(activeOnly.value_or(true)))
                result[index++] = ScriptPhysicsContactPair {.a = makeEntity(pair.a), .b = makeEntity(pair.b)};
            return result;
        });
    }
} // namespace vultra
