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

        constexpr int LIGHTINFO_MAX_AREA_LIGHTS = 32;

        struct LightInfo
        {
            glm::vec3 direction;
            glm::vec3 color {1.0f, 1.0f, 1.0f};
            glm::mat4 view;
            glm::mat4 projection;
            // Area light array (limited size for UBO); each area light uses 4 vec4 slots
            int areaLightCount {0};
            struct AreaLightGPU
            {
                glm::vec4 posIntensity; // xyz position, w intensity
                glm::vec4 uTwoSided;    // xyz half-axis U, w twoSided (0/1)
                glm::vec4 vPadding;     // xyz half-axis V, w unused
                glm::vec4 color;        // rgb color, a unused
            };
            AreaLightGPU areaLights[LIGHTINFO_MAX_AREA_LIGHTS] {};
        };

        void uploadLightBlock(FrameGraph&, FrameGraphBlackboard&, const LightInfo&);
    } // namespace gfx
} // namespace vultra