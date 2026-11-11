#pragma once
#include <vultra/core/base/error.hpp>
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

        glm::mat4                                     poseMatrix() const;
        [[nodiscard]] std::expected<glm::mat4, Error> viewProjection(float nearZ = 0.05f, float farZ = 100.0f) const;
    };

    struct XRFrame
    {
        bool                 begun        = false;
        bool                 shouldRender = false;
        XrTime               displayTime  = 0;
        std::array<XREye, 2> eyes;
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

        bool exiting() const
        {
            return m_Exiting;
        }

        VriFormat format() const
        {
            return m_Format;
        }

    private:
        OpenXRSession(OpenXRSystem& system, Device& device);
        [[nodiscard]] std::expected<void, Error> initialize();
        [[nodiscard]] std::expected<void, Error> pollEvents();
        void                                     cleanup() noexcept;
        OpenXRSystem&                            m_System;
        Device&                                  m_Device;
        XrSession                                m_Session = XR_NULL_HANDLE;
        XrSpace                                  m_Space   = XR_NULL_HANDLE;
        XrEnvironmentBlendMode                   m_Blend   = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        VriFormat                                m_Format  = VriFormat_Unknown;

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
