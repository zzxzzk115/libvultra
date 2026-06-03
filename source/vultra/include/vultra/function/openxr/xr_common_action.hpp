#pragma once

#include "vultra/function/openxr/xr_input.hpp"

#include <memory>

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

            XRInput*                 getInput() { return m_Input.get(); }
            const ext::XREyeTracker* getEyeTracker() const { return m_EyeTracker.get(); }

        private:
            XrInstance m_XrInstance = XR_NULL_HANDLE;
            XrSession  m_Session    = XR_NULL_HANDLE;

            std::unique_ptr<XRInput>           m_Input {nullptr};
            std::unique_ptr<ext::XREyeTracker> m_EyeTracker;
        };
    } // namespace openxr
} // namespace vultra
