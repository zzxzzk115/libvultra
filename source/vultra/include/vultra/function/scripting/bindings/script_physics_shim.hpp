#pragma once

// Shim declarations for the Lua `Physics` + `Character` namespaces and the
// `RigidBody` usertype. Generated into the `physics` area; bodies in
// script_physics_shim.cpp own the service-or-component fallback, query filters,
// and table building.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"

#include <sol/sol.hpp>

#include <cstdint>

namespace vultra
{
    struct VBIND_MODULE(name = Physics, area = physics, service = physicsService) PhysicsModule
    {
    };
    struct VBIND_MODULE(name = Character, area = physics, service = physicsService) CharacterModule
    {
    };
    struct VBIND_USERTYPE(name = RigidBody, handle = ScriptRigidBodyRef, area = physics,
                          component = RigidBodyComponent, accessor = rigidBody) RigidBodyUsertype
    {
    };

#define VBIND_RB_PROP(lname, getter, setter, ctype) \
    VBIND_PROPERTY(usertype = RigidBody, name = lname, set = setter) \
    ctype getter(ScriptContext& ctx, const ScriptRigidBodyRef& self); \
    void setter(ScriptContext& ctx, const ScriptRigidBodyRef& self, ctype value);

    VBIND_RB_PROP(linearVelocity, rbGetLinearVelocity, rbSetLinearVelocity, ScriptVec3)
    VBIND_RB_PROP(angularVelocity, rbGetAngularVelocity, rbSetAngularVelocity, ScriptVec3)
    VBIND_RB_PROP(motionType, rbGetMotionType, rbSetMotionType, std::uint32_t)
    VBIND_RB_PROP(objectLayer, rbGetObjectLayer, rbSetObjectLayer, std::uint32_t)
    VBIND_RB_PROP(isSensor, rbGetIsSensor, rbSetIsSensor, bool)
    VBIND_RB_PROP(motionQuality, rbGetMotionQuality, rbSetMotionQuality, std::uint32_t)
    VBIND_RB_PROP(allowSleeping, rbGetAllowSleeping, rbSetAllowSleeping, bool)
    VBIND_RB_PROP(mass, rbGetMass, rbSetMass, float)
    VBIND_RB_PROP(overrideMass, rbGetOverrideMass, rbSetOverrideMass, bool)
    VBIND_RB_PROP(friction, rbGetFriction, rbSetFriction, float)
    VBIND_RB_PROP(restitution, rbGetRestitution, rbSetRestitution, float)
    VBIND_RB_PROP(linearDamping, rbGetLinearDamping, rbSetLinearDamping, float)
    VBIND_RB_PROP(angularDamping, rbGetAngularDamping, rbSetAngularDamping, float)
    VBIND_RB_PROP(gravityFactor, rbGetGravityFactor, rbSetGravityFactor, float)
    VBIND_RB_PROP(maxLinearVelocity, rbGetMaxLinearVelocity, rbSetMaxLinearVelocity, float)
    VBIND_RB_PROP(maxAngularVelocity, rbGetMaxAngularVelocity, rbSetMaxAngularVelocity, float)
#undef VBIND_RB_PROP

    VBIND_FN(usertype = RigidBody, name = activate, body = shim)
    bool rbActivate(ScriptContext& ctx, const ScriptRigidBodyRef& self);
    VBIND_FN(usertype = RigidBody, name = addForce, body = shim)
    bool rbAddForce(ScriptContext& ctx, const ScriptRigidBodyRef& self, const ScriptVec3& force);
    VBIND_FN(usertype = RigidBody, name = addImpulse, body = shim)
    bool rbAddImpulse(ScriptContext& ctx, const ScriptRigidBodyRef& self, const ScriptVec3& impulse);
    VBIND_FN(usertype = RigidBody, name = setPosition, body = shim)
    bool rbSetPosition(ScriptContext& ctx, const ScriptRigidBodyRef& self, const ScriptVec3& position,
                       sol::optional<bool> activate);

