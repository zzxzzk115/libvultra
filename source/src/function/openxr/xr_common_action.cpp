#include "vultra/function/openxr/xr_common_action.hpp"
#include "vultra/function/openxr/ext/xr_eyetracker.hpp"
#include "vultra/function/openxr/xr_controller.hpp"
#include "vultra/function/openxr/xr_helper.hpp"

namespace vultra
{
    namespace openxr
    {
        XRCommonAction::XRCommonAction(XrInstance instance, XrSession session, bool supportEyetracking) :
            m_XrInstance(instance), m_Session(session)
        {
            m_Controllers = new XRControllers(instance, session);
            if (supportEyetracking)
            {
                // Create the eye tracker if supported
                m_EyeTracker = new ext::XREyeTracker(instance, session);
            }

            // Attach action sets
            std::vector actionSets = {m_Controllers->getActionSet()};
            if (supportEyetracking)
            {
                actionSets.push_back(m_EyeTracker->getActionSet());
            }
            XrSessionActionSetsAttachInfo attachInfo {.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
            attachInfo.countActionSets = static_cast<uint32_t>(actionSets.size());
            attachInfo.actionSets      = actionSets.data();
            OPENXR_CHECK(xrAttachSessionActionSets(session, &attachInfo), "Failed to attach session action sets");
        }

        XRCommonAction::~XRCommonAction()
        {
            delete m_Controllers;
            delete m_EyeTracker;
        }

        bool XRCommonAction::sync(XrSpace space, XrTime time)
        {
            if (m_Controllers)
            {
                if (!m_Controllers->sync(space, time))
                {
                    return false;
                }
            }
            else
            {
                return false;
            }

            if (m_EyeTracker)
            {
                if (!m_EyeTracker->sync(space, time))
                {
                    return false;
                }
            }

            return true;
        }
    } // namespace openxr
} // namespace vultra