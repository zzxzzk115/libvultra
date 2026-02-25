#pragma once

#include "vultra/core/base/uuid.hpp"

#include <glm/mat4x4.hpp>
#include <string>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }

    // A cooked camera used by the renderer (SRP-style).
    // ECS CameraComponent should be cooked into this struct by CameraSystem.
    struct RenderCamera
    {
        CoreUUID uuid;

        // Debug / editor name (optional)
        std::string name;

        // Sorting
        int priority {0};

        // Matrices
        glm::mat4 view {1.0f};
        glm::mat4 projection {1.0f};
        glm::mat4 viewProjection {1.0f};

        // Render target (nullptr => backbuffer or XR-provided target)
        rhi::Texture* target {nullptr};

        // SRP binding (string key, resolved to a Renderer instance by RenderSystem)
        // Example: "builtin", "forward", "pathtracer", "xr_builtin"
        std::string rendererKey {"builtin"};

        // Optional flags
        bool isOverlay {false};
        bool clearColor {true};
        bool clearDepth {true};
    };
} // namespace vultra
