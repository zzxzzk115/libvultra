#pragma once

#include <vultra/core/base/error.hpp>

#include <glm/mat4x4.hpp>

#include <expected>

namespace vultra
{
    [[nodiscard]] std::expected<void, Error> validateRigidPose(const glm::mat4& pose);
} // namespace vultra
