#pragma once

#include <glm/mat4x4.hpp>

namespace vultra
{
    [[nodiscard]] inline glm::mat4 flipProjectionY(glm::mat4 projection)
    {
        // GLM indexes columns first. Flip the complete clip-Y row, including off-center/jitter terms.
        for (glm::length_t column = 0; column < 4; ++column)
        {
            projection[column][1] = -projection[column][1];
        }
        return projection;
    }

    [[nodiscard]] inline glm::mat4 projectionDepthMinusOneToZeroOne(glm::mat4 projection)
    {
        for (glm::length_t column = 0; column < 4; ++column)
        {
            projection[column][2] = 0.5f * (projection[column][2] + projection[column][3]);
        }
        return projection;
    }

    [[nodiscard]] inline glm::mat4 projectionDepthZeroOneToMinusOne(glm::mat4 projection)
    {
        for (glm::length_t column = 0; column < 4; ++column)
        {
            projection[column][2] = 2.0f * projection[column][2] - projection[column][3];
        }
        return projection;
    }
} // namespace vultra
