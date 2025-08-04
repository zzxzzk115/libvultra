#pragma once

#include <vulkan/vulkan.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

// OpenXR Headers
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vultra
{
    namespace openxr
    {
        class XRControllers final
        {
        public:
            XRControllers(XrInstance instance, XrSession session);
            ~XRControllers();

            bool sync(XrSpace space, XrTime time);

            XrActionSet getActionSet() const { return m_ActionSet; }
            glm::mat4   getPose(size_t controllerIndex) const { return m_Poses.at(controllerIndex); }
            float       getFlySpeed(size_t controllerIndex) const { return m_FlySpeeds.at(controllerIndex); }

        private:
            XrSession            m_Session = nullptr;
            std::vector<XrPath>  m_Paths;
            std::vector<XrSpace> m_Spaces;

            std::vector<glm::mat4> m_Poses;
            std::vector<float>     m_FlySpeeds;

            XrActionSet m_ActionSet  = nullptr;
            XrAction    m_PoseAction = nullptr, m_FlyAction = nullptr;
        };
    } // namespace openxr
} // namespace vultra