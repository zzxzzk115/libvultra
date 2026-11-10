#include <vultra/core/math/rigid_pose.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace vultra
{
    std::expected<void, std::string> validateRigidPoseExpected(const glm::mat4& pose)
    {
        for (size_t column = 0; column < 4; ++column)
        {
            for (size_t row = 0; row < 4; ++row)
            {
                if (!std::isfinite(pose[column][row]))
                {
                    return std::unexpected("Rigid pose contains a non-finite element");
                }
            }
        }
        const glm::mat3 rotation(pose);
        const auto      identity = glm::transpose(rotation) * rotation;
        for (size_t column = 0; column < 3; ++column)
        {
            for (size_t row = 0; row < 3; ++row)
            {
                if (std::abs(identity[column][row] - float(column == row)) > 1e-4f)
                {
                    return std::unexpected("Rigid pose contains scale or shear");
                }
            }
        }
        if (std::abs(glm::determinant(rotation) - 1) > 1e-4f ||
            std::abs(pose[0][3]) + std::abs(pose[1][3]) + std::abs(pose[2][3]) > 1e-5f ||
            std::abs(pose[3][3] - 1) > 1e-5f)
        {
            return std::unexpected("Rigid pose must be affine with a proper rotation");
        }
        return {};
    }

    void validateRigidPose(const glm::mat4& pose)
    {
        if (auto valid = validateRigidPoseExpected(pose); !valid)
        {
            throw std::invalid_argument(valid.error());
        }
    }
} // namespace vultra
