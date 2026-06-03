#pragma once

#include "vultra/function/world/components/layer_component.hpp"

#include <glm/vec4.hpp>

#include <cstdint>
#include <string>

namespace vultra
{
    struct CameraComponent
    {
        bool primary {false};

        // 0 = perspective, 1 = orthographic. Kept numeric for simple v1 scene serialization.
        uint32_t projection {0};

        float fovYDegrees {60.0f};
        float orthographicHeight {10.0f};
        float zNear {0.1f};
        float zFar {1000.0f};

        // 0 = solid color, 1 = scene environment skybox.
        uint32_t clearMode {0};
        glm::vec4 clearColor {0.02f, 0.025f, 0.035f, 1.0f};
        int       priority {0};
        uint32_t  cullingMask {kRenderLayerAllMask};

        std::string rendererKey {"universal"};
    };
} // namespace vultra
