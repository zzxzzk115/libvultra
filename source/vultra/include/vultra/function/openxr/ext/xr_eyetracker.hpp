#pragma once

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vultra
{
    namespace openxr
    {
        namespace ext
        {
            class XREyeTracker final
            {
            public:
                XREyeTracker(XrInstance instance, XrSession session);
                ~XREyeTracker();

                bool sync(XrSpace space, XrTime time);

                XrActionSet    getActionSet() const { return m_GamePlayActionSet; }
                const XrPosef& getGazePose() const { return m_GazePose; }

            private:
                XrInstance m_XrInstance = XR_NULL_HANDLE;
                XrSession  m_Session    = XR_NULL_HANDLE;

                XrPath m_InteractionProfilePath;
                XrPath m_GazePosePath;

                XrSpace m_GazeActionSpace {XR_NULL_HANDLE};

                XrPosef m_GazePose {};

                XrActionSet m_GamePlayActionSet = XR_NULL_HANDLE;
                XrAction    m_UserIntentAction  = XR_NULL_HANDLE;
            };
        } // namespace ext
    } // namespace openxr
} // namespace vultra