    // --- Physics namespace ---
    VBIND_FN(module = Physics, name = enabled, body = shim) bool physicsEnabled(ScriptContext& ctx);
    VBIND_FN(module = Physics, name = setEnabled, body = shim) void physicsSetEnabled(ScriptContext& ctx, bool enabled);
    VBIND_FN(module = Physics, name = bodyCount, body = shim) std::uint32_t physicsBodyCount(ScriptContext& ctx);
    VBIND_FN(module = Physics, name = hasBody, body = shim) bool physicsHasBody(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Physics, name = fixedTimeStep, body = shim) float physicsFixedTimeStep(ScriptContext& ctx);
    VBIND_FN(module = Physics, name = setFixedTimeStep, body = shim) void physicsSetFixedTimeStep(ScriptContext& ctx, float seconds);
    VBIND_FN(module = Physics, name = addForce, body = shim) bool physicsAddForce(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& force);
    VBIND_FN(module = Physics, name = addImpulse, body = shim) bool physicsAddImpulse(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& impulse);
    VBIND_FN(module = Physics, name = setPosition, body = shim) bool physicsSetPosition(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& position, sol::optional<bool> activate);
    VBIND_FN(module = Physics, name = raycast, body = shim) ScriptPhysicsRaycastHit physicsRaycast(ScriptContext& ctx, const ScriptVec3& origin, const ScriptVec3& direction, float maxDistance, sol::optional<bool> activeOnly, sol::optional<std::uint32_t> layerMask);
    VBIND_FN(module = Physics, name = raycastAll, body = shim) sol::table physicsRaycastAll(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& origin, const ScriptVec3& direction, float maxDistance, sol::optional<bool> activeOnly, sol::optional<std::uint32_t> layerMask);
    VBIND_FN(module = Physics, name = sphereCast, body = shim) sol::table physicsSphereCast(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& origin, const ScriptVec3& direction, float radius, float maxDistance, sol::optional<bool> activeOnly, sol::optional<std::uint32_t> layerMask);
    VBIND_FN(module = Physics, name = overlapSphere, body = shim) sol::table physicsOverlapSphere(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& center, float radius, sol::optional<bool> activeOnly);
    VBIND_FN(module = Physics, name = overlapBox, body = shim) sol::table physicsOverlapBox(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& center, const ScriptVec3& halfExtents, sol::optional<bool> activeOnly);
    VBIND_FN(module = Physics, name = overlapCapsule, body = shim) sol::table physicsOverlapCapsule(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& center, float halfHeight, float radius, sol::optional<bool> activeOnly, sol::optional<std::uint32_t> layerMask);
    VBIND_FN(module = Physics, name = contactPairs, body = shim) sol::table physicsContactPairs(ScriptContext& ctx, sol::this_state luaState, sol::optional<bool> activeOnly);
    VBIND_FN(module = Physics, name = contactEvents, body = shim) sol::table physicsContactEvents(ScriptContext& ctx, sol::this_state luaState);
    VBIND_FN(module = Physics, name = addTorque, body = shim) bool physicsAddTorque(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& torque);
    VBIND_FN(module = Physics, name = addAngularImpulse, body = shim) bool physicsAddAngularImpulse(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& impulse);
    VBIND_FN(module = Physics, name = setRotation, body = shim) bool physicsSetRotation(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& eulerDegrees, sol::optional<bool> activate);
    VBIND_FN(module = Physics, name = gravity, body = shim) ScriptVec3 physicsGravity(ScriptContext& ctx);
    VBIND_FN(module = Physics, name = setGravity, body = shim) void physicsSetGravity(ScriptContext& ctx, const ScriptVec3& gravity);
    VBIND_FN(module = Physics, name = setLayerCollision, body = shim) void physicsSetLayerCollision(ScriptContext& ctx, std::uint32_t a, std::uint32_t b, bool enabled);
    VBIND_FN(module = Physics, name = layerCollision, body = shim) bool physicsLayerCollision(ScriptContext& ctx, std::uint32_t a, std::uint32_t b);

    // --- Character namespace ---
    VBIND_FN(module = Character, name = has, body = shim) bool characterHas(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Character, name = move, body = shim) bool characterMove(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& horizontalVelocity);
    VBIND_FN(module = Character, name = jump, body = shim) bool characterJump(ScriptContext& ctx, const ScriptEntity& entity, sol::optional<float> speed);
    VBIND_FN(module = Character, name = isGrounded, body = shim) bool characterIsGrounded(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Character, name = velocity, body = shim) ScriptVec3 characterVelocity(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Character, name = groundNormal, body = shim) ScriptVec3 characterGroundNormal(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Character, name = setPosition, body = shim) bool characterSetPosition(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& position);
} // namespace vultra
