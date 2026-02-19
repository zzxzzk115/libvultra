#pragma once

#include "vultra/function/openxr/xr_input_profile.hpp"

#include <vulkan/vulkan.hpp>

// OpenXR Headers
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra::openxr
{
    constexpr uint32_t kHandCount = 2u;

    /**
     * Generic action type understood by the engine.
     * Actual device capability is described by XRInputProfile.
     */
    enum class XRInputType
    {
        eBoolean,
        eFloat,
        eVector2,
        ePose
    };

    /**
     * Unified value container for all action types.
     */
    struct XRInputValue
    {
        bool       bValue = false;
        float      fValue = 0.0f;
        XrVector2f v2Value {};
        XrPosef    pose {};

        bool active = false;
    };

    /**
     * Engine level OpenXR input system.
     *
     * This class:
     *  - Manages OpenXR ActionSet and Action states
     *  - Does NOT assume any specific controller layout
     *  - Relies on XRInputProfile provided by the application
     */
    class XRInput
    {
    public:
        XRInput(XrInstance instance, XrSession session);
        ~XRInput();

        // Synchronize all actions for the current frame
        bool sync(XrSpace baseSpace, XrTime time);

        XrActionSet getActionSet() const { return m_ActionSet; }

        /**
         * Apply an interaction profile suggested by the application.
         * Multiple profiles can be applied; the runtime selects the active one.
         */
        void applyProfile(XrInstance instance, const XRInputProfile& profile);

        /**
         * Query which interaction profile is currently active.
         * This may change at runtime when devices are switched.
         */
        std::string getActiveProfile(XrInstance instance, size_t hand = 0) const;

        // Query by action name (device-agnostic)
        const XRInputValue& get(const std::string& actionName, size_t hand) const;

        bool       getBool(const std::string& actionName, size_t hand) const;
        float      getFloat(const std::string& actionName, size_t hand) const;
        XrVector2f getVector2(const std::string& actionName, size_t hand) const;
        XrPosef    getPose(const std::string& actionName, size_t hand) const;

    private:
        struct ActionData
        {
            XRInputType type;
            XrAction    action {XR_NULL_HANDLE};

            std::array<XRInputValue, kHandCount> values {};

            // Pose actions own spaces for left/right hands
            std::array<XrSpace, kHandCount> spaces {XR_NULL_HANDLE, XR_NULL_HANDLE};
        };

        /**
         * Register minimal engine actions.
         * The engine should only register actions it understands,
         * not device-specific buttons.
         */
        void registerDefaultActions();

        void registerAction(const std::string& name, XRInputType type);

        std::unordered_map<std::string, ActionData> m_Actions;

        // Subaction paths for left/right hands
        std::vector<XrPath> m_SubactionPaths;

        XrActionSet m_ActionSet {XR_NULL_HANDLE};
        XrSession   m_Session {XR_NULL_HANDLE};
    };
} // namespace vultra::openxr