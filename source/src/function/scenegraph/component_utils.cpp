#include "vultra/function/scenegraph/component_utils.hpp"

namespace vultra
{
    glm::mat4 getCameraViewMatrix(const TransformComponent& camTransform)
    {
        return glm::lookAt(camTransform.position, camTransform.position + camTransform.forward(), camTransform.up());
    }

    glm::mat4 getCameraProjectionMatrix(CameraComponent& camComponent, bool invertY)
    {
        glm::mat4 projection;

        if (camComponent.projection == CameraProjection::ePerspective)
        {
            projection = glm::perspective(glm::radians(camComponent.fov),
                                          static_cast<float>(camComponent.viewPortWidth) /
                                              static_cast<float>(camComponent.viewPortHeight),
                                          camComponent.zNear,
                                          camComponent.zFar);
        }
        else
        {
            projection = glm::ortho(0.0f,
                                    static_cast<float>(camComponent.viewPortWidth),
                                    static_cast<float>(camComponent.viewPortHeight),
                                    0.0f,
                                    camComponent.zNear,
                                    camComponent.zFar);
        }

        // Vulkan projection correction
        projection[1][1] *= invertY ? -1 : 1;

        return projection;
    }
} // namespace vultra