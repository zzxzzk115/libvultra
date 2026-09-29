#pragma once
#include <vultra/core/rhi/resources.hpp>

#include <glm/mat4x4.hpp>
#include <openxr/openxr.h>

#include <array>
#include <memory>
#include <vector>

namespace vultra
{
    // Lifetime: OpenXRSystem -> Device(system.creationHooks()) -> OpenXRSession.
    class OpenXRSystem
    {
    public:
        explicit OpenXRSystem(const char* applicationName = "Vultra");
        ~OpenXRSystem();
        OpenXRSystem(const OpenXRSystem&)            = delete;
        OpenXRSystem& operator=(const OpenXRSystem&) = delete;
        const void*   creationHooks() const;
        XrInstance    instance() const;
        XrSystemId    system() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    struct XREye
    {
        Texture*  color = nullptr;
        XrView    view {XR_TYPE_VIEW};
        glm::mat4 viewProjection(float nearZ = 0.05f, float farZ = 100.0f) const;
    };

    struct XRFrame
    {
        bool                 begun        = false;
        bool                 shouldRender = false;
        XrTime               displayTime  = 0;
        std::array<XREye, 2> eyes;
    };

    class OpenXRSession
    {
    public:
        OpenXRSession(OpenXRSystem& system, Device& device);
        ~OpenXRSession();
        OpenXRSession(const OpenXRSession&)            = delete;
        OpenXRSession& operator=(const OpenXRSession&) = delete;
        XRFrame        beginFrame();
        // Call after rendering each eye; transitions to the layout the XR runtime requires.
        void prepareSubmit(VriCommandBuffer* cmd);
        // Pair every begun frame, including shouldRender=false. Call after GPU completion.
        void endFrame();

        bool exiting() const
        {
            return m_Exiting;
        }

        VriFormat format() const
        {
            return m_Format;
        }

    private:
        void                   pollEvents();
        void                   cleanup() noexcept;
        OpenXRSystem&          m_System;
        Device&                m_Device;
        XrSession              m_Session = XR_NULL_HANDLE;
        XrSpace                m_Space   = XR_NULL_HANDLE;
        XrEnvironmentBlendMode m_Blend   = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        VriFormat              m_Format  = VriFormat_Unknown;

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
        bool                        m_Running = false;
        bool                        m_Exiting = false;
        XRFrame                     m_Frame;
    };
} // namespace vultra
