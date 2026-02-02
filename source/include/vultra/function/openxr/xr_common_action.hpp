#pragma once

#include "vultra/function/openxr/xr_input.hpp"

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
            const ext::XREyeTracker* getEyeTracker() const { return m_EyeTracker; }

        private:
            XrInstance m_XrInstance = nullptr;
            XrSession  m_Session    = nullptr;

            std::unique_ptr<XRInput> m_Input {nullptr};
            ext::XREyeTracker*       m_EyeTracker = nullptr;
        };
    } // namespace openxr
} // namespace vultra