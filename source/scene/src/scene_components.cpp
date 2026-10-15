#include <vultra/scene/scene_components.hpp>

#include <cmath>
#include <stdexcept>

namespace vultra
{
    void SceneTransform::validate(const glm::mat4& transform)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!std::isfinite(transform[column][row]))
                {
                    throw std::invalid_argument("Scene transform has a non-finite value");
                }
            }
        }
        if (transform[0][3] != 0 || transform[1][3] != 0 || transform[2][3] != 0 || transform[3][3] != 1)
        {
            throw std::invalid_argument("Scene transform must be affine");
        }
    }
} // namespace vultra
