#include "vultra/function/scripting/bindings/script_physics_shim.hpp"

#include "vultra/function/services/physics_service.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/vec3.hpp>

#include <optional>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        ScriptVec3   toScriptVec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }
        glm::vec3    toGlmVec3(const ScriptVec3& v) { return {v.x, v.y, v.z}; }
        ScriptEntity makeEntity(entt::entity entity) { return ScriptEntity {entity}; }

        ScriptPhysicsRaycastHit toScript(const std::optional<PhysicsRaycastHit>& hit)
        {
            if (!hit)
                return {};
            return ScriptPhysicsRaycastHit {.hit      = true,
                                            .entity   = makeEntity(hit->entity),
                                            .point    = toScriptVec3(hit->point),
                                            .normal   = toScriptVec3(hit->normal),
                                            .fraction = hit->fraction,
                                            .distance = hit->distance};
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

        PhysicsQueryFilter makeFilter(sol::optional<bool> activeOnly, sol::optional<std::uint32_t> layerMask)
        {
            PhysicsQueryFilter filter {};
            filter.activeOnly = activeOnly.value_or(true);
            filter.layerMask  = layerMask.value_or(0xFFFFFFFFu);
            return filter;
        }
    } // namespace

    // --- RigidBody velocity (service-or-component fallback) ---
    ScriptVec3 rbGetLinearVelocity(ScriptContext& ctx, const ScriptRigidBodyRef& self)
    {
        return ctx.physicsService ? toScriptVec3(ctx.physicsService->linearVelocity(self.entity)) :
                                    toScriptVec3(requireRigidBody(ctx, self.entity).linearVelocity);
    }
    void rbSetLinearVelocity(ScriptContext& ctx, const ScriptRigidBodyRef& self, ScriptVec3 value)
    {
        const auto velocity = toGlmVec3(value);
        if (ctx.physicsService && ctx.physicsService->setLinearVelocity(self.entity, velocity))
            return;
        requireRigidBody(ctx, self.entity).linearVelocity = velocity;
    }
    ScriptVec3 rbGetAngularVelocity(ScriptContext& ctx, const ScriptRigidBodyRef& self)
    {
        return ctx.physicsService ? toScriptVec3(ctx.physicsService->angularVelocity(self.entity)) :
                                    toScriptVec3(requireRigidBody(ctx, self.entity).angularVelocity);
    }
    void rbSetAngularVelocity(ScriptContext& ctx, const ScriptRigidBodyRef& self, ScriptVec3 value)
    {
        const auto velocity = toGlmVec3(value);
        if (ctx.physicsService && ctx.physicsService->setAngularVelocity(self.entity, velocity))
            return;
        requireRigidBody(ctx, self.entity).angularVelocity = velocity;
    }

    // --- RigidBody uniform component-field properties ---
#define RB_FIELD(getter, setter, ctype, field) \
    ctype getter(ScriptContext& ctx, const ScriptRigidBodyRef& self) \
    { \
        return requireRigidBody(ctx, self.entity).field; \
    } \
    void setter(ScriptContext& ctx, const ScriptRigidBodyRef& self, ctype value) \
    { \
        requireRigidBody(ctx, self.entity).field = value; \
    }
    RB_FIELD(rbGetMotionType, rbSetMotionType, std::uint32_t, motionType)
    RB_FIELD(rbGetObjectLayer, rbSetObjectLayer, std::uint32_t, objectLayer)
    RB_FIELD(rbGetIsSensor, rbSetIsSensor, bool, isSensor)
    RB_FIELD(rbGetMotionQuality, rbSetMotionQuality, std::uint32_t, motionQuality)
    RB_FIELD(rbGetAllowSleeping, rbSetAllowSleeping, bool, allowSleeping)
    RB_FIELD(rbGetMass, rbSetMass, float, mass)
    RB_FIELD(rbGetOverrideMass, rbSetOverrideMass, bool, overrideMass)
    RB_FIELD(rbGetFriction, rbSetFriction, float, friction)
    RB_FIELD(rbGetRestitution, rbSetRestitution, float, restitution)
    RB_FIELD(rbGetLinearDamping, rbSetLinearDamping, float, linearDamping)
    RB_FIELD(rbGetAngularDamping, rbSetAngularDamping, float, angularDamping)
    RB_FIELD(rbGetGravityFactor, rbSetGravityFactor, float, gravityFactor)
    RB_FIELD(rbGetMaxLinearVelocity, rbSetMaxLinearVelocity, float, maxLinearVelocity)
    RB_FIELD(rbGetMaxAngularVelocity, rbSetMaxAngularVelocity, float, maxAngularVelocity)
