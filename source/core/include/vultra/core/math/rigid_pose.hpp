#pragma once

#include <glm/mat4x4.hpp>

#include <expected>
#include <string>

namespace vultra
{
    std::expected<void, std::string> validateRigidPoseExpected(const glm::mat4& pose);
    void                             validateRigidPose(const glm::mat4& pose); // Legacy boundary.
} // namespace vultra
