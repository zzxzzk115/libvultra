#include "vultra/function/openxr/xr_headset.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/extent2d.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/openxr/xr_device.hpp"
#include "vultra/function/openxr/xr_helper.hpp"

namespace
{
    constexpr XrReferenceSpaceType     spaceType {XR_REFERENCE_SPACE_TYPE_STAGE};
    constexpr vultra::rhi::PixelFormat colorFormat {vultra::rhi::PixelFormat::eRGBA8_sRGB};
} // namespace

namespace vultra
{
    namespace openxr
    {
        XRHeadset::XRHeadset(rhi::RenderDevice& rd) : m_Device(*rd.getXRDevice()), m_RenderDevice(rd)
        {
            if (!static_cast<bool>(rd.getFeatureFlag() & rhi::RenderDeviceFeatureFlagBits::eOpenXR))
            {
                VULTRA_CORE_ERROR("[XRHeadset] OpenXR feature is not enabled in the RenderDevice!");
                throw std::runtime_error("OpenXR feature is not enabled in the RenderDevice");
            }

            // Macro hack
            XrInstance m_XrInstance = m_Device.m_XrInstance;

            // Create an OpenXR session
            XrGraphicsBindingVulkan2KHR graphicsBinding {};
            graphicsBinding.type             = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;
            graphicsBinding.device           = m_RenderDevice.m_Device;
            graphicsBinding.instance         = m_RenderDevice.m_Instance;
            graphicsBinding.physicalDevice   = m_RenderDevice.m_PhysicalDevice;
            graphicsBinding.queueFamilyIndex = m_RenderDevice.m_GenericQueueFamilyIndex;
            graphicsBinding.queueIndex       = 0u;

            XrSessionCreateInfo sessionCreateInfo {};
            sessionCreateInfo.type     = XR_TYPE_SESSION_CREATE_INFO;
            sessionCreateInfo.next     = &graphicsBinding;
            sessionCreateInfo.systemId = m_Device.m_XrSystemId;
            OPENXR_CHECK(xrCreateSession(m_Device.m_XrInstance, &sessionCreateInfo, &m_Session),
                         "Failed to create XrSession!");

            // Create an OpenXR reference space
            XrReferenceSpaceCreateInfo referenceSpaceCreateInfo {};
            referenceSpaceCreateInfo.type                 = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
            referenceSpaceCreateInfo.referenceSpaceType   = spaceType;
            referenceSpaceCreateInfo.poseInReferenceSpace = xrutils::makeIdentity();
            OPENXR_CHECK(xrCreateReferenceSpace(m_Session, &referenceSpaceCreateInfo, &m_Space),
                         "Failed to create XrReferenceSpace!");

            const XrViewConfigurationType viewType = m_Device.m_ViewType;

            // Get the number of eyes
            OPENXR_CHECK(xrEnumerateViewConfigurationViews(m_Device.m_XrInstance,
                                                           m_Device.m_XrSystemId,
                                                           viewType,
                                                           0,
                                                           reinterpret_cast<uint32_t*>(&m_EyeCount),
                                                           nullptr),
                         "Failed to enumerate ViewConfiguration Views.");

            // Get the eye image info per eye
            m_EyeImageInfos.resize(m_EyeCount);
            for (XrViewConfigurationView& eyeInfo : m_EyeImageInfos)
            {
                eyeInfo.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
                eyeInfo.next = nullptr;
            }

            OPENXR_CHECK(xrEnumerateViewConfigurationViews(m_Device.m_XrInstance,
                                                           m_Device.m_XrSystemId,
                                                           viewType,
                                                           static_cast<uint32_t>(m_EyeCount),
                                                           reinterpret_cast<uint32_t*>(&m_EyeCount),
                                                           m_EyeImageInfos.data()),
                         "Failed to enumerate ViewConfiguration Views.");

            // Allocate eye poses
            m_EyePoses.resize(m_EyeCount);
            for (XrView& eyePose : m_EyePoses)
            {
                eyePose.type = XR_TYPE_VIEW;
                eyePose.next = nullptr;
            }

            // Verify that the desired color format is supported
            {
                uint32_t formatCount = 0u;
                OPENXR_CHECK(xrEnumerateSwapchainFormats(m_Session, 0u, &formatCount, nullptr),
                             "Failed to enumerate swapchain formats");

                std::vector<int64_t> formats(formatCount);
                OPENXR_CHECK(xrEnumerateSwapchainFormats(m_Session, formatCount, &formatCount, formats.data()),
                             "Failed to enumerate swapchain formats");

                bool formatFound = false;
                for (const int64_t& format : formats)
                {
                    if (format == static_cast<int64_t>(colorFormat))
                    {
                        formatFound = true;
                        break;
                    }
                }

                if (!formatFound)
                {
                    spdlog::error("[Headset] Invalid swapchain format!");
                    return;
                }
            }

            // Create a swapchain and render targets
            {
                const XrViewConfigurationView& eyeImageInfo = m_EyeImageInfos.at(0u);

                // Create a swapchain
                XrSwapchainCreateInfo swapchainCreateInfo {};
                swapchainCreateInfo.type        = XR_TYPE_SWAPCHAIN_CREATE_INFO;
                swapchainCreateInfo.format      = static_cast<int64_t>(colorFormat);
                swapchainCreateInfo.sampleCount = eyeImageInfo.recommendedSwapchainSampleCount;
                swapchainCreateInfo.width       = eyeImageInfo.recommendedImageRectWidth;
                swapchainCreateInfo.height      = eyeImageInfo.recommendedImageRectHeight;
                swapchainCreateInfo.arraySize   = static_cast<uint32_t>(m_EyeCount);
                swapchainCreateInfo.faceCount   = 1u;
                swapchainCreateInfo.mipCount    = 1u;
                swapchainCreateInfo.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                                                 XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                                                 XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;

                OPENXR_CHECK(xrCreateSwapchain(m_Session, &swapchainCreateInfo, &m_Swapchain),
                             "Failed to create swapchain!");

                // Get the number of swapchain images
                uint32_t swapchainImageCount;
                OPENXR_CHECK(xrEnumerateSwapchainImages(m_Swapchain, 0u, &swapchainImageCount, nullptr),
                             "Failed to enumerate swapchains");

                // Retrieve the swapchain images
                m_SwapchainImages.resize(swapchainImageCount);
                for (XrSwapchainImageVulkan2KHR& swapchainImage : m_SwapchainImages)
                {
                    swapchainImage.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
                }

                XrSwapchainImageBaseHeader* data =
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(m_SwapchainImages.data());

                OPENXR_CHECK(
                    xrEnumerateSwapchainImages(
                        m_Swapchain, static_cast<uint32_t>(m_SwapchainImages.size()), &swapchainImageCount, data),
                    "Failed to enumerate swapchains");

                // Create swapchain render target views
                m_SwapchainStereoRenderTargetViews.resize(m_SwapchainImages.size());
                for (size_t i = 0u; i < m_SwapchainImages.size(); ++i)
                {
                    const XrSwapchainImageVulkan2KHR& swapchainImage = m_SwapchainImages[i];

                    m_SwapchainStereoRenderTargetViews[i].left =
                        rhi::Texture {m_RenderDevice.m_Device,
                                      swapchainImage.image,
                                      {static_cast<uint32_t>(eyeImageInfo.recommendedImageRectWidth),
                                       static_cast<uint32_t>(eyeImageInfo.recommendedImageRectHeight)},
                                      colorFormat,
                                      0};

                    m_SwapchainStereoRenderTargetViews[i].right =
                        rhi::Texture {m_RenderDevice.m_Device,
                                      swapchainImage.image,
                                      {static_cast<uint32_t>(eyeImageInfo.recommendedImageRectWidth),
                                       static_cast<uint32_t>(eyeImageInfo.recommendedImageRectHeight)},
                                      colorFormat,
                                      1};
                }
            }

            m_EyeRenderInfos.resize(m_EyeCount);
            for (size_t eyeIndex = 0u; eyeIndex < m_EyeRenderInfos.size(); ++eyeIndex)
            {
                XrCompositionLayerProjectionView& eyeRenderInfo = m_EyeRenderInfos.at(eyeIndex);
                eyeRenderInfo.type                              = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                eyeRenderInfo.next                              = nullptr;

                // Associate this eye with the swapchain
                const XrViewConfigurationView& eyeImageInfo = m_EyeImageInfos.at(eyeIndex);
                eyeRenderInfo.subImage.swapchain            = m_Swapchain;
                eyeRenderInfo.subImage.imageArrayIndex      = static_cast<uint32_t>(eyeIndex);
                eyeRenderInfo.subImage.imageRect.offset     = {0, 0};
                eyeRenderInfo.subImage.imageRect.extent     = {
                    static_cast<int32_t>(eyeImageInfo.recommendedImageRectWidth),
                    static_cast<int32_t>(eyeImageInfo.recommendedImageRectHeight)};
            }

            m_EyeViewMatrices.resize(m_EyeCount);
            m_EyeFOVs.resize(m_EyeCount);
        }

