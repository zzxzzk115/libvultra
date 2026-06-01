#pragma once

#include <vbase/service/service_registry.hpp>

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

    struct PhysicsContactPair
    {
        entt::entity a {entt::null};
        entt::entity b {entt::null};
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
        virtual bool addImpulse(entt::entity entity, const glm::vec3& impulse) = 0;
        virtual bool setPosition(entt::entity entity, const glm::vec3& position, bool activate = true) = 0;

        virtual std::optional<PhysicsRaycastHit> raycast(const glm::vec3& origin,
                                                         const glm::vec3& direction,
                                                         float            maxDistance,
                                                         bool             activeOnly = true) const = 0;
        virtual std::vector<entt::entity> overlapSphere(const glm::vec3& center,
                                                        float            radius,
                                                        bool             activeOnly = true) const = 0;
        virtual std::vector<entt::entity> overlapBox(const glm::vec3& center,
                                                     const glm::vec3& halfExtents,
                                                     bool             activeOnly = true) const = 0;
        virtual std::vector<PhysicsContactPair> contactPairs(bool activeOnly = true) const = 0;
    };
} // namespace vultra
