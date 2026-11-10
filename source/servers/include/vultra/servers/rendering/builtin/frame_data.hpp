#pragma once

#include <vultra/servers/rendering/builtin/render_light.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

namespace vultra
{
    // CPU mirror of builtin/shaders/resources/frame_block.slangh.
    struct BuiltinLightData
    {
        glm::vec4 positionRange;
        glm::vec4 directionKind;
        glm::vec4 colorIntensity;
        glm::vec4 cones;
    };

    struct BuiltinFrameData
    {
        glm::mat4                                      viewProjection;
        glm::mat4                                      inverseViewProjection;
        glm::mat4                                      view;
        std::array<glm::mat4, 4>                       lightViewProjection;
        glm::vec4                                      cameraPosition;
        glm::vec4                                      lightDirection;
        glm::vec4                                      lightColor;
        glm::vec4                                      ambientColor;
        glm::vec4                                      cascadeSplits;
        glm::vec4                                      cascadeWidths;
        glm::vec4                                      cascadeDepthRanges;
        glm::vec4                                      shadowParameters;
        glm::vec4                                      options;
        glm::vec4                                      cameraClip;
        glm::vec4                                      overrides;
        glm::vec4                                      lightConfig;
        std::array<BuiltinLightData, kMaxRenderLights> lights;
        glm::vec4                                      shadowViewDepth;
    };

    // The scene-light program keeps ambient, emission and scene-list lights, but omits these three frame features.
    inline bool usesOnlySceneLights(const BuiltinFrameData& frame)
    {
        const bool noPrimary = frame.lightDirection.w == 0 ||
                               (frame.lightColor.x == 0 && frame.lightColor.y == 0 && frame.lightColor.z == 0);
        return frame.options.x <= .5f && noPrimary && frame.cameraClip.w == 0;
    }

    // dot(result, float4(worldPosition, 1)) equals negative view-space Z.
    inline glm::vec4 shadowDepthPlane(const glm::mat4& view)
    {
        return -glm::vec4(view[0][2], view[1][2], view[2][2], view[3][2]);
    }

    static_assert(sizeof(BuiltinLightData) == 64);
    static_assert(offsetof(BuiltinFrameData, ambientColor) == 496);
    static_assert(offsetof(BuiltinFrameData, lightConfig) == 624 && offsetof(BuiltinFrameData, lights) == 640);
    static_assert(offsetof(BuiltinFrameData, shadowViewDepth) == 4736 && sizeof(BuiltinFrameData) == 4752);
} // namespace vultra
