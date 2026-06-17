#pragma once

// Jolt <-> engine value conversions used throughout the physics system. Header-inline so the
// physics translation unit and any helper units share one definition.

#include <Jolt/Jolt.h>

#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Real.h>
#include <Jolt/Math/Vec3.h>

#include <entt/entity/entity.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace vultra
{
    inline JPH::Vec3  toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
    inline JPH::RVec3 toJoltR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
    inline JPH::Quat  toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }

    inline glm::vec3 fromJolt(const JPH::Vec3& v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
    inline glm::vec3 fromJoltR(const JPH::RVec3& v)
    {
        return {static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())};
    }
    inline glm::quat fromJolt(const JPH::Quat& q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

    inline JPH::uint64 toUserData(entt::entity entity) { return static_cast<JPH::uint64>(entt::to_integral(entity)); }
    inline entt::entity fromUserData(JPH::uint64 userData)
    {
        return static_cast<entt::entity>(static_cast<entt::id_type>(userData));
    }
} // namespace vultra