#undef RB_FIELD

    bool rbActivate(ScriptContext& ctx, const ScriptRigidBodyRef& self)
    {
        return ctx.physicsService ? ctx.physicsService->activate(self.entity) : false;
    }
    bool rbAddForce(ScriptContext& ctx, const ScriptRigidBodyRef& self, const ScriptVec3& force)
    {
        return ctx.physicsService ? ctx.physicsService->addForce(self.entity, toGlmVec3(force)) : false;
    }
    bool rbAddImpulse(ScriptContext& ctx, const ScriptRigidBodyRef& self, const ScriptVec3& impulse)
    {
        return ctx.physicsService ? ctx.physicsService->addImpulse(self.entity, toGlmVec3(impulse)) : false;
    }
    bool rbSetPosition(ScriptContext& ctx, const ScriptRigidBodyRef& self, const ScriptVec3& position,
                       sol::optional<bool> activate)
    {
        return ctx.physicsService ?
                   ctx.physicsService->setPosition(self.entity, toGlmVec3(position), activate.value_or(true)) :
                   false;
    }

    // --- Physics namespace ---
    bool physicsEnabled(ScriptContext& ctx) { return ctx.physicsService ? ctx.physicsService->enabled() : false; }
    void physicsSetEnabled(ScriptContext& ctx, bool enabled)
    {
        if (ctx.physicsService)
            ctx.physicsService->setEnabled(enabled);
    }
    std::uint32_t physicsBodyCount(ScriptContext& ctx) { return ctx.physicsService ? ctx.physicsService->bodyCount() : 0u; }
    bool physicsHasBody(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return ctx.physicsService ? ctx.physicsService->hasBody(entity.value) : false;
    }
    float physicsFixedTimeStep(ScriptContext& ctx)
    {
        return ctx.physicsService ? ctx.physicsService->fixedTimeStep() : 0.0f;
    }
    void physicsSetFixedTimeStep(ScriptContext& ctx, float seconds)
    {
        if (ctx.physicsService)
            ctx.physicsService->setFixedTimeStep(seconds);
    }
    bool physicsAddForce(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& force)
    {
        return ctx.physicsService ? ctx.physicsService->addForce(entity.value, toGlmVec3(force)) : false;
    }
    bool physicsAddImpulse(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& impulse)
    {
        return ctx.physicsService ? ctx.physicsService->addImpulse(entity.value, toGlmVec3(impulse)) : false;
    }
    bool physicsSetPosition(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& position,
                            sol::optional<bool> activate)
    {
        return ctx.physicsService ?
                   ctx.physicsService->setPosition(entity.value, toGlmVec3(position), activate.value_or(true)) :
                   false;
    }
    ScriptPhysicsRaycastHit physicsRaycast(ScriptContext& ctx, const ScriptVec3& origin, const ScriptVec3& direction,
                                           float maxDistance, sol::optional<bool> activeOnly,
                                           sol::optional<std::uint32_t> layerMask)
    {
        if (!ctx.physicsService)
            return {};
        return toScript(ctx.physicsService->raycast(toGlmVec3(origin), toGlmVec3(direction), maxDistance,
                                                    makeFilter(activeOnly, layerMask)));
    }
    sol::table physicsRaycastAll(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& origin,
                                 const ScriptVec3& direction, float maxDistance, sol::optional<bool> activeOnly,
                                 sol::optional<std::uint32_t> layerMask)
    {
        sol::state_view lua(luaState);
        auto            result = lua.create_table();
        if (!ctx.physicsService)
            return result;
        uint32_t index = 1;
        for (const auto& hit : ctx.physicsService->raycastAll(toGlmVec3(origin), toGlmVec3(direction), maxDistance,
                                                              makeFilter(activeOnly, layerMask)))
            result[index++] = toScript(hit);
        return result;
    }
    sol::table physicsSphereCast(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& origin,
                                 const ScriptVec3& direction, float radius, float maxDistance,
                                 sol::optional<bool> activeOnly, sol::optional<std::uint32_t> layerMask)
    {
        sol::state_view lua(luaState);
        auto            out = lua.create_table();
        if (!ctx.physicsService)
        {
            out["hit"] = false;
            return out;
        }
        const auto hit = ctx.physicsService->sphereCast(toGlmVec3(origin), toGlmVec3(direction), radius, maxDistance,
                                                        makeFilter(activeOnly, layerMask));
        out["hit"] = hit.has_value();
        if (hit)
        {
            out["entity"]           = makeEntity(hit->entity);
            out["point"]            = toScriptVec3(hit->point);
            out["normal"]           = toScriptVec3(hit->normal);
            out["distance"]         = hit->distance;
            out["fraction"]         = hit->fraction;
            out["startPenetrating"] = hit->startPenetrating;
        }
        return out;
    }
    sol::table physicsOverlapSphere(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& center,
                                    float radius, sol::optional<bool> activeOnly)
    {
        sol::state_view lua(luaState);
        auto            result = lua.create_table();
        if (!ctx.physicsService || radius < 0.0f)
            return result;
        PhysicsQueryFilter filter {};
        filter.activeOnly = activeOnly.value_or(true);
        uint32_t index    = 1;
        for (auto entity : ctx.physicsService->overlapSphere(toGlmVec3(center), radius, filter))
            result[index++] = makeEntity(entity);
        return result;
    }
    sol::table physicsOverlapBox(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& center,
                                 const ScriptVec3& halfExtents, sol::optional<bool> activeOnly)
    {
        sol::state_view lua(luaState);
        auto            result = lua.create_table();
        if (!ctx.physicsService)
            return result;
        PhysicsQueryFilter filter {};
        filter.activeOnly = activeOnly.value_or(true);
        uint32_t index    = 1;
        for (auto entity : ctx.physicsService->overlapBox(toGlmVec3(center), toGlmVec3(halfExtents), filter))
            result[index++] = makeEntity(entity);
        return result;
    }
    sol::table physicsOverlapCapsule(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& center,
                                     float halfHeight, float radius, sol::optional<bool> activeOnly,
                                     sol::optional<std::uint32_t> layerMask)
    {
        sol::state_view lua(luaState);
        auto            result = lua.create_table();
        if (!ctx.physicsService)
            return result;
        uint32_t index = 1;
        for (auto entity : ctx.physicsService->overlapCapsule(toGlmVec3(center), halfHeight, radius,
                                                             makeFilter(activeOnly, layerMask)))
            result[index++] = makeEntity(entity);
        return result;
    }
    sol::table physicsContactPairs(ScriptContext& ctx, sol::this_state luaState, sol::optional<bool> activeOnly)
    {
        sol::state_view lua(luaState);
        auto            result = lua.create_table();
        if (!ctx.physicsService)
            return result;
        uint32_t index = 1;
        for (const auto& pair : ctx.physicsService->contactPairs(activeOnly.value_or(true)))
            result[index++] = ScriptPhysicsContactPair {.a = makeEntity(pair.a), .b = makeEntity(pair.b)};
        return result;
    }
    sol::table physicsContactEvents(ScriptContext& ctx, sol::this_state luaState)
    {
        sol::state_view lua(luaState);
        auto            result = lua.create_table();
        if (!ctx.physicsService)
            return result;
        uint32_t index = 1;
        for (const auto& e : ctx.physicsService->consumeContactEvents())
        {
            auto entry        = lua.create_table();
            entry["a"]        = makeEntity(e.a);
            entry["b"]        = makeEntity(e.b);
            entry["type"]     = e.type == PhysicsContactEvent::Type::eEnter ? "enter" : "exit";
            entry["isSensor"] = e.isSensor;
            result[index++]   = entry;
        }
        return result;
    }
    bool physicsAddTorque(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& torque)
    {
        return ctx.physicsService ? ctx.physicsService->addTorque(entity.value, toGlmVec3(torque)) : false;
    }
    bool physicsAddAngularImpulse(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& impulse)
    {
        return ctx.physicsService ? ctx.physicsService->addAngularImpulse(entity.value, toGlmVec3(impulse)) : false;
    }
    bool physicsSetRotation(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& eulerDegrees,
                            sol::optional<bool> activate)
    {
        return ctx.physicsService ?
                   ctx.physicsService->setRotation(entity.value, toGlmVec3(eulerDegrees), activate.value_or(true)) :
                   false;
    }
    ScriptVec3 physicsGravity(ScriptContext& ctx)
    {
        return toScriptVec3(ctx.physicsService ? ctx.physicsService->gravity() : glm::vec3 {0.0f, -9.81f, 0.0f});
    }
    void physicsSetGravity(ScriptContext& ctx, const ScriptVec3& gravity)
    {
        if (ctx.physicsService)
            ctx.physicsService->setGravity(toGlmVec3(gravity));
    }
    void physicsSetLayerCollision(ScriptContext& ctx, std::uint32_t a, std::uint32_t b, bool enabled)
    {
        if (ctx.physicsService)
            ctx.physicsService->setLayerCollision(a, b, enabled);
    }
    bool physicsLayerCollision(ScriptContext& ctx, std::uint32_t a, std::uint32_t b)
    {
        return ctx.physicsService ? ctx.physicsService->layerCollision(a, b) : true;
    }

    // --- Constraints / joints ---
    namespace
    {
        entt::entity resolveBody(const sol::optional<ScriptEntity>& body)
        {
            return body ? body->value : entt::null;
        }
    } // namespace

    std::uint32_t physicsAddFixedConstraint(ScriptContext& ctx, const ScriptEntity& bodyA, sol::optional<ScriptEntity> bodyB)
    {
        return ctx.physicsService ? ctx.physicsService->addFixedConstraint(bodyA.value, resolveBody(bodyB)) : 0u;
    }
    std::uint32_t physicsAddPointConstraint(ScriptContext& ctx, const ScriptEntity& bodyA, sol::optional<ScriptEntity> bodyB, const ScriptVec3& point)
    {
        return ctx.physicsService ?
                   ctx.physicsService->addPointConstraint(bodyA.value, resolveBody(bodyB), toGlmVec3(point)) :
                   0u;
    }
    std::uint32_t physicsAddDistanceConstraint(ScriptContext& ctx, const ScriptEntity& bodyA, sol::optional<ScriptEntity> bodyB, float minDistance, float maxDistance)
    {
        return ctx.physicsService ?
                   ctx.physicsService->addDistanceConstraint(bodyA.value, resolveBody(bodyB), minDistance, maxDistance) :
                   0u;
    }
    std::uint32_t physicsAddHingeConstraint(ScriptContext& ctx, const ScriptEntity& bodyA, sol::optional<ScriptEntity> bodyB, const ScriptVec3& point, const ScriptVec3& axis, float minAngleDegrees, float maxAngleDegrees)
    {
        return ctx.physicsService ?
                   ctx.physicsService->addHingeConstraint(bodyA.value,
                                                          resolveBody(bodyB),
                                                          toGlmVec3(point),
                                                          toGlmVec3(axis),
                                                          minAngleDegrees,
                                                          maxAngleDegrees) :
                   0u;
    }
    std::uint32_t physicsAddSliderConstraint(ScriptContext& ctx, const ScriptEntity& bodyA, sol::optional<ScriptEntity> bodyB, const ScriptVec3& point, const ScriptVec3& axis, float minDistance, float maxDistance)
    {
        return ctx.physicsService ?
                   ctx.physicsService->addSliderConstraint(bodyA.value,
                                                           resolveBody(bodyB),
                                                           toGlmVec3(point),
                                                           toGlmVec3(axis),
                                                           minDistance,
                                                           maxDistance) :
                   0u;
    }
    std::uint32_t physicsAddConeConstraint(ScriptContext& ctx, const ScriptEntity& bodyA, sol::optional<ScriptEntity> bodyB, const ScriptVec3& point, const ScriptVec3& twistAxis, float halfAngleDegrees)
    {
        return ctx.physicsService ?
                   ctx.physicsService->addConeConstraint(bodyA.value,
                                                         resolveBody(bodyB),
                                                         toGlmVec3(point),
                                                         toGlmVec3(twistAxis),
                                                         halfAngleDegrees) :
                   0u;
    }
    bool physicsRemoveConstraint(ScriptContext& ctx, std::uint32_t constraintId)
    {
        return ctx.physicsService ? ctx.physicsService->removeConstraint(constraintId) : false;
    }
    bool physicsIsConstraintValid(ScriptContext& ctx, std::uint32_t constraintId)
    {
        return ctx.physicsService ? ctx.physicsService->isConstraintValid(constraintId) : false;
    }
    bool physicsSetConstraintMotor(ScriptContext& ctx, std::uint32_t constraintId, bool enabled, float targetVelocity, float maxForce)
    {
        return ctx.physicsService ?
                   ctx.physicsService->setConstraintMotor(constraintId, enabled, targetVelocity, maxForce) :
                   false;
    }

    // --- Character namespace ---
    bool characterHas(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return ctx.physicsService ? ctx.physicsService->hasCharacter(entity.value) : false;
    }
    bool characterMove(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& horizontalVelocity)
    {
        return ctx.physicsService ? ctx.physicsService->characterMove(entity.value, toGlmVec3(horizontalVelocity)) :
                                    false;
    }
    bool characterJump(ScriptContext& ctx, const ScriptEntity& entity, sol::optional<float> speed)
    {
        return ctx.physicsService ? ctx.physicsService->characterJump(entity.value, speed.value_or(0.0f)) : false;
    }
    bool characterIsGrounded(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return ctx.physicsService ? ctx.physicsService->characterIsGrounded(entity.value) : false;
    }
    ScriptVec3 characterVelocity(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return toScriptVec3(ctx.physicsService ? ctx.physicsService->characterVelocity(entity.value) : glm::vec3 {0.0f});
    }
    ScriptVec3 characterGroundNormal(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return toScriptVec3(ctx.physicsService ? ctx.physicsService->characterGroundNormal(entity.value) :
                                                 glm::vec3 {0.0f, 1.0f, 0.0f});
    }
    bool characterSetPosition(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& position)
    {
        return ctx.physicsService ? ctx.physicsService->characterSetPosition(entity.value, toGlmVec3(position)) : false;
    }
} // namespace vultra
