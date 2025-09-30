#pragma once

#include "vultra/core/rhi/texture.hpp"

// OpenXR Headers
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <glm/glm.hpp>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        struct Extent2D;
    } // namespace rhi

    namespace openxr
    {
        class XRDevice;

        class XRHeadset
        {
        public:
            enum class BeginFrameResult
            {
                eError,
                eNormal,
                eSkipRender,
                eSkipAll
            };

            struct StereoRenderTargetView
            {
                rhi::Texture left;
                rhi::Texture right;
            };

            explicit XRHeadset(rhi::RenderDevice&);
            XRHeadset(const XRHeadset&)     = delete;
            XRHeadset(XRHeadset&&) noexcept = delete;
            ~XRHeadset();

            XRHeadset& operator=(const XRHeadset&)     = delete;
            XRHeadset& operator=(XRHeadset&&) noexcept = delete;

            [[nodiscard]] BeginFrameResult beginFrame(uint32_t& swapchainImageIndex);
            void                           endFrame() const;

            [[nodiscard]] bool isExitRequested() const { return m_ExitRequested; }

            [[nodiscard]] XrSession    getXrSession() const { return m_Session; }
            [[nodiscard]] XrSpace      getXrSpace() const { return m_Space; }
            [[nodiscard]] XrFrameState getXrFrameState() const { return m_FrameState; }

            [[nodiscard]] size_t        getEyeCount() const { return m_EyeCount; }
            [[nodiscard]] glm::vec3     getEyePosition(size_t eyeIndex) const;
            [[nodiscard]] glm::quat     getEyeRotation(size_t eyeIndex) const;
            [[nodiscard]] rhi::Extent2D getEyeResolution(size_t eyeIndex) const;
            [[nodiscard]] glm::mat4     getEyeViewMatrix(size_t eyeIndex) const;
            [[nodiscard]] XrFovf        getEyeFOV(size_t eyeIndex) const;

            [[nodiscard]] float getIPD() const;

            [[nodiscard]] size_t                  getSwapchainCount() const { return m_SwapchainImages.size(); }
            [[nodiscard]] StereoRenderTargetView& getSwapchainStereoRenderTargetView(size_t index);
            [[nodiscard]] static rhi::PixelFormat getSwapchainPixelFormat();

        private:
            bool beginSession() const;
            bool endSession() const;

            vk::Image getSwapchainImage(size_t swapchainImageIndex) const
            {
                return m_SwapchainImages[swapchainImageIndex].image;
            }

        private:
            XRDevice&          m_Device;
            rhi::RenderDevice& m_RenderDevice;

            size_t                 m_EyeCount {0u};
            std::vector<glm::mat4> m_EyeViewMatrices;
            std::vector<XrFovf>    m_EyeFOVs;

            XrSession      m_Session {nullptr};
            XrSessionState m_SessionState {XR_SESSION_STATE_UNKNOWN};
            XrSpace        m_Space {nullptr};
            XrFrameState   m_FrameState {};
            XrViewState    m_ViewState {};

            std::vector<XrViewConfigurationView>          m_EyeImageInfos;
            std::vector<XrView>                           m_EyePoses;
            std::vector<XrCompositionLayerProjectionView> m_EyeRenderInfos;

            XrSwapchain                             m_Swapchain {nullptr};
            std::vector<XrSwapchainImageVulkan2KHR> m_SwapchainImages;

            std::vector<StereoRenderTargetView> m_SwapchainStereoRenderTargetViews;

            bool m_ExitRequested {false};
        };
    } // namespace openxr
} // namespace vultra