        XRHeadset::~XRHeadset()
        {
            // Clean up OpenXR
            if (m_Session)
            {
                xrEndSession(m_Session);
            }

            if (m_Swapchain)
            {
                xrDestroySwapchain(m_Swapchain);
            }

            if (m_Space)
            {
                xrDestroySpace(m_Space);
            }

            if (m_Session)
            {
                xrDestroySession(m_Session);
            }
        }

        XRHeadset::BeginFrameResult XRHeadset::beginFrame(uint32_t& swapchainImageIndex)
        {
            ZoneScopedN("XRHeadset::beginFrame");

            XrInstance instance = m_Device.m_XrInstance;

            // Poll OpenXR events
            XrEventDataBuffer buffer {};
            buffer.type = XR_TYPE_EVENT_DATA_BUFFER;

            while (xrPollEvent(instance, &buffer) == XR_SUCCESS)
            {
                switch (buffer.type)
                {
                    case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                        m_ExitRequested = true;
                        return BeginFrameResult::eSkipAll;
                    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                        XrEventDataSessionStateChanged* event =
                            reinterpret_cast<XrEventDataSessionStateChanged*>(&buffer);
                        m_SessionState = event->state;

                        if (event->state == XR_SESSION_STATE_READY)
                        {
                            if (!beginSession())
                            {
                                return BeginFrameResult::eError;
                            }
                        }
                        else if (event->state == XR_SESSION_STATE_STOPPING)
                        {
                            if (!endSession())
                            {
                                return BeginFrameResult::eError;
                            }
                        }
                        else if (event->state == XR_SESSION_STATE_LOSS_PENDING ||
                                 event->state == XR_SESSION_STATE_EXITING)
                        {
                            m_ExitRequested = true;
                            return BeginFrameResult::eSkipAll;
                        }

                        break;
                    }
                    default:;
                }

                buffer.type = XR_TYPE_EVENT_DATA_BUFFER;
            }

