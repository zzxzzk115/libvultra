#pragma once

#include "vultra/core/rhi/extent2d.hpp"

#include <fg/Fwd.hpp>
#include <glm/ext/matrix_float4x4.hpp>

namespace vultra
{
    namespace gfx
    {
        struct FrameInfo
        {
            float time;
            float deltaTime;
        };

        void uploadFrameBlock(FrameGraph&, FrameGraphBlackboard&, const FrameInfo&);

        struct CameraInfo
        {
            glm::mat4 inverseOriginalProjection;
            glm::mat4 projection;
            glm::mat4 view;
            glm::mat4 viewProjection;
            float     zNear;
            float     zFar;
        };

        void uploadCameraBlock(FrameGraph&, FrameGraphBlackboard&, const vultra::rhi::Extent2D, const CameraInfo&);

        constexpr int LIGHTINFO_MAX_POINT_LIGHTS = 32;
        constexpr int LIGHTINFO_MAX_AREA_LIGHTS  = 32;

        struct LightInfo
        {
            // Directional light
            struct DirectionalLightInfo
            {
                glm::vec3 direction;
                glm::vec3 color {0.0f, 0.0f, -1.0f};
                float     intensity {1.0f};
                glm::mat4 view {1.0f};
                glm::mat4 projection {1.0f};
            };
            DirectionalLightInfo directionalLight {};

            // Point lights
            int pointLightCount {0};
            struct PointLightInfo
            {
                glm::vec3 position;
                float     intensity;
                glm::vec3 color; // rgb color
                float     radius;
            };
            PointLightInfo pointLights[LIGHTINFO_MAX_POINT_LIGHTS] {};

            // Area light array (limited size for UBO); each area light uses 4 vec4 slots
            int areaLightCount {0};
            struct AreaLightInfo
            {
                glm::vec3 position;
                float     intensity;
                glm::vec3 u;        // half-axis U
                float     twoSided; // 0 = false, 1 = true
                glm::vec3 v;        // half-axis V
                float     padding;  // unused
                glm::vec3 color;    // rgb color
            };
            AreaLightInfo areaLights[LIGHTINFO_MAX_AREA_LIGHTS] {};
        };

        void uploadLightBlock(FrameGraph&, FrameGraphBlackboard&, const LightInfo&);
    } // namespace gfx
} // namespace vultra