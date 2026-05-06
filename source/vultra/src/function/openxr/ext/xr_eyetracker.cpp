#include "vultra/function/openxr/ext/xr_eyetracker.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/function/openxr/xr_helper.hpp"

namespace vultra
{
    namespace openxr
    {
        namespace ext
        {
            XREyeTracker::XREyeTracker(XrInstance instance, XrSession session) :
                m_XrInstance(instance), m_Session(session)
            {
                // Create action set
                XrActionSetCreateInfo actionSetInfo {.type = XR_TYPE_ACTION_SET_CREATE_INFO};
                strcpy(actionSetInfo.actionSetName, "gameplay");
                strcpy(actionSetInfo.localizedActionSetName, "Gameplay");
                actionSetInfo.priority = 0;
                OPENXR_CHECK(xrCreateActionSet(instance, &actionSetInfo, &m_GamePlayActionSet),
                             "Failed to create action set for gameplay");

                // Create user intent action
                XrActionCreateInfo actionInfo {.type = XR_TYPE_ACTION_CREATE_INFO};
                strcpy(actionInfo.actionName, "user_intent");
                actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
                strcpy(actionInfo.localizedActionName, "User Intent");
                OPENXR_CHECK(xrCreateAction(m_GamePlayActionSet, &actionInfo, &m_UserIntentAction),
                             "Failed to create action for user intent");

                // Create suggested bindings
                OPENXR_CHECK(xrStringToPath(
                                 instance, "/interaction_profiles/ext/eye_gaze_interaction", &m_InteractionProfilePath),
                             "Failed to load eye gaze interaction profile");

                OPENXR_CHECK(xrStringToPath(instance, "/user/eyes_ext/input/gaze_ext/pose", &m_GazePosePath),
                             "Failed to load gaze pose");

                XrActionSuggestedBinding bindings;
                bindings.action  = m_UserIntentAction;
                bindings.binding = m_GazePosePath;

                XrInteractionProfileSuggestedBinding suggestedBindings {
                    .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
                suggestedBindings.interactionProfile     = m_InteractionProfilePath;
                suggestedBindings.suggestedBindings      = &bindings;
                suggestedBindings.countSuggestedBindings = 1;
                OPENXR_CHECK(xrSuggestInteractionProfileBindings(instance, &suggestedBindings),
                             "Failed to get interaction profile bindings");

                XrActionSpaceCreateInfo createActionSpaceInfo {.type = XR_TYPE_ACTION_SPACE_CREATE_INFO};
                createActionSpaceInfo.action            = m_UserIntentAction;
                createActionSpaceInfo.poseInActionSpace = xrutils::makeIdentity();
                OPENXR_CHECK(xrCreateActionSpace(session, &createActionSpaceInfo, &m_GazeActionSpace),
                             "Failed to create action space");
            }

            XREyeTracker::~XREyeTracker()
            {
                if (m_GazeActionSpace)
                {
                    xrDestroySpace(m_GazeActionSpace);
                }

                if (m_UserIntentAction)
                {
                    xrDestroyAction(m_UserIntentAction);
                }

                if (m_GamePlayActionSet)
                {
                    xrDestroyActionSet(m_GamePlayActionSet);
                }
            }

            bool XREyeTracker::sync(XrSpace space, XrTime time)
            {
                // Sync the actions
                XrActiveActionSet activeActionSet {};
                activeActionSet.actionSet     = m_GamePlayActionSet;
                activeActionSet.subactionPath = XR_NULL_PATH; // Wildcard for all

                XrActionsSyncInfo actionsSyncInfo {};
                actionsSyncInfo.type                  = XR_TYPE_ACTIONS_SYNC_INFO;
                actionsSyncInfo.countActiveActionSets = 1u;
                actionsSyncInfo.activeActionSets      = &activeActionSet;

                XrResult result = xrSyncActions(m_Session, &actionsSyncInfo);
                if (XR_FAILED(result))
                {
                    VULTRA_CORE_ERROR("[EyeTracker] Failed to sync action set");
                    return false;
                }

                XrActionStatePose    actionStatePose {.type = XR_TYPE_ACTION_STATE_POSE};
                XrActionStateGetInfo getActionStateInfo {.type = XR_TYPE_ACTION_STATE_GET_INFO};
                getActionStateInfo.action = m_UserIntentAction;
                OPENXR_CHECK(xrGetActionStatePose(m_Session, &getActionStateInfo, &actionStatePose),
                             "Failed to get action state pose");

                if (actionStatePose.isActive)
                {
                    XrEyeGazeSampleTimeEXT eyeGazeSampleTime {.type = XR_TYPE_EYE_GAZE_SAMPLE_TIME_EXT};
                    XrSpaceLocation        gazeLocation {.type = XR_TYPE_SPACE_LOCATION, .next = &eyeGazeSampleTime};
                    OPENXR_CHECK(xrLocateSpace(m_GazeActionSpace, space, time, &gazeLocation),
                                 "Failed to locate gaze location");

                    m_GazePose = gazeLocation.pose;
                }

                return true;
            }
        } // namespace ext
    } // namespace openxr
} // namespace vultra