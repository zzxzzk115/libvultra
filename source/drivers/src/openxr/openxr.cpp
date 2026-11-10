#define XR_USE_GRAPHICS_API_VULKAN
#if defined(__APPLE__)
#define XR_USE_GRAPHICS_API_METAL
#endif
#if defined(__ANDROID__)
#define XR_USE_PLATFORM_ANDROID
#include <jni.h>
#endif
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
#include <utility>

namespace vultra
{
#if defined(__APPLE__)
    std::array<std::pair<int64_t, VriFormat>, 4> metalXrFormats();
#endif
    namespace
    {
        bool validXrPose(const XrPosef& pose)
        {
            const auto& p             = pose.position;
            const auto& q             = pose.orientation;
            const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(lengthSquared) &&
                   std::abs(lengthSquared - 1.0f) <= 0.001f;
        }

        void reportXrCleanup(XrResult result, const char* operation)
        {
            if (XR_FAILED(result))
            {
                Logger::core().error("[OpenXR] {} failed during cleanup: {}", operation, int(result));
            }
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
        VriGraphicsAPI                    api = VULTRA_GRAPHICS_API;
#if defined(__APPLE__)
        VriMetalCreateInfo metal {};
#endif

        ~Impl()
        {
            if (instance)
            {
                reportXrCleanup(xrDestroyInstance(instance), "Destroy instance");
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

    OpenXRSystem::OpenXRSystem(Uninitialized) :
        m_Impl(std::make_unique<Impl>())
    {
    }

#if defined(__ANDROID__)
    std::expected<std::unique_ptr<OpenXRSystem>, std::string> OpenXRSystem::create(OpenXRAndroidContext context,
                                                                                   const char*          name)
#else
    std::expected<std::unique_ptr<OpenXRSystem>, std::string> OpenXRSystem::create(const char* name)
#endif
    {
        auto result = std::unique_ptr<OpenXRSystem>(new OpenXRSystem(Uninitialized {}));
#if defined(__ANDROID__)
        auto status = result->initialize(context, name);
#else
        auto status = result->initialize(name);
#endif
        if (!status)
        {
            return std::unexpected(status.error());
        }
        return result;
    }

#if defined(__ANDROID__)
    std::expected<void, std::string> OpenXRSystem::initialize(OpenXRAndroidContext context, const char* name)
#else
    std::expected<void, std::string> OpenXRSystem::initialize(const char* name)
#endif
    {
        if (!name || !*name)
        {
            return std::unexpected("OpenXR application name is empty");
        }
        auto&      s           = *m_Impl;
        const auto checkResult = [](XrResult value, const char* operation) -> std::expected<void, std::string>
        {
            if (XR_FAILED(value))
            {
                return std::unexpected(std::string(operation) + " failed: " + std::to_string(value));
            }
            return {};
        };
        const auto load = [&](XrInstance  instance,
                              const char* function) -> std::expected<PFN_xrVoidFunction, std::string>
        {
            PFN_xrVoidFunction value = nullptr;
            if (auto status = checkResult(xrGetInstanceProcAddr(instance, function, &value), function); !status)
            {
                return std::unexpected(status.error());
            }
            if (!value)
            {
                return std::unexpected(std::string("Missing XR function: ") + function);
            }
            return value;
        };
#if defined(__ANDROID__)
        if (!context.javaVm || !context.activity)
            return std::unexpected("OpenXR on Android requires a JavaVM and Activity");
        auto initialize = load(XR_NULL_HANDLE, "xrInitializeLoaderKHR");
        if (!initialize)
            return std::unexpected(initialize.error());
        XrLoaderInitInfoAndroidKHR loader {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loader.applicationVM      = context.javaVm;
        loader.applicationContext = context.activity;
        if (auto status = checkResult(reinterpret_cast<PFN_xrInitializeLoaderKHR>(*initialize)(
                                          reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader)),
                                      "Initialize Android OpenXR loader");
            !status)
            return std::unexpected(status.error());
        XrInstanceCreateInfoAndroidKHR android {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        android.applicationVM       = context.javaVm;
        android.applicationActivity = context.activity;
        const char* extensions[] {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME};
#else
        const char* extensions[] {
#if defined(__APPLE__)
            s.api == VriGraphicsAPI_Metal ? XR_KHR_METAL_ENABLE_EXTENSION_NAME :
#endif
                                            XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
#endif
        XrInstanceCreateInfo info {XR_TYPE_INSTANCE_CREATE_INFO};
#if defined(__ANDROID__)
        info.next = &android;
#endif
        std::strncpy(info.applicationInfo.applicationName, name, XR_MAX_APPLICATION_NAME_SIZE - 1);
        info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        info.enabledExtensionCount      = uint32_t(std::size(extensions));
        info.enabledExtensionNames      = extensions;
        if (auto status = checkResult(xrCreateInstance(&info, &s.instance), "Create OpenXR instance"); !status)
        {
            return std::unexpected(status.error());
        }
        XrSystemGetInfo system {XR_TYPE_SYSTEM_GET_INFO};
        system.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (auto status = checkResult(xrGetSystem(s.instance, &system, &s.system), "Get OpenXR HMD system"); !status)
        {
            return std::unexpected(status.error());
        }
#if defined(__APPLE__)
        if (s.api == VriGraphicsAPI_Metal)
        {
            auto requirements = load(s.instance, "xrGetMetalGraphicsRequirementsKHR");
            if (!requirements)
                return std::unexpected(requirements.error());
            XrGraphicsRequirementsMetalKHR metalRequirements {XR_TYPE_GRAPHICS_REQUIREMENTS_METAL_KHR};
            if (auto status = checkResult(reinterpret_cast<PFN_xrGetMetalGraphicsRequirementsKHR>(
                                              *requirements)(s.instance, s.system, &metalRequirements),
                                          "Get Metal graphics requirements");
                !status)
                return std::unexpected(status.error());
            if (!metalRequirements.metalDevice)
                return std::unexpected("OpenXR returned no Metal device");
            s.metal = {VriGraphicsAPI_Metal, metalRequirements.metalDevice};
            return {};
        }
#endif
        if (s.api != VriGraphicsAPI_Vulkan)
            return std::unexpected("OpenXR does not support the configured graphics backend");
        auto requirements = load(s.instance, "xrGetVulkanGraphicsRequirements2KHR");
        if (!requirements)
        {
            return std::unexpected(requirements.error());
        }
        if (auto status = checkResult(reinterpret_cast<PFN_xrGetVulkanGraphicsRequirements2KHR>(
                                          *requirements)(s.instance, s.system, &s.requirements),
                                      "Get Vulkan graphics requirements");
            !status)
        {
            return std::unexpected(status.error());
        }
        auto createInstance = load(s.instance, "xrCreateVulkanInstanceKHR");
        if (!createInstance)
        {
            return std::unexpected(createInstance.error());
        }
        auto createDevice = load(s.instance, "xrCreateVulkanDeviceKHR");
        if (!createDevice)
        {
            return std::unexpected(createDevice.error());
        }
        auto getDevice = load(s.instance, "xrGetVulkanGraphicsDevice2KHR");
        if (!getDevice)
        {
            return std::unexpected(getDevice.error());
        }
        s.createInstance = reinterpret_cast<PFN_xrCreateVulkanInstanceKHR>(*createInstance);
        s.createDevice   = reinterpret_cast<PFN_xrCreateVulkanDeviceKHR>(*createDevice);
        s.getDevice      = reinterpret_cast<PFN_xrGetVulkanGraphicsDevice2KHR>(*getDevice);
        s.hooks          = {VriGraphicsAPI_Vulkan, Impl::onCreateInstance, Impl::onCreateDevice, &s, nullptr};
        return {};
    }

    OpenXRSystem::~OpenXRSystem() = default;

    const void* OpenXRSystem::creationHooks() const
    {
#if defined(__APPLE__)
        if (m_Impl->api == VriGraphicsAPI_Metal)
            return &m_Impl->metal;
#endif
        return &m_Impl->hooks;
    }

    std::expected<void, std::string> OpenXRSystem::validateDevice(Device& device) const
    {
        const auto* desc = device.core.GetDeviceDesc(device.handle);
        if (!desc || desc->graphicsAPI != m_Impl->api)
            return std::unexpected("Graphics device API does not match the OpenXR system");
        VriInteropInterface interop {};
        if (vriGetInterface(device.handle, VRI_INTERFACE_INTEROP, sizeof(interop), &interop) != VriResult_Success)
            return std::unexpected("Graphics device has no native interop interface");
        VriDeviceNativeHandles native {};
        if (interop.GetDeviceNativeHandles(device.handle, &native) != VriResult_Success || native.api != m_Impl->api)
            return std::unexpected("Unable to get OpenXR graphics device handles");
#if defined(__APPLE__)
        if (m_Impl->api == VriGraphicsAPI_Metal)
        {
            if (native.u.metal.device != m_Impl->metal.device || !native.u.metal.commandQueue)
                return std::unexpected("Metal device or queue does not match the OpenXR runtime");
            return {};
        }
#endif
        if (!native.u.vulkan.instance || !native.u.vulkan.device || !native.u.vulkan.physicalDevice ||
            native.u.vulkan.physicalDevice != m_Impl->hooks.physicalDevice)
            return std::unexpected("Vulkan device was not created through the OpenXR hooks");
        return {};
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
               glm::mat4_cast(glm::normalize(
                   glm::quat(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z)));
    }

    std::expected<glm::mat4, std::string> XRFrame::headPose() const
    {
        if (!shouldRender)
        {
            return std::unexpected("Head pose requires located, renderable XR views");
        }
        const auto& left  = eyes[0].view.pose;
        const auto& right = eyes[1].view.pose;
        if (!validXrPose(left) || !validXrPose(right))
        {
            return std::unexpected("XR eye pose has a non-finite position or invalid orientation");
        }
        const auto orientation = glm::slerp(
            glm::normalize(glm::quat(left.orientation.w, left.orientation.x, left.orientation.y, left.orientation.z)),
            glm::normalize(
                glm::quat(right.orientation.w, right.orientation.x, right.orientation.y, right.orientation.z)),
            0.5f);
        const glm::vec3 midpoint {left.position.x * 0.5f + right.position.x * 0.5f,
                                  left.position.y * 0.5f + right.position.y * 0.5f,
                                  left.position.z * 0.5f + right.position.z * 0.5f};
        return glm::translate(glm::mat4(1), midpoint) * glm::mat4_cast(orientation);
    }

    std::expected<glm::mat4, std::string> XREye::viewProjection(float nearZ, float farZ) const
    {
        if (!std::isfinite(nearZ) || !std::isfinite(farZ) || !(nearZ > 0 && farZ > nearZ))
        {
            return std::unexpected("Require finite clipping planes with 0 < near < far");
        }
        if (!validXrPose(view.pose))
        {
            return std::unexpected("XR eye pose has a non-finite position or invalid orientation");
        }
        const auto& f = view.fov;
        const float l = std::tan(f.angleLeft);
        const float r = std::tan(f.angleRight);
        const float u = std::tan(f.angleUp);
        const float d = std::tan(f.angleDown);
        if (!std::isfinite(l) || !std::isfinite(r) || !std::isfinite(u) || !std::isfinite(d) || l >= r || d >= u)
        {
            return std::unexpected("XR eye field of view is invalid");
        }
        glm::mat4 projection(0);
        projection[0][0] = 2 / (r - l);
        projection[1][1] = 2 / (u - d);
        projection[2][0] = (r + l) / (r - l);
        projection[2][1] = (u + d) / (u - d);
        projection[2][2] = farZ / (nearZ - farZ);
        projection[2][3] = -1;
        projection[3][2] = farZ * nearZ / (nearZ - farZ);
        return projection * glm::inverse(poseMatrix());
    }

    OpenXRSession::OpenXRSession(OpenXRSystem& system, Device& device, Uninitialized) :
        m_System(system),
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<OpenXRSession>, std::string> OpenXRSession::create(OpenXRSystem& system,
                                                                                     Device&       device)
    {
        auto session = std::unique_ptr<OpenXRSession>(new OpenXRSession(system, device, Uninitialized {}));
        if (auto result = session->initialize(); !result)
        {
            return std::unexpected(result.error());
        }
        return session;
    }

    std::expected<void, std::string> OpenXRSession::initialize()
    {
        auto& system = m_System;
        auto& device = m_Device;
#define VULTRA_XR_EXPECT(expr, operation) \
    do \
    { \
        const auto value = (expr); \
        if (XR_FAILED(value)) \
            return std::unexpected(std::string(operation) + " failed: " + std::to_string(value)); \
    } while (false)
#define VULTRA_VRI_EXPECT(expr, operation) \
    do \
    { \
        const auto value = (expr); \
        if (value != VriResult_Success) \
            return std::unexpected(std::string(operation) + " failed: " + std::to_string(value)); \
    } while (false)
        if (auto compatible = system.validateDevice(device); !compatible)
            return std::unexpected(compatible.error());
        VriInteropInterface interop {};
        VULTRA_VRI_EXPECT(vriGetInterface(device.handle, VRI_INTERFACE_INTEROP, sizeof(interop), &interop),
                          "Get VRI interop");
        VriDeviceNativeHandles native {};
        VULTRA_VRI_EXPECT(interop.GetDeviceNativeHandles(device.handle, &native), "Get native graphics handles");
        XrGraphicsBindingVulkanKHR binding {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
#if defined(__APPLE__)
        XrGraphicsBindingMetalKHR metalBinding {XR_TYPE_GRAPHICS_BINDING_METAL_KHR};
#endif
        XrSessionCreateInfo session {XR_TYPE_SESSION_CREATE_INFO};
#if defined(__APPLE__)
        if (native.api == VriGraphicsAPI_Metal)
        {
            metalBinding.commandQueue = native.u.metal.commandQueue;
            session.next              = &metalBinding;
        }
        else
#endif
        {
            binding.instance         = static_cast<VkInstance>(native.u.vulkan.instance);
            binding.physicalDevice   = static_cast<VkPhysicalDevice>(native.u.vulkan.physicalDevice);
            binding.device           = static_cast<VkDevice>(native.u.vulkan.device);
            binding.queueFamilyIndex = native.u.vulkan.graphicsQueueFamilyIndex;
            binding.queueIndex       = native.u.vulkan.graphicsQueueIndex;
            session.next             = &binding;
        }
        session.systemId = system.system();
        VULTRA_XR_EXPECT(xrCreateSession(system.instance(), &session, &m_Session), "Create XR session");
        XrReferenceSpaceCreateInfo space {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space.referenceSpaceType                 = XR_REFERENCE_SPACE_TYPE_LOCAL;
        space.poseInReferenceSpace.orientation.w = 1;
        VULTRA_XR_EXPECT(xrCreateReferenceSpace(m_Session, &space, &m_Space), "Create local reference space");
        uint32_t count = 0;
        VULTRA_XR_EXPECT(xrEnumerateViewConfigurationViews(system.instance(),
                                                           system.system(),
                                                           XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                           0,
                                                           &count,
                                                           nullptr),
                         "Count stereo views");
        if (count != 2)
        {
            return std::unexpected("Vultra requires PRIMARY_STEREO with two views");
        }
        XrViewConfigurationView views[2] {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        VULTRA_XR_EXPECT(xrEnumerateViewConfigurationViews(system.instance(),
                                                           system.system(),
                                                           XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                           2,
                                                           &count,
                                                           views),
                         "Get stereo views");
        VULTRA_XR_EXPECT(xrEnumerateEnvironmentBlendModes(system.instance(),
                                                          system.system(),
                                                          XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                          0,
                                                          &count,
                                                          nullptr),
                         "Count blend modes");
        std::vector<XrEnvironmentBlendMode> modes(count);
        VULTRA_XR_EXPECT(xrEnumerateEnvironmentBlendModes(system.instance(),
                                                          system.system(),
                                                          XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                          count,
                                                          &count,
                                                          modes.data()),
                         "Get blend modes");
        if (modes.empty())
        {
            return std::unexpected("No XR environment blend mode");
        }
        m_Blend = std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) != modes.end() ?
                      XR_ENVIRONMENT_BLEND_MODE_OPAQUE :
                      modes.front();
        VULTRA_XR_EXPECT(xrEnumerateSwapchainFormats(m_Session, 0, &count, nullptr), "Count XR formats");
        std::vector<int64_t> formats(count);
        VULTRA_XR_EXPECT(xrEnumerateSwapchainFormats(m_Session, count, &count, formats.data()), "Get XR formats");
        std::array<std::pair<int64_t, VriFormat>, 4> candidates {{{VK_FORMAT_R8G8B8A8_SRGB, VriFormat_RGBA8_SRGB},
                                                                  {VK_FORMAT_B8G8R8A8_SRGB, VriFormat_BGRA8_SRGB},
                                                                  {VK_FORMAT_R8G8B8A8_UNORM, VriFormat_RGBA8_UNORM},
                                                                  {VK_FORMAT_B8G8R8A8_UNORM, VriFormat_BGRA8_UNORM}}};
#if defined(__APPLE__)
        if (native.api == VriGraphicsAPI_Metal)
            candidates = metalXrFormats();
#endif
        int64_t format = 0;
        for (const auto& [candidate, vriFormat] : candidates)
        {
            if (std::find(formats.begin(), formats.end(), candidate) != formats.end())
            {
                format   = candidate;
                m_Format = vriFormat;
                break;
            }
        }
        if (m_Format == VriFormat_Unknown)
            return std::unexpected("XR runtime has no supported RGBA/BGRA8 format");
        for (size_t e = 0; e < 2; ++e)
        {
            auto& eye = m_Eyes[e];
            eye.size  = {views[e].recommendedImageRectWidth, views[e].recommendedImageRectHeight};
            XrSwapchainCreateInfo info {XR_TYPE_SWAPCHAIN_CREATE_INFO};
            info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
#if defined(__ANDROID__)
            // Hardware-buffer imports can back an sRGB swapchain with a UNORM image and sRGB views.
            if (m_Format == VriFormat_RGBA8_SRGB || m_Format == VriFormat_BGRA8_SRGB)
            {
                info.usageFlags |= XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
            }
#endif
            info.format      = format;
            info.width       = eye.size.width;
            info.height      = eye.size.height;
            info.sampleCount = 1;
            info.faceCount   = 1;
            info.arraySize   = 1;
            info.mipCount    = 1;
            VULTRA_XR_EXPECT(xrCreateSwapchain(m_Session, &info, &eye.handle), "Create eye swapchain");
            VULTRA_XR_EXPECT(xrEnumerateSwapchainImages(eye.handle, 0, &count, nullptr), "Count eye images");
            std::vector<void*> nativeImages;
            nativeImages.reserve(count);
#if defined(__APPLE__)
            if (native.api == VriGraphicsAPI_Metal)
            {
                std::vector<XrSwapchainImageMetalKHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_METAL_KHR});
                VULTRA_XR_EXPECT(
                    xrEnumerateSwapchainImages(eye.handle,
                                               count,
                                               &count,
                                               reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                    "Get Metal eye images");
                for (const auto& image : images)
                    nativeImages.push_back(image.texture);
            }
            else
#endif
            {
                std::vector<XrSwapchainImageVulkanKHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
                VULTRA_XR_EXPECT(
                    xrEnumerateSwapchainImages(eye.handle,
                                               count,
                                               &count,
                                               reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                    "Get Vulkan eye images");
                for (const auto& image : images)
                    nativeImages.push_back(image.image);
            }
            eye.wrappers.reserve(count);
            eye.textures.reserve(count);
            for (auto* image : nativeImages)
            {
                VriWrapTextureDesc wrap {};
                wrap.nativeTexture  = image;
                wrap.desc           = colorTexture(eye.size, m_Format);
                wrap.desc.usage     = VriTextureUsage_ColorAttachment | VriTextureUsage_ShaderResource;
                VriTexture* texture = nullptr;
                VULTRA_VRI_EXPECT(interop.WrapTexture(device.handle, &wrap, &texture), "Wrap eye image");
                eye.wrappers.push_back(texture);
                eye.textures.push_back(std::make_unique<Texture>(device, wrap.desc, texture));
            }
        }
#undef VULTRA_XR_EXPECT
#undef VULTRA_VRI_EXPECT
        return {};
    }

    OpenXRSession::~OpenXRSession()
    {
        cleanup();
    }

    void OpenXRSession::cleanup() noexcept
    {
        m_Device.waitIdleForDestruction();
        // Pair an interrupted frame with zero layers where the runtime still permits it.
        m_Frame.shouldRender = false;
        if (m_Frame.begun)
        {
            if (auto result = endFrame(); !result)
            {
                Logger::core().error("[OpenXR] End interrupted frame during cleanup: {}", result.error());
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
                reportXrCleanup(xrDestroySwapchain(eye.handle), "Destroy swapchain");
            }
            eye.handle = XR_NULL_HANDLE;
        }
        if (m_Space)
        {
            reportXrCleanup(xrDestroySpace(m_Space), "Destroy space");
        }
        if (m_Session)
        {
            reportXrCleanup(xrDestroySession(m_Session), "Destroy session");
        }
        m_Space   = XR_NULL_HANDLE;
        m_Session = XR_NULL_HANDLE;
    }

#define VULTRA_FRAME_XR_EXPECT(expr, operation) \
    do \
    { \
        const auto value = (expr); \
        if (XR_FAILED(value)) \
            return std::unexpected(std::string(operation) + " failed: " + std::to_string(value)); \
    } while (false)

    std::expected<void, std::string> OpenXRSession::pollEventsExpected()
    {
        XrEventDataBuffer event {XR_TYPE_EVENT_DATA_BUFFER};
        XrResult          result;
        while ((result = xrPollEvent(m_System.instance(), &event)) == XR_SUCCESS)
        {
            if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            {
                m_Lost          = true;
                m_ExitCompleted = false;
                m_Exiting       = true;
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
                        VULTRA_FRAME_XR_EXPECT(xrBeginSession(m_Session, &info), "Begin XR session");
                        m_Running = true;
                    }
                    if (changed.state == XR_SESSION_STATE_STOPPING && m_Running)
                    {
                        const auto ended = xrEndSession(m_Session);
                        // OpenXR makes the session non-running even when xrEndSession fails.
                        m_Running = false;
                        VULTRA_FRAME_XR_EXPECT(ended, "End XR session");
                    }
                    if (changed.state == XR_SESSION_STATE_EXITING)
                    {
                        m_ExitCompleted = m_ExitRequested && !m_Running && !m_Lost;
                        m_Exiting       = true;
                    }
                    if (changed.state == XR_SESSION_STATE_LOSS_PENDING)
                    {
                        m_Lost          = true;
                        m_ExitCompleted = false;
                        m_Exiting       = true;
                    }
                }
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (result != XR_EVENT_UNAVAILABLE)
        {
            VULTRA_FRAME_XR_EXPECT(result, "Poll XR events");
        }
        return {};
    }

    std::expected<XRFrame, std::string> OpenXRSession::beginFrame()
    {
        if (m_Frame.begun)
        {
            return std::unexpected("End the previous XR frame first");
        }
        m_Frame = {};
        if (auto events = pollEventsExpected(); !events)
        {
            return std::unexpected(events.error());
        }
        if (!m_Running || m_Exiting)
        {
            return m_Frame;
        }
        XrFrameWaitInfo wait {XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState    state {XR_TYPE_FRAME_STATE};
        VULTRA_FRAME_XR_EXPECT(xrWaitFrame(m_Session, &wait, &state), "Wait XR frame");
        XrFrameBeginInfo begin {XR_TYPE_FRAME_BEGIN_INFO};
        VULTRA_FRAME_XR_EXPECT(xrBeginFrame(m_Session, &begin), "Begin XR frame");
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
        VULTRA_FRAME_XR_EXPECT(xrLocateViews(m_Session, &locate, &viewState, 2, &count, views), "Locate eye views");
        const auto valid = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
        if (count != 2 || (viewState.viewStateFlags & valid) != valid)
        {
            return m_Frame;
        }
        for (size_t e = 0; e < 2; ++e)
        {
            auto&                       eye = m_Eyes[e];
            XrSwapchainImageAcquireInfo acquire {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            VULTRA_FRAME_XR_EXPECT(xrAcquireSwapchainImage(eye.handle, &acquire, &eye.index), "Acquire eye image");
            eye.acquired = true;
            XrSwapchainImageWaitInfo imageWait {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            imageWait.timeout = XR_INFINITE_DURATION;
            VULTRA_FRAME_XR_EXPECT(xrWaitSwapchainImage(eye.handle, &imageWait), "Wait eye image");
            eye.waited = true;
            if (eye.index >= eye.textures.size())
            {
                return std::unexpected("XR acquired an invalid swapchain image index");
            }
            auto& target = *eye.textures[eye.index];
            target.setState({}); // XR acquisition discards the previous frame's contents
            m_Frame.eyes[e] = {&target, views[e]};
        }
        m_Frame.shouldRender = true;
        return m_Frame;
    }

    std::expected<void, std::string> OpenXRSession::requestExit()
    {
        if (m_Frame.begun)
        {
            return std::unexpected("End the current XR frame before requesting exit");
        }
        if (m_ExitRequested || m_Exiting)
        {
            return {};
        }
        if (!m_Running)
        {
            return std::unexpected("XR session is not running");
        }
        VULTRA_FRAME_XR_EXPECT(xrRequestExitSession(m_Session), "Request XR session exit");
        m_ExitRequested = true;
        return {};
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

    std::expected<void, std::string> OpenXRSession::endFrame()
    {
        if (!m_Frame.begun)
        {
            return {};
        }
        XrCompositionLayerProjectionView views[2] {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                   {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        for (size_t e = 0; e < 2; ++e)
        {
            auto& eye = m_Eyes[e];
            if (eye.acquired && eye.waited)
            {
                XrSwapchainImageReleaseInfo release {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                VULTRA_FRAME_XR_EXPECT(xrReleaseSwapchainImage(eye.handle, &release), "Release eye image");
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
        VULTRA_FRAME_XR_EXPECT(xrEndFrame(m_Session, &end), "End XR frame");
        return {};
    }

#undef VULTRA_FRAME_XR_EXPECT

} // namespace vultra
