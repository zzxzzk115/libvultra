#include "vultra/function/openxr/xr_input.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/function/openxr/xr_helper.hpp"

namespace vultra::openxr
{
    // =======================================================

    XRInput::XRInput(XrInstance instance, XrSession session) : m_Session(session)
    {
        // ----- Create Action Set -----
        XrActionSetCreateInfo info {};
        info.type = XR_TYPE_ACTION_SET_CREATE_INFO;

        const std::string actionSetName = "vultra_input";
        const std::string localizedName = "Vultra Input";

        memcpy(info.actionSetName, actionSetName.data(), actionSetName.length() + 1);

        memcpy(info.localizedActionSetName, localizedName.data(), localizedName.length() + 1);

        XrResult result = xrCreateActionSet(instance, &info, &m_ActionSet);

        if (XR_FAILED(result))
        {
            VULTRA_CORE_ERROR("[XRInput] Failed to create action set");
            throw std::runtime_error("Failed to create action set");
        }

        // ----- Subaction paths for left/right hands -----
        m_SubactionPaths.resize(kHandCount);

        m_SubactionPaths[0] = xrutils::stringToPath(instance, "/user/hand/left");

        m_SubactionPaths[1] = xrutils::stringToPath(instance, "/user/hand/right");

        // Register minimal engine actions (device-agnostic)
        registerDefaultActions();

        // Apply default interaction profiles
        {
            XRInputProfile profile("/interaction_profiles/khr/simple_controller");
            profile.add("select", "/user/hand/left/input/select/click")
                .add("select", "/user/hand/right/input/select/click")
                .add("aim_pose", "/user/hand/left/input/aim/pose")
                .add("aim_pose", "/user/hand/right/input/aim/pose")
                .add("grip_pose", "/user/hand/left/input/grip/pose")
                .add("grip_pose", "/user/hand/right/input/grip/pose");

            applyProfile(instance, profile);
        }
    }

    // =======================================================

    XRInput::~XRInput()
    {
        // Destroy pose spaces
        for (auto& [name, a] : m_Actions)
        {
            if (a.type == XRInputType::ePose)
            {
                for (auto& space : a.spaces)
                {
                    if (space != XR_NULL_HANDLE)
                    {
                        xrDestroySpace(space);
                        space = XR_NULL_HANDLE;
                    }
                }
            }
        }

        // Destroy actions
        for (auto& [_, a] : m_Actions)
        {
            if (a.action != XR_NULL_HANDLE)
            {
                xrDestroyAction(a.action);
                a.action = XR_NULL_HANDLE;
            }
        }

        // Destroy action set
        if (m_ActionSet != XR_NULL_HANDLE)
        {
            xrDestroyActionSet(m_ActionSet);
            m_ActionSet = XR_NULL_HANDLE;
        }
    }

    // =======================================================

    void XRInput::registerDefaultActions()
    {
        // Engine minimal guaranteed actions.
        // Additional device-specific actions are provided by XRInputProfile.

        registerAction("select", XRInputType::eFloat);
        registerAction("aim_pose", XRInputType::ePose);
        registerAction("grip_pose", XRInputType::ePose);
    }

    // =======================================================

    void XRInput::registerAction(const std::string& name, XRInputType type)
    {
        ActionData data {};
        data.type = type;

        XrActionType xrType = XR_ACTION_TYPE_BOOLEAN_INPUT;

        switch (type)
        {
            case XRInputType::eBoolean:
                xrType = XR_ACTION_TYPE_BOOLEAN_INPUT;
                break;

            case XRInputType::eFloat:
                xrType = XR_ACTION_TYPE_FLOAT_INPUT;
                break;

            case XRInputType::eVector2:
                xrType = XR_ACTION_TYPE_VECTOR2F_INPUT;
                break;

            case XRInputType::ePose:
                xrType = XR_ACTION_TYPE_POSE_INPUT;
                break;
        }

        if (!xrutils::createAction(m_ActionSet, m_SubactionPaths, name, name, xrType, data.action))
        {
            VULTRA_CORE_ERROR("[XRInput] Failed to create action: {}", name);
            throw std::runtime_error("Failed to create action: " + name);
        }

        // Create spaces for pose actions
        if (type == XRInputType::ePose)
        {
            for (uint32_t i = 0; i < kHandCount; ++i)
            {
                XrActionSpaceCreateInfo s {};
                s.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;

                s.action            = data.action;
                s.poseInActionSpace = xrutils::makeIdentity();
                s.subactionPath     = m_SubactionPaths[i];

                XrResult result = xrCreateActionSpace(m_Session, &s, &data.spaces[i]);

                if (XR_FAILED(result))
                {
                    VULTRA_CORE_ERROR("[XRInput] Failed to create pose space: {}", name);

                    throw std::runtime_error("Failed to create pose space: " + name);
                }
            }
        }

        m_Actions[name] = std::move(data);
    }

    // =======================================================

