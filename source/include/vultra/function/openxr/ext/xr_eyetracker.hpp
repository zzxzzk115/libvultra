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
                XrInstance m_XrInstance = nullptr;
                XrSession  m_Session    = nullptr;

                XrPath m_InteractionProfilePath;
                XrPath m_GazePosePath;

                XrSpace m_GazeActionSpace;

                XrPosef m_GazePose {};

                XrActionSet m_GamePlayActionSet = nullptr;
                XrAction    m_UserIntentAction  = nullptr;
            };
        } // namespace ext
    } // namespace openxr
} // namespace vultra