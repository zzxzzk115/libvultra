#pragma once
#include <vultra/drivers/rhi/resources.hpp>

#include <glm/mat4x4.hpp>
#include <openxr/openxr.h>

#include <array>
#include <memory>
#include <vector>

namespace vultra
{
#if defined(__ANDROID__)
    // Borrowed JavaVM and Activity references from the Android application entry. Keep the Activity
    // alive throughout the OpenXRSystem lifetime; JNI local references cannot cross threads.
    struct OpenXRAndroidContext
    {
        void* javaVm   = nullptr;
        void* activity = nullptr;
    };
#endif

    // Lifetime: OpenXRSystem -> Device(system.creationHooks()) -> OpenXRSession.
    class OpenXRSystem
    {
    public:
#if defined(__ANDROID__)
        static std::expected<std::unique_ptr<OpenXRSystem>, std::string> create(OpenXRAndroidContext context,
                                                                                const char* applicationName = "Vultra");
#else
        static std::expected<std::unique_ptr<OpenXRSystem>, std::string> create(const char* applicationName = "Vultra");
#endif
        ~OpenXRSystem();
        OpenXRSystem(const OpenXRSystem&)                               = delete;
        OpenXRSystem&                    operator=(const OpenXRSystem&) = delete;
        const void*                      creationHooks() const;
        std::expected<void, std::string> validateDevice(Device& device) const;
        XrInstance                       instance() const;
        XrSystemId                       system() const;

    private:
        struct Impl;

        struct Uninitialized
        {};

        explicit OpenXRSystem(Uninitialized);
#if defined(__ANDROID__)
        std::expected<void, std::string> initialize(OpenXRAndroidContext context, const char* applicationName);
#else
        std::expected<void, std::string> initialize(const char* applicationName);
#endif
        std::unique_ptr<Impl> m_Impl;
    };

    struct XREye
    {
        Texture*                              color = nullptr;
        XrView                                view {XR_TYPE_VIEW};
        glm::mat4                             poseMatrix() const;
        std::expected<glm::mat4, std::string> viewProjection(float nearZ = 0.05f, float farZ = 100.0f) const;
    };

    struct XRFrame
    {
        bool                 begun        = false;
        bool                 shouldRender = false;
        XrTime               displayTime  = 0;
        std::array<XREye, 2> eyes;
        // Located eye midpoint in the runtime's tracking space, retaining the session head orientation.
        std::expected<glm::mat4, std::string> headPose() const;
    };

    class OpenXRSession
    {
    public:
        static std::expected<std::unique_ptr<OpenXRSession>, std::string> create(OpenXRSystem& system, Device& device);
        ~OpenXRSession();
        OpenXRSession(const OpenXRSession&)                                 = delete;
        OpenXRSession&                      operator=(const OpenXRSession&) = delete;
        std::expected<XRFrame, std::string> beginFrame();
        // Call after rendering each eye; transitions to the layout the XR runtime requires.
        void prepareSubmit(VriCommandBuffer* cmd);
        // Pair every begun frame, including shouldRender=false. Call after GPU completion.
        std::expected<void, std::string> endFrame();
        // Request a graceful exit between frames, then keep calling beginFrame/endFrame until
        // exiting() is true. The runtime may require additional frames before it stops.
        std::expected<void, std::string> requestExit();

        bool exiting() const
        {
            return m_Exiting;
        }

        // True only after a requested exit reaches XR_SESSION_STATE_EXITING, not on session loss.
        bool exitRequestCompleted() const
        {
            return m_ExitCompleted;
        }

        VriFormat format() const
        {
            return m_Format;
        }

    private:
        struct Uninitialized
        {};

        OpenXRSession(OpenXRSystem& system, Device& device, Uninitialized);
        std::expected<void, std::string> initialize();
        std::expected<void, std::string> pollEventsExpected();
        void                             cleanup() noexcept;
        OpenXRSystem&                    m_System;
        Device&                          m_Device;
        XrSession                        m_Session = XR_NULL_HANDLE;
        XrSpace                          m_Space   = XR_NULL_HANDLE;
        XrEnvironmentBlendMode           m_Blend   = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        VriFormat                        m_Format  = VriFormat_Unknown;

        struct EyeSwapchain
        {
            XrSwapchain                           handle = XR_NULL_HANDLE;
            Extent                                size {};
            uint32_t                              index    = 0;
            bool                                  acquired = false;
            bool                                  waited   = false;
            std::vector<VriTexture*>              wrappers;
            std::vector<std::unique_ptr<Texture>> textures;
        };

        std::array<EyeSwapchain, 2> m_Eyes;
        bool                        m_Running       = false;
        bool                        m_Exiting       = false;
        bool                        m_ExitRequested = false;
        bool                        m_ExitCompleted = false;
        bool                        m_Lost          = false;
        XRFrame                     m_Frame;
    };
} // namespace vultra