    void XRInput::applyProfile(XrInstance instance, const XRInputProfile& profile)
    {
        std::vector<XrActionSuggestedBinding> binds;

        for (const auto& b : profile.getBindings())
        {
            auto it = m_Actions.find(b.actionName);
            if (it == m_Actions.end())
                continue;

            XrPath p = xrutils::stringToPath(instance, b.path);

            if (p == XR_NULL_PATH)
                continue;

            binds.push_back({it->second.action, p});
        }

        if (binds.empty())
            return;

        XrInteractionProfileSuggestedBinding s {};
        s.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;

        s.interactionProfile = xrutils::stringToPath(instance, profile.getProfilePath());

        s.suggestedBindings      = binds.data();
        s.countSuggestedBindings = static_cast<uint32_t>(binds.size());

        XrResult r = xrSuggestInteractionProfileBindings(instance, &s);

        if (XR_FAILED(r))
        {
            VULTRA_CORE_ERROR("[XRInput] Suggest bindings failed: {}", xrutils::resultToString(instance, r));
        }
        else
        {
            VULTRA_CORE_INFO("[XRInput] Suggested bindings for profile: {}", profile.getProfilePath());
            for (const auto& b : profile.getBindings())
            {
                VULTRA_CORE_INFO("    Action: {}, Path: {}", b.actionName, b.path);
            }
        }
    }

    // =======================================================

    std::string XRInput::getActiveProfile(XrInstance instance, size_t hand) const
    {
        if (hand >= m_SubactionPaths.size())
            return {};

        XrInteractionProfileState state {};
        state.type = XR_TYPE_INTERACTION_PROFILE_STATE;

        XrResult r = xrGetCurrentInteractionProfile(m_Session, m_SubactionPaths[hand], &state);

        if (XR_FAILED(r))
            return {};

        char buf[XR_MAX_PATH_LENGTH];
        xrPathToString(instance, state.interactionProfile, sizeof(buf), nullptr, buf);

        return std::string(buf);
    }

    // =======================================================

    bool XRInput::sync(XrSpace baseSpace, XrTime time)
    {
        XrActiveActionSet set {};
        set.actionSet     = m_ActionSet;
        set.subactionPath = XR_NULL_PATH;

        XrActionsSyncInfo info {};
        info.type                  = XR_TYPE_ACTIONS_SYNC_INFO;
        info.countActiveActionSets = 1;
        info.activeActionSets      = &set;

        if (XR_FAILED(xrSyncActions(m_Session, &info)))
        {
            VULTRA_CORE_ERROR("[XRInput] xrSyncActions failed");
            return false;
        }

        for (auto& [name, data] : m_Actions)
        {
            for (uint32_t i = 0; i < kHandCount; ++i)
            {
                const XrPath& path = m_SubactionPaths[i];

                switch (data.type)
                {
                    case XRInputType::eBoolean: {
                        XrActionStateBoolean s {};
                        s.type = XR_TYPE_ACTION_STATE_BOOLEAN;

                        xrutils::updateActionStateBoolean(m_Session, data.action, path, s);

                        data.values[i].bValue = s.currentState;
                        data.values[i].active = s.isActive;
                        break;
                    }

                    case XRInputType::eFloat: {
                        XrActionStateFloat s {};
                        s.type = XR_TYPE_ACTION_STATE_FLOAT;

                        xrutils::updateActionStateFloat(m_Session, data.action, path, s);

                        data.values[i].fValue = s.currentState;
                        data.values[i].active = s.isActive;
                        break;
                    }

                    case XRInputType::eVector2: {
                        XrActionStateVector2f s {};
                        s.type = XR_TYPE_ACTION_STATE_VECTOR2F;

                        xrutils::updateActionStateVector2(m_Session, data.action, path, s);

                        data.values[i].v2Value = s.currentState;
                        data.values[i].active  = s.isActive;
                        break;
                    }

                    case XRInputType::ePose: {
                        XrActionStatePose s {};
                        s.type = XR_TYPE_ACTION_STATE_POSE;

                        xrutils::updateActionStatePose(m_Session, data.action, path, s);

                        data.values[i].active = false;

                        if (s.isActive)
                        {
                            XrSpaceLocation loc {};
                            loc.type = XR_TYPE_SPACE_LOCATION;

                            XrResult r = xrLocateSpace(data.spaces[i], baseSpace, time, &loc);

                            if (XR_SUCCEEDED(r))
                            {
                                constexpr XrSpaceLocationFlags check =
                                    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
                                    XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;

                                if ((loc.locationFlags & check) == check)
                                {
                                    data.values[i].pose   = loc.pose;
                                    data.values[i].active = true;
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }

        return true;
    }

    // =======================================================

    const XRInputValue& XRInput::get(const std::string& actionName, size_t hand) const
    {
        return m_Actions.at(actionName).values[hand];
    }

    // =======================================================

    bool XRInput::getBool(const std::string& actionName, size_t hand) const { return get(actionName, hand).bValue; }

    float XRInput::getFloat(const std::string& actionName, size_t hand) const { return get(actionName, hand).fValue; }

    XrVector2f XRInput::getVector2(const std::string& actionName, size_t hand) const
    {
        return get(actionName, hand).v2Value;
    }

    XrPosef XRInput::getPose(const std::string& actionName, size_t hand) const { return get(actionName, hand).pose; }

} // namespace vultra::openxr