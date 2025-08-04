#pragma once

#include <vulkan/vulkan.hpp>

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vultra
{
    namespace openxr
    {
        class XRControllers;

        namespace ext
        {
            class XREyeTracker;
        }

        class XRCommonAction final
        {
        public:
            XRCommonAction(XrInstance instance, XrSession session, bool supportEyetracking);
            ~XRCommonAction();

            bool sync(XrSpace space, XrTime time);

            const XRControllers*     getControllers() const { return m_Controllers; }
            const ext::XREyeTracker* getEyeTracker() const { return m_EyeTracker; }

        private:
            XrInstance m_XrInstance = nullptr;
            XrSession  m_Session    = nullptr;

            XRControllers*     m_Controllers = nullptr;
            ext::XREyeTracker* m_EyeTracker  = nullptr;
        };
    } // namespace openxr
} // namespace vultra