            if (m_SessionState != XR_SESSION_STATE_READY && m_SessionState != XR_SESSION_STATE_SYNCHRONIZED &&
                m_SessionState != XR_SESSION_STATE_VISIBLE && m_SessionState != XR_SESSION_STATE_FOCUSED)
            {
                // If we are not ready, synchronized, visible or focused, we skip all processing of this frame
                // This means no waiting, no beginning or ending of the frame at all
                return BeginFrameResult::eSkipAll;
            }

            // Wait for the new frame
            m_FrameState.type = XR_TYPE_FRAME_STATE;

            XrFrameWaitInfo frameWaitInfo {};
            frameWaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;

            XrResult result = xrWaitFrame(m_Session, &frameWaitInfo, &m_FrameState);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[XRHeadset] xrWaitFrame failed: {}", xrutils::resultToString(instance, result));
                return BeginFrameResult::eError;
            }

            // Begin the new frame
            XrFrameBeginInfo frameBeginInfo {};
            frameBeginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;

            result = xrBeginFrame(m_Session, &frameBeginInfo);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[XRHeadset] xrBeginFrame failed: {}", xrutils::resultToString(instance, result));
                return BeginFrameResult::eError;
            }

            // Update the eye poses
            m_ViewState.type = XR_TYPE_VIEW_STATE;
            uint32_t         viewCount;
            XrViewLocateInfo viewLocateInfo {};
            viewLocateInfo.type                  = XR_TYPE_VIEW_LOCATE_INFO;
            viewLocateInfo.viewConfigurationType = m_Device.getXrViewType();
            viewLocateInfo.displayTime           = m_FrameState.predictedDisplayTime;
            viewLocateInfo.space                 = m_Space;

            result = xrLocateViews(m_Session,
                                   &viewLocateInfo,
                                   &m_ViewState,
                                   static_cast<uint32_t>(m_EyePoses.size()),
                                   &viewCount,
                                   m_EyePoses.data());
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[XRHeadset] xrLocateViews failed: {}", xrutils::resultToString(instance, result));
                return BeginFrameResult::eError;
            }

            if (viewCount != m_EyeCount)
            {
                VULTRA_CORE_ERROR("[XRHeadset] Eye count mismatch: {}", viewCount);
                return BeginFrameResult::eError;
            }

            if ((m_ViewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0)
            {
                VULTRA_CORE_ERROR("[XRHeadset] Eye position valid flag not set");
                return BeginFrameResult::eError;
            }

            if ((m_ViewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0)
            {
                VULTRA_CORE_ERROR("[XRHeadset] Eye orientation valid flag not set");
                return BeginFrameResult::eError;
            }

            // Update the eye render infos, view and projection matrices
            for (size_t eyeIndex = 0u; eyeIndex < m_EyeCount; ++eyeIndex)
            {
                // Copy the eye poses into the eye render infos
                XrCompositionLayerProjectionView& eyeRenderInfo = m_EyeRenderInfos.at(eyeIndex);
                const XrView&                     eyePose       = m_EyePoses.at(eyeIndex);
                eyeRenderInfo.pose                              = eyePose.pose;
                eyeRenderInfo.fov                               = eyePose.fov;

                // Update the view and projection matrices
                const XrPosef& pose            = eyeRenderInfo.pose;
                m_EyeViewMatrices.at(eyeIndex) = glm::inverse(xrutils::poseToMatrix(pose));
                m_EyeFOVs.at(eyeIndex)         = eyeRenderInfo.fov;
            }

            // Acquire the swapchain image
            XrSwapchainImageAcquireInfo swapchainImageAcquireInfo {};
            swapchainImageAcquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
            result = xrAcquireSwapchainImage(m_Swapchain, &swapchainImageAcquireInfo, &swapchainImageIndex);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[XRHeadset] xrAcquireSwapchainImage failed: {}",
                                  xrutils::resultToString(instance, result));
                return BeginFrameResult::eError;
            }

            // Wait for the swapchain image
            XrSwapchainImageWaitInfo swapchainImageWaitInfo {};
            swapchainImageWaitInfo.type    = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
            swapchainImageWaitInfo.timeout = XR_INFINITE_DURATION;
            result                         = xrWaitSwapchainImage(m_Swapchain, &swapchainImageWaitInfo);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[XRHeadset] xrWaitSwapchainImage failed: {}",
                                  xrutils::resultToString(instance, result));
                return BeginFrameResult::eError;
            }

            if (!m_FrameState.shouldRender)
            {
                return BeginFrameResult::eSkipRender;
            }

            return BeginFrameResult::eNormal;
        }

        void XRHeadset::endFrame() const
        {
            ZoneScopedN("XRHeadset::endFrame");

            // Release the swapchain image
            XrSwapchainImageReleaseInfo swapchainImageReleaseInfo {};
            swapchainImageReleaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
            XrResult result                = xrReleaseSwapchainImage(m_Swapchain, &swapchainImageReleaseInfo);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[XRHeadset] xrReleaseSwapchainImage failed: {}",
                                  xrutils::resultToString(m_Device.m_XrInstance, result));
                return;
            }

            // End the frame
            XrCompositionLayerProjection compositionLayerProjection {};
            compositionLayerProjection.type      = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
            compositionLayerProjection.space     = m_Space;
            compositionLayerProjection.viewCount = static_cast<uint32_t>(m_EyeRenderInfos.size());
            compositionLayerProjection.views     = m_EyeRenderInfos.data();

            std::vector<XrCompositionLayerBaseHeader*> layers;
            if (m_FrameState.shouldRender)
            {
                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&compositionLayerProjection));
            }

            XrFrameEndInfo frameEndInfo {};
            frameEndInfo.type                 = XR_TYPE_FRAME_END_INFO;
            frameEndInfo.displayTime          = m_FrameState.predictedDisplayTime;
            frameEndInfo.layerCount           = static_cast<uint32_t>(layers.size());
            frameEndInfo.layers               = layers.data();
            frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            result                            = xrEndFrame(m_Session, &frameEndInfo);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[XRHeadset] xrEndFrame failed: {}",
                                  xrutils::resultToString(m_Device.m_XrInstance, result));
            }
        }

        glm::vec3 XRHeadset::getEyePosition(size_t eyeIndex) const
        {
            auto pose = m_EyePoses.at(eyeIndex).pose;
            return xrutils::toVec3(pose.position);
        }

        glm::quat XRHeadset::getEyeRotation(size_t eyeIndex) const
        {
            auto pose = m_EyePoses.at(eyeIndex).pose;
            return xrutils::toQuat(pose.orientation);
        }

        rhi::Extent2D XRHeadset::getEyeResolution(size_t eyeIndex) const
        {
            const XrViewConfigurationView& eyeInfo = m_EyeImageInfos.at(eyeIndex);
            return {eyeInfo.recommendedImageRectWidth, eyeInfo.recommendedImageRectHeight};
        }

        glm::mat4 XRHeadset::getEyeViewMatrix(size_t eyeIndex) const { return m_EyeViewMatrices.at(eyeIndex); }

        XrFovf XRHeadset::getEyeFOV(size_t eyeIndex) const { return m_EyeFOVs.at(eyeIndex); }

        XRHeadset::StereoRenderTargetView& XRHeadset::getSwapchainStereoRenderTargetView(size_t index)
        {
            return m_SwapchainStereoRenderTargetViews.at(index);
        }

        rhi::PixelFormat XRHeadset::getSwapchainPixelFormat() { return colorFormat; }

        bool XRHeadset::beginSession() const
        {
            // Start the session
            XrSessionBeginInfo sessionBeginInfo {};
            sessionBeginInfo.type                         = XR_TYPE_SESSION_BEGIN_INFO;
            sessionBeginInfo.primaryViewConfigurationType = m_Device.getXrViewType();

            const XrResult result = xrBeginSession(m_Session, &sessionBeginInfo);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[XRHeadset] xrBeginSession failed: {}",
                                  xrutils::resultToString(m_Device.m_XrInstance, result));
                return false;
            }

            return true;
        }

        bool XRHeadset::endSession() const
        {
            // End the session
            const XrResult result = xrEndSession(m_Session);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_ERROR("[XRHeadset] xrEndSession failed: {}",
                                  xrutils::resultToString(m_Device.m_XrInstance, result));
                return false;
            }

            return true;
        }
    } // namespace openxr
} // namespace vultra