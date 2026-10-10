#define XR_USE_GRAPHICS_API_VULKAN
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/openxr/openxr.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
// clang-format off
// openxr_platform.h uses Vulkan types; these three includes must stay in this order.
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
// clang-format on

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vultra
{
    namespace
    {
        void checkXr(XrResult result, const char* operation)
        {
            if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
            {
                throw std::runtime_error(std::string(operation) + ": the OpenXR runtime reports no available headset");
            }
            if (XR_FAILED(result))
            {
                throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
            }
        }

        template<class F>
        F loadXr(XrInstance instance, const char* name)
        {
            PFN_xrVoidFunction function = nullptr;
            checkXr(xrGetInstanceProcAddr(instance, name, &function), name);
            if (!function)
            {
                throw std::runtime_error(std::string("Missing XR function: ") + name);
            }
            return reinterpret_cast<F>(function);
        }

        // Creation hooks cross a C ABI: report both errors without throwing through VRI.
        void logVulkanError(XrInstance instance, const char* operation, XrResult xr, VkResult vk)
        {
            char name[XR_MAX_RESULT_STRING_SIZE] {};
            xrResultToString(instance, xr, name);
            Logger::core().error("[OpenXR] {}: {} ({}), VkResult={}", operation, name, int(xr), int(vk));
        }
    } // namespace

    struct OpenXRSystem::Impl
    {
        XrInstance                        instance = XR_NULL_HANDLE;
        XrSystemId                        system   = XR_NULL_SYSTEM_ID;
        XrGraphicsRequirementsVulkanKHR   requirements {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
        PFN_xrCreateVulkanInstanceKHR     createInstance = nullptr;
        PFN_xrCreateVulkanDeviceKHR       createDevice   = nullptr;
        PFN_xrGetVulkanGraphicsDevice2KHR getDevice      = nullptr;
        VriVulkanCreateHooks              hooks {};

        ~Impl()
        {
            if (instance)
            {
                xrDestroyInstance(instance);
            }
        }

        static int32_t VRI_CALL onCreateInstance(void* user, const void* description, void* output)
        {
            auto&          self    = *static_cast<Impl*>(user);
            const auto*    vk      = static_cast<const VkInstanceCreateInfo*>(description);
            const uint32_t v       = vk->pApplicationInfo->apiVersion;
            const auto     version = XR_MAKE_VERSION(VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), 0);
            if (version < self.requirements.minApiVersionSupported)
            {
                Logger::core().error("[OpenXR] VRI requests Vulkan {}.{}; the runtime requires at least {}.{}",
                                     VK_API_VERSION_MAJOR(v),
                                     VK_API_VERSION_MINOR(v),
                                     unsigned(XR_VERSION_MAJOR(self.requirements.minApiVersionSupported)),
                                     unsigned(XR_VERSION_MINOR(self.requirements.minApiVersionSupported)));
                return VK_ERROR_INCOMPATIBLE_DRIVER;
            }
            // maxApiVersionSupported is the highest tested version, not a hard upper bound.
            // Let xrCreateVulkanInstanceKHR decide whether a newer compatible version works.
            if (version > self.requirements.maxApiVersionSupported)
            {
                Logger::core().warn("[OpenXR] Trying Vulkan {}.{}; runtime tested through {}.{}",
                                    VK_API_VERSION_MAJOR(v),
                                    VK_API_VERSION_MINOR(v),
                                    unsigned(XR_VERSION_MAJOR(self.requirements.maxApiVersionSupported)),
                                    unsigned(XR_VERSION_MINOR(self.requirements.maxApiVersionSupported)));
            }
            XrVulkanInstanceCreateInfoKHR info {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
            info.systemId               = self.system;
            info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
            info.vulkanCreateInfo       = vk;
            VkResult   result           = VK_ERROR_INITIALIZATION_FAILED;
            VkInstance instance         = VK_NULL_HANDLE;
            const auto xr               = self.createInstance(self.instance, &info, &instance, &result);
            if (XR_FAILED(xr))
            {
                logVulkanError(self.instance, "xrCreateVulkanInstanceKHR", xr, result);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            if (result != VK_SUCCESS)
            {
                logVulkanError(self.instance, "xrCreateVulkanInstanceKHR", xr, result);
                return result;
            }
            XrVulkanGraphicsDeviceGetInfoKHR deviceInfo {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
            deviceInfo.systemId           = self.system;
            deviceInfo.vulkanInstance     = instance;
            VkPhysicalDevice physical     = VK_NULL_HANDLE;
            const auto       deviceResult = self.getDevice(self.instance, &deviceInfo, &physical);
            if (XR_FAILED(deviceResult) || !physical)
            {
                logVulkanError(self.instance,
                               "xrGetVulkanGraphicsDevice2KHR (no usable device)",
                               deviceResult,
                               VK_ERROR_INITIALIZATION_FAILED);
                vkDestroyInstance(instance, nullptr);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            self.hooks.physicalDevice         = physical;
            *static_cast<VkInstance*>(output) = instance;
            return VK_SUCCESS;
        }

        static int32_t VRI_CALL onCreateDevice(void* user, void* physical, const void* description, void* output)
        {
            auto&                       self = *static_cast<Impl*>(user);
            XrVulkanDeviceCreateInfoKHR info {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
            info.systemId               = self.system;
            info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
            info.vulkanPhysicalDevice   = static_cast<VkPhysicalDevice>(physical);
            info.vulkanCreateInfo       = static_cast<const VkDeviceCreateInfo*>(description);
            VkResult   result           = VK_ERROR_INITIALIZATION_FAILED;
            const auto xr = self.createDevice(self.instance, &info, static_cast<VkDevice*>(output), &result);
            if (XR_FAILED(xr) || result != VK_SUCCESS)
            {
                logVulkanError(self.instance, "xrCreateVulkanDeviceKHR", xr, result);
            }
            return XR_FAILED(xr) ? VK_ERROR_INITIALIZATION_FAILED : result;
        }
    };

    OpenXRSystem::OpenXRSystem(const char* name) :
        m_Impl(std::make_unique<Impl>())
    {
        auto&                s         = *m_Impl;
        const char*          extension = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
        XrInstanceCreateInfo info {XR_TYPE_INSTANCE_CREATE_INFO};
        std::strncpy(info.applicationInfo.applicationName, name, XR_MAX_APPLICATION_NAME_SIZE - 1);
        info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        info.enabledExtensionCount      = 1;
        info.enabledExtensionNames      = &extension;
        checkXr(xrCreateInstance(&info, &s.instance), "Create OpenXR instance (check active runtime)");
        XrSystemGetInfo systemInfo {XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        checkXr(xrGetSystem(s.instance, &systemInfo, &s.system), "Get OpenXR HMD system");
        const auto getRequirements =
            loadXr<PFN_xrGetVulkanGraphicsRequirements2KHR>(s.instance, "xrGetVulkanGraphicsRequirements2KHR");
        checkXr(getRequirements(s.instance, s.system, &s.requirements), "Get Vulkan graphics requirements");
        s.createInstance = loadXr<PFN_xrCreateVulkanInstanceKHR>(s.instance, "xrCreateVulkanInstanceKHR");
        s.createDevice   = loadXr<PFN_xrCreateVulkanDeviceKHR>(s.instance, "xrCreateVulkanDeviceKHR");
        s.getDevice      = loadXr<PFN_xrGetVulkanGraphicsDevice2KHR>(s.instance, "xrGetVulkanGraphicsDevice2KHR");
        s.hooks          = {VriGraphicsAPI_Vulkan, Impl::onCreateInstance, Impl::onCreateDevice, &s, nullptr};
    }

    OpenXRSystem::~OpenXRSystem() = default;

    const void* OpenXRSystem::creationHooks() const
    {
        return &m_Impl->hooks;
    }

    XrInstance OpenXRSystem::instance() const
    {
        return m_Impl->instance;
    }

    XrSystemId OpenXRSystem::system() const
    {
        return m_Impl->system;
    }

    glm::mat4 XREye::poseMatrix() const
    {
        const auto& pose = view.pose;
        return glm::translate(glm::mat4(1), glm::vec3(pose.position.x, pose.position.y, pose.position.z)) *
               glm::mat4_cast(
                   glm::quat(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z));
    }

    glm::mat4 XRFrame::headPose() const
    {
        if (!shouldRender)
        {
            throw std::invalid_argument("Head pose requires located, renderable XR views");
        }
        const auto& left  = eyes[0].view.pose;
        const auto& right = eyes[1].view.pose;
        const auto  orientation =
            glm::slerp(glm::quat(left.orientation.w, left.orientation.x, left.orientation.y, left.orientation.z),
                       glm::quat(right.orientation.w, right.orientation.x, right.orientation.y, right.orientation.z),
                       0.5f);
        const glm::vec3 midpoint {(left.position.x + right.position.x) * 0.5f,
                                  (left.position.y + right.position.y) * 0.5f,
                                  (left.position.z + right.position.z) * 0.5f};
        return glm::translate(glm::mat4(1), midpoint) * glm::mat4_cast(orientation);
    }

    glm::mat4 XREye::viewProjection(float nearZ, float farZ) const
    {
        if (!(nearZ > 0 && farZ > nearZ))
        {
            throw std::invalid_argument("Require 0 < near < far");
        }
        const auto& f = view.fov;
        const float l = std::tan(f.angleLeft);
        const float r = std::tan(f.angleRight);
        const float u = std::tan(f.angleUp);
        const float d = std::tan(f.angleDown);
        glm::mat4   projection(0);
        projection[0][0] = 2 / (r - l);
        projection[1][1] = 2 / (u - d);
        projection[2][0] = (r + l) / (r - l);
        projection[2][1] = (u + d) / (u - d);
        projection[2][2] = farZ / (nearZ - farZ);
        projection[2][3] = -1;
        projection[3][2] = farZ * nearZ / (nearZ - farZ);
        return projection * glm::inverse(poseMatrix());
    }

    OpenXRSession::OpenXRSession(OpenXRSystem& system, Device& device) :
        m_System(system),
        m_Device(device)
    {
        try
        {
            VriInteropInterface interop {};
            check(vriGetInterface(device.handle, VRI_INTERFACE_INTEROP, sizeof(interop), &interop), "Get VRI interop");
            VriDeviceNativeHandles native {};
            check(interop.GetDeviceNativeHandles(device.handle, &native), "Get Vulkan handles");
            XrGraphicsBindingVulkanKHR binding {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
            binding.instance         = static_cast<VkInstance>(native.u.vulkan.instance);
            binding.physicalDevice   = static_cast<VkPhysicalDevice>(native.u.vulkan.physicalDevice);
            binding.device           = static_cast<VkDevice>(native.u.vulkan.device);
            binding.queueFamilyIndex = native.u.vulkan.graphicsQueueFamilyIndex;
            binding.queueIndex       = native.u.vulkan.graphicsQueueIndex;
            XrSessionCreateInfo session {XR_TYPE_SESSION_CREATE_INFO};
            session.next     = &binding;
            session.systemId = system.system();
            checkXr(xrCreateSession(system.instance(), &session, &m_Session), "Create XR session");
            XrReferenceSpaceCreateInfo space {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
            space.referenceSpaceType                 = XR_REFERENCE_SPACE_TYPE_LOCAL;
            space.poseInReferenceSpace.orientation.w = 1;
            checkXr(xrCreateReferenceSpace(m_Session, &space, &m_Space), "Create local reference space");
            uint32_t count = 0;
            checkXr(xrEnumerateViewConfigurationViews(system.instance(),
                                                      system.system(),
                                                      XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                      0,
                                                      &count,
                                                      nullptr),
                    "Count stereo views");
            if (count != 2)
            {
                throw std::runtime_error("Vultra requires PRIMARY_STEREO with two views");
            }
            XrViewConfigurationView views[2] {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
            checkXr(xrEnumerateViewConfigurationViews(system.instance(),
                                                      system.system(),
                                                      XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                      2,
                                                      &count,
                                                      views),
                    "Get stereo views");
            checkXr(xrEnumerateEnvironmentBlendModes(system.instance(),
                                                     system.system(),
                                                     XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                     0,
                                                     &count,
                                                     nullptr),
                    "Count blend modes");
            std::vector<XrEnvironmentBlendMode> modes(count);
            checkXr(xrEnumerateEnvironmentBlendModes(system.instance(),
                                                     system.system(),
                                                     XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                     count,
                                                     &count,
                                                     modes.data()),
                    "Get blend modes");
            if (modes.empty())
            {
                throw std::runtime_error("No XR environment blend mode");
            }
            m_Blend = std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) != modes.end() ?
                          XR_ENVIRONMENT_BLEND_MODE_OPAQUE :
                          modes.front();
            checkXr(xrEnumerateSwapchainFormats(m_Session, 0, &count, nullptr), "Count XR formats");
            std::vector<int64_t> formats(count);
            checkXr(xrEnumerateSwapchainFormats(m_Session, count, &count, formats.data()), "Get XR formats");
            int64_t format = 0;
            for (const auto candidate :
                 {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM})
            {
                if (std::find(formats.begin(), formats.end(), candidate) != formats.end())
                {
                    format = candidate;
                    break;
                }
            }
            switch (format)
            {
                case VK_FORMAT_R8G8B8A8_SRGB:
                    m_Format = VriFormat_RGBA8_SRGB;
                    break;
                case VK_FORMAT_R8G8B8A8_UNORM:
                    m_Format = VriFormat_RGBA8_UNORM;
                    break;
                case VK_FORMAT_B8G8R8A8_SRGB:
                    m_Format = VriFormat_BGRA8_SRGB;
                    break;
                case VK_FORMAT_B8G8R8A8_UNORM:
                    m_Format = VriFormat_BGRA8_UNORM;
                    break;
                default:
                    throw std::runtime_error("XR runtime has no supported RGBA/BGRA8 format");
            }
            for (size_t e = 0; e < 2; ++e)
            {
                auto& eye = m_Eyes[e];
                eye.size  = {views[e].recommendedImageRectWidth, views[e].recommendedImageRectHeight};
                XrSwapchainCreateInfo info {XR_TYPE_SWAPCHAIN_CREATE_INFO};
                info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
                info.format      = format;
                info.width       = eye.size.width;
                info.height      = eye.size.height;
                info.sampleCount = 1;
                info.faceCount   = 1;
                info.arraySize   = 1;
                info.mipCount    = 1;
                checkXr(xrCreateSwapchain(m_Session, &info, &eye.handle), "Create eye swapchain");
                checkXr(xrEnumerateSwapchainImages(eye.handle, 0, &count, nullptr), "Count eye images");
                std::vector<XrSwapchainImageVulkanKHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
                checkXr(xrEnumerateSwapchainImages(eye.handle,
                                                   count,
                                                   &count,
                                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                        "Get eye images");
                eye.wrappers.reserve(count);
                eye.textures.reserve(count);
                for (const auto& image : images)
                {
                    VriWrapTextureDesc wrap {};
                    wrap.nativeTexture  = image.image;
                    wrap.desc           = colorTexture(eye.size, m_Format);
                    wrap.desc.usage     = VriTextureUsage_ColorAttachment | VriTextureUsage_ShaderResource;
                    VriTexture* texture = nullptr;
                    check(interop.WrapTexture(device.handle, &wrap, &texture), "Wrap eye image");
                    eye.wrappers.push_back(texture);
                    eye.textures.push_back(std::make_unique<Texture>(device, wrap.desc, texture));
                }
            }
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }

    OpenXRSession::~OpenXRSession()
    {
        cleanup();
    }

    void OpenXRSession::cleanup() noexcept
    {
        m_Device.waitIdle();
        // Pair an interrupted frame with zero layers where the runtime still permits it.
        m_Frame.shouldRender = false;
        if (m_Frame.begun)
        {
            try
            {
                endFrame();
            }
            catch (...)
            {
            }
        }
        for (auto& eye : m_Eyes)
        {
            eye.textures.clear();
            for (auto* wrapper : eye.wrappers)
            {
                m_Device.core.DestroyTexture(wrapper);
            }
            eye.wrappers.clear();
            if (eye.handle)
            {
                xrDestroySwapchain(eye.handle);
            }
            eye.handle = XR_NULL_HANDLE;
        }
        if (m_Space)
        {
            xrDestroySpace(m_Space);
        }
        if (m_Session)
        {
            xrDestroySession(m_Session);
        }
        m_Space   = XR_NULL_HANDLE;
        m_Session = XR_NULL_HANDLE;
    }

    void OpenXRSession::pollEvents()
    {
        XrEventDataBuffer event {XR_TYPE_EVENT_DATA_BUFFER};
        XrResult          result;
        while ((result = xrPollEvent(m_System.instance(), &event)) == XR_SUCCESS)
        {
            if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            {
                m_Exiting = true;
            }
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto& changed = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                if (changed.session == m_Session)
                {
                    if (changed.state == XR_SESSION_STATE_READY && !m_Running)
                    {
                        XrSessionBeginInfo info {XR_TYPE_SESSION_BEGIN_INFO};
                        info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        checkXr(xrBeginSession(m_Session, &info), "Begin XR session");
                        m_Running = true;
                    }
                    if (changed.state == XR_SESSION_STATE_STOPPING && m_Running)
                    {
                        checkXr(xrEndSession(m_Session), "End XR session");
                        m_Running = false;
                    }
                    if (changed.state == XR_SESSION_STATE_EXITING || changed.state == XR_SESSION_STATE_LOSS_PENDING)
                    {
                        m_Exiting = true;
                    }
                }
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (result != XR_EVENT_UNAVAILABLE)
        {
            checkXr(result, "Poll XR events");
        }
    }

    XRFrame OpenXRSession::beginFrame()
    {
        if (m_Frame.begun)
        {
            throw std::logic_error("End the previous XR frame first");
        }
        m_Frame = {};
        pollEvents();
        if (!m_Running || m_Exiting)
        {
            return m_Frame;
        }
        XrFrameWaitInfo wait {XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState    state {XR_TYPE_FRAME_STATE};
        checkXr(xrWaitFrame(m_Session, &wait, &state), "Wait XR frame");
        XrFrameBeginInfo begin {XR_TYPE_FRAME_BEGIN_INFO};
        checkXr(xrBeginFrame(m_Session, &begin), "Begin XR frame");
        m_Frame.begun       = true;
        m_Frame.displayTime = state.predictedDisplayTime;
        if (!state.shouldRender)
        {
            return m_Frame;
        }
        XrViewLocateInfo locate {XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime           = state.predictedDisplayTime;
        locate.space                 = m_Space;
        XrViewState viewState {XR_TYPE_VIEW_STATE};
        XrView      views[2] {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        uint32_t    count = 0;
        checkXr(xrLocateViews(m_Session, &locate, &viewState, 2, &count, views), "Locate eye views");
        const auto valid = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
        if (count != 2 || (viewState.viewStateFlags & valid) != valid)
        {
            return m_Frame;
        }
        for (size_t e = 0; e < 2; ++e)
        {
            auto&                       eye = m_Eyes[e];
            XrSwapchainImageAcquireInfo acquire {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            checkXr(xrAcquireSwapchainImage(eye.handle, &acquire, &eye.index), "Acquire eye image");
            eye.acquired = true;
            XrSwapchainImageWaitInfo imageWait {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            imageWait.timeout = XR_INFINITE_DURATION;
            checkXr(xrWaitSwapchainImage(eye.handle, &imageWait), "Wait eye image");
            eye.waited      = true;
            auto& target    = *eye.textures.at(eye.index);
            target.state    = {}; // XR acquisition discards the previous frame's contents
            m_Frame.eyes[e] = {&target, views[e]};
        }
        m_Frame.shouldRender = true;
        return m_Frame;
    }

    void OpenXRSession::prepareSubmit(VriCommandBuffer* cmd)
    {
        if (m_Frame.shouldRender)
        {
            for (const auto& eye : m_Frame.eyes)
            {
                eye.color->transition(cmd,
                                      {VriAccess_ColorAttachmentWrite,
                                       VriLayout_ColorAttachment,
                                       VriPipelineStage_ColorAttachmentOutput});
            }
        }
    }

    void OpenXRSession::endFrame()
    {
        if (!m_Frame.begun)
        {
            return;
        }
        XrCompositionLayerProjectionView views[2] {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                   {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        for (size_t e = 0; e < 2; ++e)
        {
            auto& eye = m_Eyes[e];
            if (eye.acquired && eye.waited)
            {
                XrSwapchainImageReleaseInfo release {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                checkXr(xrReleaseSwapchainImage(eye.handle, &release), "Release eye image");
                eye.acquired = false;
                eye.waited   = false;
            }
            views[e].pose                      = m_Frame.eyes[e].view.pose;
            views[e].fov                       = m_Frame.eyes[e].view.fov;
            views[e].subImage.swapchain        = eye.handle;
            views[e].subImage.imageRect.extent = {int32_t(eye.size.width), int32_t(eye.size.height)};
        }
        XrCompositionLayerProjection layer {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space         = m_Space;
        layer.viewCount     = 2;
        layer.views         = views;
        const auto*    base = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
        XrFrameEndInfo end {XR_TYPE_FRAME_END_INFO};
        end.displayTime          = m_Frame.displayTime;
        end.environmentBlendMode = m_Blend;
        end.layerCount           = m_Frame.shouldRender ? 1 : 0;
        end.layers               = m_Frame.shouldRender ? &base : nullptr;
        m_Frame.begun            = false;
        checkXr(xrEndFrame(m_Session, &end), "End XR frame");
    }
} // namespace vultra
