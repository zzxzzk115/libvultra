#pragma once

#include <vbase/service/service_registry.hpp>

#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace vultra
{
    struct PhysicsRaycastHit
    {
        entt::entity entity {entt::null};
        glm::vec3    point {0.0f};
        glm::vec3    normal {0.0f, 1.0f, 0.0f};
        float        fraction {0.0f};
        float        distance {0.0f};
    };

    // Result of a swept shape query (sphere/box/capsule cast).
    struct PhysicsShapeCastHit
    {
        entt::entity entity {entt::null};
        glm::vec3    point {0.0f};
        glm::vec3    normal {0.0f, 1.0f, 0.0f};
        float        fraction {0.0f};
        float        distance {0.0f};
        bool         startPenetrating {false};
    };

    // Shared filter for spatial queries. layerMask is a bitmask over logical
    // collision-layer indices (bit i set => include bodies on layer i). ignore lets
    // callers skip a specific entity (e.g. the shooter's own body).
    struct PhysicsQueryFilter
    {
        uint32_t     layerMask {0xFFFFFFFFu};
        bool         activeOnly {true};
        entt::entity ignore {entt::null};
    };

    struct PhysicsContactPair
    {
        entt::entity a {entt::null};
        entt::entity b {entt::null};
    };

    // A discrete contact/trigger event captured from the physics step. `eEnter` is a new
    // contact (trigger enter when isSensor), `eExit` is a contact ending (trigger exit).
    struct PhysicsContactEvent
    {
        enum class Type : uint8_t
        {
            eEnter,
            eExit,
        };
        Type         type {Type::eEnter};
        entt::entity a {entt::null};
        entt::entity b {entt::null};
        bool         isSensor {false}; // either body is a trigger/sensor
    };

    class IPhysicsService
    {
    public:
        SERVICE_REGISTER(IPhysicsService)

        virtual ~IPhysicsService() = default;

        virtual void setEnabled(bool enabled) = 0;
        virtual bool enabled() const = 0;

        virtual void setPlaybackState(bool playing, bool paused) = 0;
        virtual bool playing() const = 0;
        virtual bool paused() const = 0;
        virtual void requestSingleStep() = 0;

        virtual void setFixedTimeStep(float seconds) = 0;
        virtual float fixedTimeStep() const = 0;

        virtual uint32_t bodyCount() const = 0;
        virtual bool     hasBody(entt::entity entity) const = 0;
        virtual bool     activate(entt::entity entity) = 0;

        virtual glm::vec3 linearVelocity(entt::entity entity) const = 0;
        virtual bool      setLinearVelocity(entt::entity entity, const glm::vec3& velocity) = 0;
        virtual glm::vec3 angularVelocity(entt::entity entity) const = 0;
        virtual bool      setAngularVelocity(entt::entity entity, const glm::vec3& velocity) = 0;

        virtual bool addForce(entt::entity entity, const glm::vec3& force) = 0;
        virtual bool addTorque(entt::entity entity, const glm::vec3& torque) = 0;
        virtual bool addImpulse(entt::entity entity, const glm::vec3& impulse) = 0;
        virtual bool addAngularImpulse(entt::entity entity, const glm::vec3& impulse) = 0;
        virtual bool setPosition(entt::entity entity, const glm::vec3& position, bool activate = true) = 0;
        virtual bool setRotation(entt::entity entity, const glm::vec3& eulerDegrees, bool activate = true) = 0;

        // World gravity (default {0,-9.81,0}).
        virtual void      setGravity(const glm::vec3& gravity) = 0;
        virtual glm::vec3 gravity() const = 0;

        // Collision-layer matrix. Logical layer indices are stored on
        // RigidBodyComponent::objectLayer; this controls which pairs collide.
        virtual void setLayerCollision(uint32_t layerA, uint32_t layerB, bool enabled) = 0;
        virtual bool layerCollision(uint32_t layerA, uint32_t layerB) const = 0;

        // Closest narrow-phase ray hit (real Jolt geometry, not AABB).
        virtual std::optional<PhysicsRaycastHit> raycast(const glm::vec3&          origin,
                                                         const glm::vec3&          direction,
                                                         float                     maxDistance,
                                                         const PhysicsQueryFilter& filter = {}) const = 0;
        // All ray hits sorted near-to-far.
        virtual std::vector<PhysicsRaycastHit> raycastAll(const glm::vec3&          origin,
                                                          const glm::vec3&          direction,
                                                          float                     maxDistance,
                                                          const PhysicsQueryFilter& filter = {}) const = 0;
        // Swept-sphere cast (spherecast). Returns the first blocking hit.
        virtual std::optional<PhysicsShapeCastHit> sphereCast(const glm::vec3&          origin,
                                                              const glm::vec3&          direction,
                                                              float                     radius,
                                                              float                     maxDistance,
                                                              const PhysicsQueryFilter& filter = {}) const = 0;
        virtual std::vector<entt::entity> overlapSphere(const glm::vec3&          center,
                                                        float                     radius,
                                                        const PhysicsQueryFilter& filter = {}) const = 0;
        virtual std::vector<entt::entity> overlapBox(const glm::vec3&          center,
                                                     const glm::vec3&          halfExtents,
                                                     const PhysicsQueryFilter& filter = {}) const = 0;
        virtual std::vector<entt::entity> overlapCapsule(const glm::vec3&          center,
                                                         float                     halfHeight,
                                                         float                     radius,
                                                         const PhysicsQueryFilter& filter = {}) const = 0;
        virtual std::vector<PhysicsContactPair> contactPairs(bool activeOnly = true) const = 0;

        // Drain contact/trigger events accumulated since the last call (begin/end + sensor).
        virtual std::vector<PhysicsContactEvent> consumeContactEvents() = 0;

        // --- Character controller (Jolt CharacterVirtual) ---
        // Driven through a CharacterControllerComponent. These helpers let gameplay set the
        // desired horizontal velocity, request a jump, read ground/velocity state, and
        // teleport, without touching the component directly.
        virtual bool      hasCharacter(entt::entity entity) const = 0;
        virtual bool      characterMove(entt::entity entity, const glm::vec3& horizontalVelocity) = 0;
        virtual bool      characterJump(entt::entity entity, float speed) = 0;
        virtual bool      characterIsGrounded(entt::entity entity) const = 0;
        virtual glm::vec3 characterVelocity(entt::entity entity) const = 0;
        virtual glm::vec3 characterGroundNormal(entt::entity entity) const = 0;
        virtual bool      characterSetPosition(entt::entity entity, const glm::vec3& position) = 0;

        // --- Constraints / joints (Jolt two-body constraints) ---
        // Connect two rigid bodies. `bodyB == entt::null` anchors `bodyA` to the world.
        // `point`/`axis` are world-space at creation; angles in degrees, distances in meters.
        // Each returns an opaque constraint id (0 == failure); pass it to remove/motor.
        // Missing bodies are created on demand if the entity has a RigidBodyComponent.
        virtual uint32_t addFixedConstraint(entt::entity bodyA, entt::entity bodyB) = 0;
        virtual uint32_t addPointConstraint(entt::entity bodyA, entt::entity bodyB, const glm::vec3& point) = 0;
        virtual uint32_t addDistanceConstraint(entt::entity bodyA,
                                               entt::entity bodyB,
                                               float        minDistance,
                                               float        maxDistance) = 0;
        virtual uint32_t addHingeConstraint(entt::entity     bodyA,
                                            entt::entity     bodyB,
                                            const glm::vec3& point,
                                            const glm::vec3& axis,
                                            float            minAngleDegrees,
                                            float            maxAngleDegrees) = 0;
        virtual uint32_t addSliderConstraint(entt::entity     bodyA,
                                             entt::entity     bodyB,
                                             const glm::vec3& point,
                                             const glm::vec3& axis,
                                             float            minDistance,
                                             float            maxDistance) = 0;
        virtual uint32_t addConeConstraint(entt::entity     bodyA,
                                           entt::entity     bodyB,
                                           const glm::vec3& point,
                                           const glm::vec3& twistAxis,
                                           float            halfAngleDegrees) = 0;
        virtual bool removeConstraint(uint32_t constraintId) = 0;
        virtual bool isConstraintValid(uint32_t constraintId) const = 0;
        // Drive a hinge (angular, degrees/sec) or slider (linear, m/s) motor toward a target
        // velocity. `enabled == false` frees the motor. `maxForce` caps the motor torque/force.
        virtual bool setConstraintMotor(uint32_t constraintId,
                                        bool     enabled,
                                        float    targetVelocity,
                                        float    maxForce) = 0;
    };
} // namespace vultra
