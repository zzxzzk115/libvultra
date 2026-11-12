#pragma once
#include <vultra/core/base/error.hpp>
#include <vultra/core/math/ray.hpp>
#include <vultra/gpu/resources.hpp>

#include <glm/mat4x4.hpp>
#include <openxr/openxr.h>

#include <array>
#include <expected>
#include <memory>
#include <vector>

namespace vultra
{
    // Lifetime: OpenXRSystem -> Device(system.creationHooks()) -> OpenXRSession.
    // The device keeps the creation hooks and the session keeps a reference, so the system does not move.
    class OpenXRSystem
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<OpenXRSystem>, Error>
        create(const char* applicationName = "Vultra");
        ~OpenXRSystem();
        OpenXRSystem(const OpenXRSystem&)            = delete;
        OpenXRSystem& operator=(const OpenXRSystem&) = delete;
        const void*   creationHooks() const;
        XrInstance    instance() const;
        XrSystemId    system() const;

    private:
        struct Impl;
        OpenXRSystem();
        std::unique_ptr<Impl> m_Impl;
    };

    struct XREye
    {
        Texture* color = nullptr;
        XrView   view {XR_TYPE_VIEW};

        glm::mat4 poseMatrix() const;
        // The eye as a camera of the tracking space. `origin` places that space in the world: the pose of the
        // tracking origin there.
        [[nodiscard]] std::expected<RenderCamera, Error>
        camera(float nearZ = 0.05f, float farZ = 100.0f, const glm::mat4& origin = glm::mat4(1)) const;
        [[nodiscard]] std::expected<glm::mat4, Error> viewProjection(float nearZ = 0.05f, float farZ = 100.0f) const;
    };

    // One hand's controller at the frame's display time, in the tracking space of the eye views.
    struct XRHand
    {
        // False without a tracked controller or while the session lacks input focus; the other members
        // then keep their defaults.
        bool      active = false;
        glm::mat4 grip {1};    // Where the hand holds the controller.
        glm::mat4 aim {1};     // Pointing pose; -Z is the pointing direction.
        float     select  = 0; // Trigger, 0 released to 1 pressed.
        float     squeeze = 0; // Grip button, 0 to 1; stays 0 on a controller without one.

        // From the aim pose along its pointing direction, with a unit direction.
        Ray pointer() const;
    };

    struct XRFrame
    {
        bool                  begun        = false;
        bool                  shouldRender = false;
        XrTime                displayTime  = 0;
        std::array<XREye, 2>  eyes;
        std::array<XRHand, 2> hands; // Left, right. Located in every begun frame, rendered or not.
        // Located eye midpoint in the runtime's tracking space, retaining the session head orientation.
        [[nodiscard]] std::expected<glm::mat4, Error> headPose() const;
    };

    // Eye textures are handed out by pointer, so the session does not move.
    class OpenXRSession
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<OpenXRSession>, Error> create(OpenXRSystem& system,
                                                                                         Device&       device);
        ~OpenXRSession();
        OpenXRSession(const OpenXRSession&)            = delete;
        OpenXRSession& operator=(const OpenXRSession&) = delete;

        [[nodiscard]] std::expected<XRFrame, Error> beginFrame();
        // Call after rendering each eye; transitions to the layout the XR runtime requires.
        void prepareSubmit(VriCommandBuffer* cmd);
        // Pair every begun frame, including shouldRender=false. Call after GPU completion.
        [[nodiscard]] std::expected<void, Error> endFrame();
        // Asks the runtime to end the session. Keep beginning and ending frames until exiting(): the runtime
        // stops the session through its events, and destroying a session that still runs skips that.
        [[nodiscard]] std::expected<void, Error> requestExit();

        bool exiting() const
        {
            return m_Exiting;
        }

        VriFormat format() const
        {
            return m_Format;
        }

        // The image size of one eye, 0 left and 1 right.
        Extent eyeSize(size_t eye) const
        {
            return m_Eyes.at(eye).size;
        }

    private:
        OpenXRSession(OpenXRSystem& system, Device& device);
        [[nodiscard]] std::expected<void, Error> initialize();
        [[nodiscard]] std::expected<void, Error> initializeInput();
        [[nodiscard]] std::expected<void, Error> pollEvents();
        [[nodiscard]] std::expected<void, Error> locateHands(XrTime time);
        void                                     cleanup() noexcept;
        OpenXRSystem&                            m_System;
        Device&                                  m_Device;
        XrSession                                m_Session = XR_NULL_HANDLE;
        XrSpace                                  m_Space   = XR_NULL_HANDLE;
        XrEnvironmentBlendMode                   m_Blend   = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        VriFormat                                m_Format  = VriFormat_Unknown;

        // One action set with left and right hand subactions. The actions are destroyed with the set.
        XrActionSet            m_ActionSet     = XR_NULL_HANDLE;
        XrAction               m_GripAction    = XR_NULL_HANDLE;
        XrAction               m_AimAction     = XR_NULL_HANDLE;
        XrAction               m_SelectAction  = XR_NULL_HANDLE;
        XrAction               m_SqueezeAction = XR_NULL_HANDLE;
        std::array<XrPath, 2>  m_HandPaths {};
        std::array<XrSpace, 2> m_GripSpaces {};
        std::array<XrSpace, 2> m_AimSpaces {};

        struct EyeSwapchain
        {
            XrSwapchain              handle = XR_NULL_HANDLE;
            Extent                   size {};
            uint32_t                 index    = 0;
            bool                     acquired = false;
            bool                     waited   = false;
            std::vector<VriTexture*> wrappers;
            // Reserved once; XRFrame points into it.
            std::vector<Texture> textures;
        };

        std::array<EyeSwapchain, 2> m_Eyes;
        bool                        m_Running = false;
        bool                        m_Exiting = false;
        XRFrame                     m_Frame;
    };
} // namespace vultra
