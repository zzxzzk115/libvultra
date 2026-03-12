#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"
#include "vultra/function/resource/gpu_scene_view.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <array>
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
        glm::mat4 inverseView {1.0f};
        glm::mat4 inverseProjection {1.0f};
        glm::mat4 inverseViewProjection {1.0f};

        float zNear {0.1f};
        float zFar {1000.0f};
        float fovY {glm::radians(60.0f)};

        std::array<glm::vec4, 6> frustumPlanes {glm::vec4(0.0f),
                                                glm::vec4(0.0f),
                                                glm::vec4(0.0f),
                                                glm::vec4(0.0f),
                                                glm::vec4(0.0f),
                                                glm::vec4(0.0f)};

        // Render target (nullptr => backbuffer or XR-provided target)
        rhi::Texture* target {nullptr};
        glm::vec4     clearValue {0, 0, 0, 1};

        // SRP binding (string key, resolved to a Renderer instance by RenderSystem)
        // Example: "universal", "hd"
        std::string rendererKey {"universal"};
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

        resource::GpuSceneDatabase* gpuSceneDatabase {nullptr};
        resource::GpuSceneView*     gpuSceneView {nullptr};

        void clear()
        {
            cameras.clear();
            instances.clear();
        }
    };
} // namespace vultra