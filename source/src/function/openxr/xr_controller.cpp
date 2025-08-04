#include "vultra/function/openxr/xr_controller.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/function/openxr/xr_helper.hpp"

namespace
{
    constexpr size_t controllerCount = 2u;

    const std::string actionSetName          = "actionset";
    const std::string localizedActionSetName = "Actions";
} // namespace

namespace vultra
{
    namespace openxr
    {
        XRControllers::XRControllers(XrInstance instance, XrSession session) : m_Session(session)
        {
            // Create an action set
            XrActionSetCreateInfo actionSetCreateInfo {};
            actionSetCreateInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;

            memcpy(actionSetCreateInfo.actionSetName, actionSetName.data(), actionSetName.length() + 1u);
            memcpy(actionSetCreateInfo.localizedActionSetName,
                   localizedActionSetName.data(),
                   localizedActionSetName.length() + 1u);

            XrResult result = xrCreateActionSet(instance, &actionSetCreateInfo, &m_ActionSet);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[Controllers] Failed to create action set");
                throw std::runtime_error("Failed to create action set");
            }

            // Create paths
            m_Paths.resize(controllerCount);
            m_Paths.at(0u) = xrutils::stringToPath(instance, "/user/hand/left");
            m_Paths.at(1u) = xrutils::stringToPath(instance, "/user/hand/right");

            // Create actions
            if (!xrutils::createAction(
                    m_ActionSet, m_Paths, "handpose", "Hand Pose", XR_ACTION_TYPE_POSE_INPUT, m_PoseAction))
            {
                VULTRA_CORE_ERROR("[Controllers] Failed to create action set");
                throw std::runtime_error("Failed to create action set");
            }

            if (!xrutils::createAction(m_ActionSet, m_Paths, "fly", "Fly", XR_ACTION_TYPE_FLOAT_INPUT, m_FlyAction))
            {
                VULTRA_CORE_ERROR("[Controllers] Failed to create action set");
                throw std::runtime_error("Failed to create action set");
            }

            // Create spaces
            m_Spaces.resize(controllerCount);
            for (size_t controllerIndex = 0u; controllerIndex < controllerCount; ++controllerIndex)
            {
                const XrPath& path = m_Paths.at(controllerIndex);

                XrActionSpaceCreateInfo actionSpaceCreateInfo {};
                actionSpaceCreateInfo.type              = XR_TYPE_ACTION_SPACE_CREATE_INFO;
                actionSpaceCreateInfo.action            = m_PoseAction;
                actionSpaceCreateInfo.poseInActionSpace = xrutils::makeIdentity();
                actionSpaceCreateInfo.subactionPath     = path;

                result = xrCreateActionSpace(session, &actionSpaceCreateInfo, &m_Spaces.at(controllerIndex));
                if (XR_FAILED(result))
                {
                    VULTRA_CORE_ERROR("[Controllers] Failed to create action set");
                    throw std::runtime_error("Failed to create action set");
                }
            }

            // Suggest simple controller binding (generic)
            const std::array<XrActionSuggestedBinding, 4u> bindings = {
                {{m_PoseAction, xrutils::stringToPath(instance, "/user/hand/left/input/aim/pose")},
                 {m_PoseAction, xrutils::stringToPath(instance, "/user/hand/right/input/aim/pose")},
                 {m_FlyAction, xrutils::stringToPath(instance, "/user/hand/left/input/select/click")},
                 {m_FlyAction, xrutils::stringToPath(instance, "/user/hand/right/input/select/click")}}};

            XrInteractionProfileSuggestedBinding interactionProfileSuggestedBinding {};
            interactionProfileSuggestedBinding.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
            interactionProfileSuggestedBinding.interactionProfile =
                xrutils::stringToPath(instance, "/interaction_profiles/khr/simple_controller");
            interactionProfileSuggestedBinding.suggestedBindings      = bindings.data();
            interactionProfileSuggestedBinding.countSuggestedBindings = static_cast<uint32_t>(bindings.size());

            result = xrSuggestInteractionProfileBindings(instance, &interactionProfileSuggestedBinding);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[Controllers] Failed to create interaction profile binding {}",
                                  xrutils::resultToString(instance, result));
                throw std::runtime_error("Failed to create interaction profile binding");
            }

            m_Poses.resize(controllerCount);
            m_FlySpeeds.resize(controllerCount);
        }

        XRControllers::~XRControllers()
        {
            for (const XrSpace& space : m_Spaces)
            {
                xrDestroySpace(space);
            }

            if (m_FlyAction)
            {
                xrDestroyAction(m_FlyAction);
            }

            if (m_PoseAction)
            {
                xrDestroyAction(m_PoseAction);
            }

            if (m_ActionSet)
            {
                xrDestroyActionSet(m_ActionSet);
            }
        }

        bool XRControllers::sync(XrSpace space, XrTime time)
        {
            // Sync the actions
            XrActiveActionSet activeActionSet {};
            activeActionSet.actionSet     = m_ActionSet;
            activeActionSet.subactionPath = XR_NULL_PATH; // Wildcard for all

            XrActionsSyncInfo actionsSyncInfo {};
            actionsSyncInfo.type                  = XR_TYPE_ACTIONS_SYNC_INFO;
            actionsSyncInfo.countActiveActionSets = 1u;
            actionsSyncInfo.activeActionSets      = &activeActionSet;

            XrResult result = xrSyncActions(m_Session, &actionsSyncInfo);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[Controllers] Failed to sync action set");
                return false;
            }

            // Update the actions
            for (size_t controllerIndex = 0u; controllerIndex < controllerCount; ++controllerIndex)
            {
                const XrPath& path = m_Paths.at(controllerIndex);

                // Pose
                XrActionStatePose poseState {};
                poseState.type = XR_TYPE_ACTION_STATE_POSE;

                if (!xrutils::updateActionStatePose(m_Session, m_PoseAction, path, poseState))
                {
                    VULTRA_CORE_ERROR("[Controllers] Failed to update action state");
                    return false;
                }

                if (poseState.isActive)
                {
                    XrSpaceLocation spaceLocation {};
                    spaceLocation.type = XR_TYPE_SPACE_LOCATION;

                    result = xrLocateSpace(m_Spaces.at(controllerIndex), space, time, &spaceLocation);
                    if (XR_FAILED(result))
                    {
                        VULTRA_CORE_ERROR("[Controllers] Failed to locate space");
                        return false;
                    }

                    // Check that the position and orientation are valid and tracked
                    constexpr XrSpaceLocationFlags checkFlags =
                        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
                        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
                    if ((spaceLocation.locationFlags & checkFlags) == checkFlags)
                    {
                        m_Poses.at(controllerIndex) = xrutils::poseToMatrix(spaceLocation.pose);
                    }
                }

                // Fly speed
                XrActionStateFloat flySpeedState {};
                flySpeedState.type = XR_TYPE_ACTION_STATE_FLOAT;

                if (!xrutils::updateActionStateFloat(m_Session, m_FlyAction, path, flySpeedState))
                {
                    VULTRA_CORE_ERROR("[Controllers] Failed to update action state");
                    return false;
                }

                if (flySpeedState.isActive)
                {
                    m_FlySpeeds.at(controllerIndex) = flySpeedState.currentState;
                }
            }

            return true;
        }
    } // namespace openxr
} // namespace vultra