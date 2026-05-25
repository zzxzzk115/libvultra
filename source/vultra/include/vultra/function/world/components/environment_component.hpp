#pragma once

#include "vultra/core/base/uuid.hpp"

#include <glm/vec3.hpp>

namespace vultra
{
    struct EnvironmentComponent
    {
        bool active {true};

        CoreUUID skybox;

        glm::vec3 ambientColor {0.15f};
        float     ambientIntensity {1.0f};

        bool      enableIBL {false};
        glm::vec3 iblColor {0.04f, 0.045f, 0.05f};
        float     iblIntensity {1.0f};
    };
} // namespace vultra
