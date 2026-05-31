#pragma once

#include "vultra/core/rhi/texture.hpp"

#include <vulkan/vulkan.hpp>

// OpenXR Headers
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
                rhi::Texture stereo;
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
            void                           endFrame();

            [[nodiscard]] bool isExitRequested() const { return m_ExitRequested; }
            [[nodiscard]] bool isSessionCloseRequested() const { return m_SessionCloseRequested; }

            [[nodiscard]] XrSession    getXrSession() const { return m_Session; }
            [[nodiscard]] XrSpace      getXrSpace() const { return m_Space; }
            [[nodiscard]] XrFrameState getXrFrameState() const { return m_FrameState; }

            [[nodiscard]] size_t        getEyeCount() const { return m_EyeCount; }
            [[nodiscard]] glm::vec3     getEyePosition(size_t eyeIndex) const;
            [[nodiscard]] glm::quat     getEyeRotation(size_t eyeIndex) const;
            [[nodiscard]] glm::mat4     getEyePoseMatrix(size_t eyeIndex) const;
            [[nodiscard]] glm::vec3     getHeadPosition() const;
            [[nodiscard]] glm::quat     getHeadRotation() const;
            [[nodiscard]] rhi::Extent2D getEyeResolution(size_t eyeIndex) const;
            [[nodiscard]] glm::mat4     getEyeViewMatrix(size_t eyeIndex) const;
            [[nodiscard]] glm::mat4     getEyeProjectionMatrix(size_t eyeIndex) const;
            [[nodiscard]] XrFovf        getEyeFOV(size_t eyeIndex) const;
            [[nodiscard]] int64_t       getPredictedDisplayTime() const { return m_FrameState.predictedDisplayTime; }
            [[nodiscard]] XrViewStateFlags getViewStateFlags() const { return m_ViewState.viewStateFlags; }

            [[nodiscard]] float getIPD() const;

            [[nodiscard]] size_t                  getSwapchainCount() const { return m_SwapchainImages.size(); }
            [[nodiscard]] StereoRenderTargetView& getSwapchainStereoRenderTargetView(size_t index);
            [[nodiscard]] rhi::PixelFormat getSwapchainPixelFormat() const;

        private:
            bool beginSession();
            bool endSession();
            bool ensureSwapchain();
            void destroySwapchain();

            vk::Image getSwapchainImage(size_t swapchainImageIndex) const
            {
                return vk::Image {m_SwapchainImages[swapchainImageIndex].image};
            }

        private:
            XRDevice&          m_Device;
            rhi::RenderDevice& m_RenderDevice;

            size_t                 m_EyeCount {0u};
            std::vector<glm::mat4> m_EyeViewMatrices;
            std::vector<glm::mat4> m_EyeProjectionMatrices;
            std::vector<XrFovf>    m_EyeFOVs;

            XrSession      m_Session {XR_NULL_HANDLE};
            XrSessionState m_SessionState {XR_SESSION_STATE_UNKNOWN};
            XrSpace        m_Space {XR_NULL_HANDLE};
            XrFrameState   m_FrameState {};
            XrViewState    m_ViewState {};

            std::vector<XrViewConfigurationView>          m_EyeImageInfos;
            std::vector<XrView>                           m_EyePoses;
            std::vector<XrCompositionLayerProjectionView> m_EyeRenderInfos;

            XrSwapchain                             m_Swapchain {XR_NULL_HANDLE};
            std::vector<XrSwapchainImageVulkan2KHR> m_SwapchainImages;
            rhi::PixelFormat                        m_SwapchainPixelFormat {rhi::PixelFormat::eUndefined};

            std::vector<StereoRenderTargetView> m_SwapchainStereoRenderTargetViews;

            bool m_SessionRunning {false};
            bool m_ExitRequested {false};
            bool m_SessionCloseRequested {false};
            bool m_FrameBegun {false};
            bool m_SwapchainImageAcquired {false};
        };
    } // namespace openxr
} // namespace vultra
