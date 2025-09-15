#include "vultra/function/renderer/builtin/upload_resources.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/transient_buffer.hpp"
#include "vultra/function/framegraph/upload_struct.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/frame_data.hpp"
#include "vultra/function/renderer/builtin/resources/light_data.hpp"

#include <fg/Blackboard.hpp>

namespace vultra
{
    struct alignas(16) GPUFrameBlock
    {
        explicit GPUFrameBlock(const FrameInfo& frameInfo) : time(frameInfo.time), deltaTime(frameInfo.deltaTime) {}

        float time {0.0f};
        float deltaTime {0.0f};
    };
    static_assert(sizeof(GPUFrameBlock) == 16);

    void uploadFrameBlock(FrameGraph& fg, FrameGraphBlackboard& blackboard, const FrameInfo& frameInfo)
    {
        const auto buffer = framegraph::uploadStruct(fg,
                                                     "UploadFrameBlock",
                                                     framegraph::TransientBuffer {
                                                         .name = "FrameBlock",
                                                         .type = framegraph::BufferType::eUniformBuffer,
                                                         .data = GPUFrameBlock {frameInfo},
                                                     });
        if (!blackboard.has<FrameData>())
        {
            blackboard.add<FrameData>(buffer);
        }
        else
        {
            blackboard.get<FrameData>().frameBlock = buffer;
        }
    }

    struct alignas(16) GPUCameraBlock
    {
        GPUCameraBlock(const rhi::Extent2D extent, const CameraInfo& camera) :
            projection {camera.projection}, inversedProjection {glm::inverse(projection)}, view {camera.view},
            inversedView {glm::inverse(view)}, viewProjection {camera.viewProjection},
            inversedViewProjection {glm::inverse(viewProjection)}, resolution {extent}, zNear {camera.zNear},
            zFar {camera.zFar}
        {}

        glm::mat4 projection {1.0f};
        glm::mat4 inversedProjection {1.0f};
        glm::mat4 view {1.0f};
        glm::mat4 inversedView {1.0f};
        glm::mat4 viewProjection {1.0f};
        glm::mat4 inversedViewProjection {1.0f};

        rhi::Extent2D resolution;

        float zNear;
        float zFar;
    };
    static_assert(sizeof(GPUCameraBlock) == 400);

    [[nodiscard]] auto uploadCameraBlock(FrameGraph& fg, GPUCameraBlock&& cameraBlock)
    {
        return framegraph::uploadStruct(fg,
                                        "UploadCameraBlock",
                                        framegraph::TransientBuffer {
                                            .name = "CameraBlock",
                                            .type = framegraph::BufferType::eUniformBuffer,
                                            .data = std::move(cameraBlock),
                                        });
    }

    void uploadCameraBlock(FrameGraph&           fg,
                           FrameGraphBlackboard& blackboard,
                           const rhi::Extent2D   resolution,
                           const CameraInfo&     cameraInfo)
    {
        auto buffer = uploadCameraBlock(fg, GPUCameraBlock {resolution, cameraInfo});
        if (!blackboard.has<CameraData>())
        {
            blackboard.add<CameraData>(buffer);
        }
        else
        {
            blackboard.get<CameraData>().cameraBlock = buffer;
        }
    }

    struct alignas(16) GPUAreaLight
    {
        glm::vec4 posIntensity; // xyz position, w intensity
        glm::vec4 uTwoSided;    // xyz U, w twoSided flag
        glm::vec4 vPadding;     // xyz V, w padding
        glm::vec4 color;        // rgb color, a unused
    };

    struct alignas(16) GPULightBlock
    {
        explicit GPULightBlock(const LightInfo& lightInfo) :
            direction {lightInfo.direction}, color {lightInfo.color},
            lightSpaceMatrix(lightInfo.projection * lightInfo.view), areaLightCount {lightInfo.areaLightCount}
        {
            for (int i = 0; i < lightInfo.areaLightCount && i < LIGHTINFO_MAX_AREA_LIGHTS; ++i)
            {
                areaLights[i].posIntensity = lightInfo.areaLights[i].posIntensity;
                areaLights[i].uTwoSided    = lightInfo.areaLights[i].uTwoSided;
                areaLights[i].vPadding     = lightInfo.areaLights[i].vPadding;
                areaLights[i].color        = lightInfo.areaLights[i].color;
            }
        }

        glm::vec3    direction {0.0f};
        float        padding0 {0.0f};
        glm::vec3    color {1.0f};
        float        padding1 {0.0f};
        glm::mat4    lightSpaceMatrix {1.0f};
        int          areaLightCount {0};                       // implicit padding to 16-byte alignment before array
        GPUAreaLight areaLights[LIGHTINFO_MAX_AREA_LIGHTS] {}; // value-init to zero
    };

    static_assert(sizeof(GPUAreaLight) == 64, "GPUAreaLight unexpected size (std140 mismatch)");
    static_assert(sizeof(GPULightBlock) == 2160, "GPULightBlock unexpected size (std140 mismatch)");

    void uploadLightBlock(FrameGraph& fg, FrameGraphBlackboard& blackboard, const LightInfo& lightInfo)
    {
        auto buffer = framegraph::uploadStruct(fg,
                                               "UploadLightBlock",
                                               framegraph::TransientBuffer {
                                                   .name = "LightBlock",
                                                   .type = framegraph::BufferType::eUniformBuffer,
                                                   .data = GPULightBlock {lightInfo},
                                               });

        if (!blackboard.has<LightData>())
        {
            blackboard.add<LightData>(buffer);
        }
        else
        {
            blackboard.get<LightData>().lightBlock = buffer;
        }
    }
} // namespace vultra