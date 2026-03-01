#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/resource/gpu_scene.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <string>
#include <vector>

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
        glm::vec4     clearValue {0, 0, 0, 1};

        // SRP binding (string key, resolved to a Renderer instance by RenderSystem)
        // Example: "builtin", "forward", "pathtracer", "xr_builtin"
        std::string rendererKey {"builtin"};

        // Temp: remove. use frame graph blackboard for per-camera data.
        Ref<rhi::UniformBuffer> uniformBuffer {nullptr};
        void                    ensureUniformBuffer(rhi::RenderDevice& rd)
        {
            if (!uniformBuffer)
            {
                // Test: hard-coded view projection
                view       = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
                projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
                projection[1][1] *= -1; // GL to Vulkan clip space
                viewProjection = projection * view;
                uniformBuffer  = createRef<rhi::UniformBuffer>(rd.createUniformBuffer(sizeof(glm::mat4)));
                rd.uploadS(*uniformBuffer, 0, sizeof(glm::mat4), &viewProjection);
            }
        }
    };

    // Cooked render instance extracted from World.
    // Renderer consumes RenderWorld only.
    struct RenderInstance
    {
        CoreUUID  entity;
        uint32_t  meshIndex {0};
        uint32_t  materialIndex {0};
        glm::mat4 worldMatrix {1.0f};
    };

    // Double-buffered cooked scene for rendering.
    struct RenderWorld
    {
        uint64_t                    frameIndex {0};
        std::vector<RenderCamera>   cameras;
        std::vector<RenderInstance> instances;

        resource::GpuScene* gpuScene {nullptr};

        void clear()
        {
            cameras.clear();
            instances.clear();
        }
    };
} // namespace vultra