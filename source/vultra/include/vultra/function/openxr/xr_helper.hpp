#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

// OpenXR Headers
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

inline void OpenXRDebugBreak()
{
    DEBUG_BREAK();
}

inline const char* GetXRErrorString(XrInstance xrInstance, XrResult result)
{
    static char string[XR_MAX_RESULT_STRING_SIZE];
    xrResultToString(xrInstance, result, string);
    return string;
}

#define OPENXR_CHECK(x, y) \
    { \
        XrResult result = (x); \
        if (!XR_SUCCEEDED(result)) \
        { \
            VULTRA_CORE_ERROR("[OpenXR] {0} ({1}) {2}", int(result), GetXRErrorString(m_XrInstance, result), y); \
            OpenXRDebugBreak(); \
        } \
    }

namespace xrutils
{
    inline XrPosef makeIdentity()
    {
        XrPosef identity;
        identity.position    = {0.0f, 0.0f, 0.0f};
        identity.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        return identity;
    }

    inline glm::vec3 toVec3(const XrVector3f& vector) { return glm::vec3(vector.x, vector.y, vector.z); }

    inline glm::quat toQuat(const XrQuaternionf& quaternion)
    {
        return glm::quat(quaternion.w, quaternion.x, quaternion.y, quaternion.z);
    }

    inline XrVector3f fromVec3(const glm::vec3& vector) { return {vector.x, vector.y, vector.z}; }

    inline XrQuaternionf fromQuat(const glm::quat& quaternion)
    {
        return {quaternion.x, quaternion.y, quaternion.z, quaternion.w};
    }

    // Converts an OpenXR pose to a transformation matrix
    inline glm::mat4 poseToMatrix(const XrPosef& pose)
    {
        const glm::mat4 translation =
            glm::translate(glm::mat4(1.0f), glm::vec3(pose.position.x, pose.position.y, pose.position.z));

        const glm::mat4 rotation =
            glm::toMat4(glm::quat(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z));

        return translation * rotation;
    }

    // Creates an OpenXR projection matrix
    inline glm::mat4 createProjectionMatrix(const XrFovf& fov, float nearClip, float farClip)
    {
        const float tanLeft  = glm::tan(fov.angleLeft);
        const float tanRight = glm::tan(fov.angleRight);
        const float tanDown  = glm::tan(fov.angleDown);
        const float tanUp    = glm::tan(fov.angleUp);

        const float tanWidth  = tanRight - tanLeft;
        const float tanHeight = tanUp - tanDown;
        const float depth     = farClip - nearClip;

        glm::mat4 projectionMatrix(0.0f);
        projectionMatrix[0][0] = 2.0f / tanWidth;
        projectionMatrix[1][1] = 2.0f / tanHeight;
        projectionMatrix[2][0] = (tanRight + tanLeft) / tanWidth;
        projectionMatrix[2][1] = (tanUp + tanDown) / tanHeight;
        projectionMatrix[2][2] = -(farClip + nearClip) / depth;
        projectionMatrix[2][3] = -1.0f;
        projectionMatrix[3][2] = -(2.0f * farClip * nearClip) / depth;
        return projectionMatrix;
    }

    // Creates an OpenXR path from a name string
    inline XrPath stringToPath(XrInstance instance, const std::string& string)
    {
        XrPath         path;
        const XrResult result = xrStringToPath(instance, string.c_str(), &path);
        if (XR_FAILED(result))
        {
            return XR_NULL_PATH;
        }

        return path;
    }

    // Creates an OpenXR action with given names, returns false on error
    inline bool createAction(const XrActionSet&         actionSet,
                             const std::vector<XrPath>& paths,
                             const std::string&         actionName,
                             const std::string&         localizedActionName,
                             const XrActionType&        type,
                             XrAction&                  action)
    {
        XrActionCreateInfo actionCreateInfo {};
        actionCreateInfo.type                = XR_TYPE_ACTION_CREATE_INFO;
        actionCreateInfo.actionType          = type;
        actionCreateInfo.countSubactionPaths = static_cast<uint32_t>(paths.size());
        actionCreateInfo.subactionPaths      = paths.data();

        memcpy(actionCreateInfo.actionName, actionName.data(), actionName.length() + 1u);

        memcpy(actionCreateInfo.localizedActionName, localizedActionName.data(), localizedActionName.length() + 1u);

        XrResult result = xrCreateAction(actionSet, &actionCreateInfo, &action);
        return XR_SUCCEEDED(result);
    }

    // -------------------------------------------------------
    // Action State Updates
    // -------------------------------------------------------

    // Updates an action state for a given action and path in pose format
    inline bool updateActionStatePose(const XrSession&   session,
                                      const XrAction&    action,
                                      const XrPath&      path,
                                      XrActionStatePose& state)
    {
        XrActionStateGetInfo actionStateGetInfo {};
        actionStateGetInfo.type          = XR_TYPE_ACTION_STATE_GET_INFO;
        actionStateGetInfo.action        = action;
        actionStateGetInfo.subactionPath = path;

        const XrResult result = xrGetActionStatePose(session, &actionStateGetInfo, &state);

        return XR_SUCCEEDED(result);
    }

    // Updates an action state for a given action and path in float format
    inline bool updateActionStateFloat(const XrSession&    session,
                                       const XrAction&     action,
                                       const XrPath&       path,
                                       XrActionStateFloat& state)
    {
        XrActionStateGetInfo actionStateGetInfo {};
        actionStateGetInfo.type          = XR_TYPE_ACTION_STATE_GET_INFO;
        actionStateGetInfo.action        = action;
        actionStateGetInfo.subactionPath = path;

        const XrResult result = xrGetActionStateFloat(session, &actionStateGetInfo, &state);

        return XR_SUCCEEDED(result);
    }

    // -------------------------------------------------------
    //  ADDED: Boolean Action State
    // -------------------------------------------------------
    inline bool updateActionStateBoolean(const XrSession&      session,
                                         const XrAction&       action,
                                         const XrPath&         path,
                                         XrActionStateBoolean& state)
    {
        XrActionStateGetInfo actionStateGetInfo {};
        actionStateGetInfo.type          = XR_TYPE_ACTION_STATE_GET_INFO;
        actionStateGetInfo.action        = action;
        actionStateGetInfo.subactionPath = path;

        const XrResult result = xrGetActionStateBoolean(session, &actionStateGetInfo, &state);

        return XR_SUCCEEDED(result);
    }

    // -------------------------------------------------------
    //  ADDED: Vector2 Action State
    // -------------------------------------------------------
    inline bool updateActionStateVector2(const XrSession&       session,
                                         const XrAction&        action,
                                         const XrPath&          path,
                                         XrActionStateVector2f& state)
    {
        XrActionStateGetInfo actionStateGetInfo {};
        actionStateGetInfo.type          = XR_TYPE_ACTION_STATE_GET_INFO;
        actionStateGetInfo.action        = action;
        actionStateGetInfo.subactionPath = path;

        const XrResult result = xrGetActionStateVector2f(session, &actionStateGetInfo, &state);

        return XR_SUCCEEDED(result);
    }

    // -------------------------------------------------------

    inline std::string resultToString(XrInstance instance, XrResult result)
    {
        char buffer[XR_MAX_RESULT_STRING_SIZE];
        xrResultToString(instance, result, buffer);
        return std::string(buffer);
    }

} // namespace xrutils
