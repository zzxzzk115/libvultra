#define XR_USE_GRAPHICS_API_VULKAN
#include <vultra/core/base/logger.hpp>
#include <vultra/xr/openxr.hpp>

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
#include <span>
#include <string>
#include <utility>

namespace vultra
{
    namespace
    {
        [[nodiscard]] std::expected<void, Error> checkXr(XrResult result, const char* operation)
        {
            if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
            {
                return makeError("{}: the OpenXR runtime reports no available headset", operation);
            }
            if (XR_FAILED(result))
            {
                return makeError("{} failed: {}", operation, int(result));
            }
            return {};
        }

        template<class F>
        [[nodiscard]] std::expected<F, Error> loadXr(XrInstance instance, const char* name)
        {
            PFN_xrVoidFunction function = nullptr;
            if (auto result = checkXr(xrGetInstanceProcAddr(instance, name, &function), name); !result)
            {
                return std::unexpected(result.error());
            }
            if (!function)
            {
                return makeError("Missing XR function: {}", name);
            }
            return reinterpret_cast<F>(function);
        }

        glm::mat4 toMatrix(const XrPosef& pose)
        {
            return glm::translate(glm::mat4(1), glm::vec3(pose.position.x, pose.position.y, pose.position.z)) *
                   glm::mat4_cast(
                       glm::quat(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z));
        }

        [[nodiscard]] std::expected<XrPath, Error> toPath(XrInstance instance, const std::string& text)
        {
            XrPath path = XR_NULL_PATH;
            if (auto result = checkXr(xrStringToPath(instance, text.c_str(), &path), "Get XR path"); !result)
            {
                return std::unexpected(result.error());
            }
            return path;
        }

        // Creation hooks cross a C ABI: log both results and return a VkResult to VRI.
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

    OpenXRSystem::OpenXRSystem() :
        m_Impl(std::make_unique<Impl>())
    {
    }

    std::expected<std::unique_ptr<OpenXRSystem>, Error> OpenXRSystem::create(const char* applicationName)
    {
        auto                 system    = std::unique_ptr<OpenXRSystem>(new OpenXRSystem());
        auto&                s         = *system->m_Impl;
        const char*          extension = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
        XrInstanceCreateInfo info {XR_TYPE_INSTANCE_CREATE_INFO};
        std::strncpy(info.applicationInfo.applicationName, applicationName, XR_MAX_APPLICATION_NAME_SIZE - 1);
        info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        info.enabledExtensionCount      = 1;
        info.enabledExtensionNames      = &extension;
        if (auto result =
                checkXr(xrCreateInstance(&info, &s.instance), "Create OpenXR instance (check active runtime)");
            !result)
        {
            return std::unexpected(result.error());
        }
        XrSystemGetInfo systemInfo {XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (auto result = checkXr(xrGetSystem(s.instance, &systemInfo, &s.system), "Get OpenXR HMD system"); !result)
        {
            return std::unexpected(result.error());
        }
        const auto getRequirements =
            loadXr<PFN_xrGetVulkanGraphicsRequirements2KHR>(s.instance, "xrGetVulkanGraphicsRequirements2KHR");
        if (!getRequirements)
        {
            return std::unexpected(getRequirements.error());
        }
        if (auto result =
                checkXr((*getRequirements)(s.instance, s.system, &s.requirements), "Get Vulkan graphics requirements");
            !result)
        {
            return std::unexpected(result.error());
        }
        const auto createInstance = loadXr<PFN_xrCreateVulkanInstanceKHR>(s.instance, "xrCreateVulkanInstanceKHR");
        if (!createInstance)
        {
            return std::unexpected(createInstance.error());
        }
        const auto createDevice = loadXr<PFN_xrCreateVulkanDeviceKHR>(s.instance, "xrCreateVulkanDeviceKHR");
        if (!createDevice)
        {
            return std::unexpected(createDevice.error());
        }
        const auto getDevice = loadXr<PFN_xrGetVulkanGraphicsDevice2KHR>(s.instance, "xrGetVulkanGraphicsDevice2KHR");
        if (!getDevice)
        {
            return std::unexpected(getDevice.error());
        }
        s.createInstance = *createInstance;
        s.createDevice   = *createDevice;
        s.getDevice      = *getDevice;
        s.hooks          = {VriGraphicsAPI_Vulkan, Impl::onCreateInstance, Impl::onCreateDevice, &s, nullptr};
        return system;
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
        return toMatrix(view.pose);
    }

    Ray XRHand::pointer() const
    {
        return {glm::vec3(aim[3]), -glm::normalize(glm::vec3(aim[2]))};
    }

    std::expected<glm::mat4, Error> XRFrame::headPose() const
    {
        if (!shouldRender)
        {
            return makeError("Head pose requires located, renderable XR views");
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

    std::expected<glm::mat4, Error> XREye::viewProjection(float nearZ, float farZ) const
    {
        const auto eye = camera(nearZ, farZ);
        if (!eye)
        {
            return std::unexpected(eye.error());
        }
        return eye->projection * eye->view;
    }

    std::expected<RenderCamera, Error> XREye::camera(float nearZ, float farZ, const glm::mat4& origin) const
    {
        if (!(nearZ > 0 && farZ > nearZ))
        {
            return makeError("Require 0 < near < far");
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
        return RenderCamera {glm::inverse(origin * poseMatrix()), projection, nearZ, farZ};
    }

    OpenXRSession::OpenXRSession(OpenXRSystem& system, Device& device) :
        m_System(system),
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<OpenXRSession>, Error> OpenXRSession::create(OpenXRSystem& system, Device& device)
    {
        // The destructor releases whatever initialize() created before a failure.
        auto session = std::unique_ptr<OpenXRSession>(new OpenXRSession(system, device));
        if (auto result = session->initialize(); !result)
        {
            return std::unexpected(result.error());
        }
        return session;
    }

    std::expected<void, Error> OpenXRSession::initialize()
    {
        VriInteropInterface interop {};
        if (auto result = check(vriGetInterface(m_Device.handle, VRI_INTERFACE_INTEROP, sizeof(interop), &interop),
                                "Get VRI interop");
            !result)
        {
            return result;
        }
        VriDeviceNativeHandles native {};
        if (auto result = check(interop.GetDeviceNativeHandles(m_Device.handle, &native), "Get Vulkan handles");
            !result)
        {
            return result;
        }
        XrGraphicsBindingVulkanKHR binding {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
        binding.instance         = static_cast<VkInstance>(native.u.vulkan.instance);
        binding.physicalDevice   = static_cast<VkPhysicalDevice>(native.u.vulkan.physicalDevice);
        binding.device           = static_cast<VkDevice>(native.u.vulkan.device);
        binding.queueFamilyIndex = native.u.vulkan.graphicsQueueFamilyIndex;
        binding.queueIndex       = native.u.vulkan.graphicsQueueIndex;
        XrSessionCreateInfo session {XR_TYPE_SESSION_CREATE_INFO};
        session.next     = &binding;
        session.systemId = m_System.system();
        if (auto result = checkXr(xrCreateSession(m_System.instance(), &session, &m_Session), "Create XR session");
            !result)
        {
            return result;
        }
        XrReferenceSpaceCreateInfo space {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space.referenceSpaceType                 = XR_REFERENCE_SPACE_TYPE_LOCAL;
        space.poseInReferenceSpace.orientation.w = 1;
        if (auto result = checkXr(xrCreateReferenceSpace(m_Session, &space, &m_Space), "Create local reference space");
            !result)
        {
            return result;
        }
        uint32_t count = 0;
        if (auto result = checkXr(xrEnumerateViewConfigurationViews(m_System.instance(),
                                                                    m_System.system(),
                                                                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                                    0,
                                                                    &count,
                                                                    nullptr),
                                  "Count stereo views");
            !result)
        {
            return result;
        }
        if (count != 2)
        {
            return makeError("Vultra requires PRIMARY_STEREO with two views");
        }
        XrViewConfigurationView views[2] {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        if (auto result = checkXr(xrEnumerateViewConfigurationViews(m_System.instance(),
                                                                    m_System.system(),
                                                                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                                    2,
                                                                    &count,
                                                                    views),
                                  "Get stereo views");
            !result)
        {
            return result;
        }
        if (auto result = checkXr(xrEnumerateEnvironmentBlendModes(m_System.instance(),
                                                                   m_System.system(),
                                                                   XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                                   0,
                                                                   &count,
                                                                   nullptr),
                                  "Count blend modes");
            !result)
        {
            return result;
        }
        std::vector<XrEnvironmentBlendMode> modes(count);
        if (auto result = checkXr(xrEnumerateEnvironmentBlendModes(m_System.instance(),
                                                                   m_System.system(),
                                                                   XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                                   count,
                                                                   &count,
                                                                   modes.data()),
                                  "Get blend modes");
            !result)
        {
            return result;
        }
        if (modes.empty())
        {
            return makeError("No XR environment blend mode");
        }
        m_Blend = std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) != modes.end() ?
                      XR_ENVIRONMENT_BLEND_MODE_OPAQUE :
                      modes.front();
        if (auto result = checkXr(xrEnumerateSwapchainFormats(m_Session, 0, &count, nullptr), "Count XR formats");
            !result)
        {
            return result;
        }
        std::vector<int64_t> formats(count);
        if (auto result =
                checkXr(xrEnumerateSwapchainFormats(m_Session, count, &count, formats.data()), "Get XR formats");
            !result)
        {
            return result;
        }
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
                return makeError("XR runtime has no supported RGBA/BGRA8 format");
        }
        for (size_t e = 0; e < 2; ++e)
        {
            auto& eye = m_Eyes[e];
            eye.size  = {views[e].recommendedImageRectWidth, views[e].recommendedImageRectHeight};
            XrSwapchainCreateInfo info {XR_TYPE_SWAPCHAIN_CREATE_INFO};
            // The transfer usages let a mirror or a readback copy the eye image before it is released, and let
            // a host copy a finished image into it instead of drawing.
            info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                              XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.format      = format;
            info.width       = eye.size.width;
            info.height      = eye.size.height;
            info.sampleCount = 1;
            info.faceCount   = 1;
            info.arraySize   = 1;
            info.mipCount    = 1;
            if (auto result = checkXr(xrCreateSwapchain(m_Session, &info, &eye.handle), "Create eye swapchain");
                !result)
            {
                return result;
            }
            if (auto result = checkXr(xrEnumerateSwapchainImages(eye.handle, 0, &count, nullptr), "Count eye images");
                !result)
            {
                return result;
            }
            std::vector<XrSwapchainImageVulkanKHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
            if (auto result =
                    checkXr(xrEnumerateSwapchainImages(eye.handle,
                                                       count,
                                                       &count,
                                                       reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                            "Get eye images");
                !result)
            {
                return result;
            }
            eye.wrappers.reserve(count);
            eye.textures.reserve(count);
            for (const auto& image : images)
            {
                VriWrapTextureDesc wrap {};
                wrap.nativeTexture = image.image;
                wrap.desc          = colorTexture(eye.size, m_Format);
                wrap.desc.usage    = VriTextureUsage_ColorAttachment | VriTextureUsage_ShaderResource |
                                  VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
                VriTexture* wrapper = nullptr;
                if (auto result = check(interop.WrapTexture(m_Device.handle, &wrap, &wrapper), "Wrap eye image");
                    !result)
                {
                    return result;
                }
                eye.wrappers.push_back(wrapper);
                auto texture = Texture::create(m_Device, wrap.desc, wrapper);
                if (!texture)
                {
                    return std::unexpected(texture.error());
                }
                eye.textures.push_back(std::move(*texture));
            }
        }
        return initializeInput();
    }

    std::expected<void, Error> OpenXRSession::initializeInput()
    {
        const XrInstance instance = m_System.instance();
        const std::array hands {std::string("/user/hand/left"), std::string("/user/hand/right")};
        for (size_t hand = 0; hand < 2; ++hand)
        {
            const auto path = toPath(instance, hands[hand]);
            if (!path)
            {
                return std::unexpected(path.error());
            }
            m_HandPaths[hand] = *path;
        }
        XrActionSetCreateInfo setInfo {XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strncpy(setInfo.actionSetName, "vultra", XR_MAX_ACTION_SET_NAME_SIZE - 1);
        std::strncpy(setInfo.localizedActionSetName, "Vultra", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
        if (auto result = checkXr(xrCreateActionSet(instance, &setInfo, &m_ActionSet), "Create XR action set"); !result)
        {
            return result;
        }
        const auto createAction = [this](XrActionType type, const char* name, const char* label, XrAction& action)
        {
            XrActionCreateInfo info {XR_TYPE_ACTION_CREATE_INFO};
            info.actionType          = type;
            info.countSubactionPaths = uint32_t(m_HandPaths.size());
            info.subactionPaths      = m_HandPaths.data();
            std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
            std::strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
            return checkXr(xrCreateAction(m_ActionSet, &info, &action), "Create XR action");
        };
        if (auto result = createAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip pose", m_GripAction); !result)
        {
            return result;
        }
        if (auto result = createAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim pose", m_AimAction); !result)
        {
            return result;
        }
        if (auto result = createAction(XR_ACTION_TYPE_FLOAT_INPUT, "select", "Select", m_SelectAction); !result)
        {
            return result;
        }
        if (auto result = createAction(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Squeeze", m_SqueezeAction); !result)
        {
            return result;
        }

        // The simple controller is the profile every runtime accepts; it has a select button and no grip
        // button. The Touch profile adds the analog trigger and grip of Quest controllers.
        struct Input
        {
            XrAction    action;
            const char* component;
        };

        const auto suggest = [&](const char* profile, std::span<const Input> inputs) -> std::expected<void, Error>
        {
            std::vector<XrActionSuggestedBinding> bindings;
            for (const auto& input : inputs)
            {
                for (const auto& hand : hands)
                {
                    const auto path = toPath(instance, hand + input.component);
                    if (!path)
                    {
                        return std::unexpected(path.error());
                    }
                    bindings.push_back({input.action, *path});
                }
            }
            const auto profilePath = toPath(instance, profile);
            if (!profilePath)
            {
                return std::unexpected(profilePath.error());
            }
            XrInteractionProfileSuggestedBinding suggested {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            suggested.interactionProfile     = *profilePath;
            suggested.countSuggestedBindings = uint32_t(bindings.size());
            suggested.suggestedBindings      = bindings.data();
            return checkXr(xrSuggestInteractionProfileBindings(instance, &suggested), "Suggest XR bindings");
        };
        const std::array simple {Input {m_GripAction, "/input/grip/pose"},
                                 Input {m_AimAction, "/input/aim/pose"},
                                 Input {m_SelectAction, "/input/select/click"}};
        if (auto result = suggest("/interaction_profiles/khr/simple_controller", simple); !result)
        {
            return result;
        }
        const std::array touch {Input {m_GripAction, "/input/grip/pose"},
                                Input {m_AimAction, "/input/aim/pose"},
                                Input {m_SelectAction, "/input/trigger/value"},
                                Input {m_SqueezeAction, "/input/squeeze/value"}};
        if (auto result = suggest("/interaction_profiles/oculus/touch_controller", touch); !result)
        {
            return result;
        }

        for (size_t hand = 0; hand < 2; ++hand)
        {
            XrActionSpaceCreateInfo info {XR_TYPE_ACTION_SPACE_CREATE_INFO};
            info.subactionPath                   = m_HandPaths[hand];
            info.poseInActionSpace.orientation.w = 1;
            info.action                          = m_GripAction;
            if (auto result = checkXr(xrCreateActionSpace(m_Session, &info, &m_GripSpaces[hand]), "Create grip space");
                !result)
            {
                return result;
            }
            info.action = m_AimAction;
            if (auto result = checkXr(xrCreateActionSpace(m_Session, &info, &m_AimSpaces[hand]), "Create aim space");
                !result)
            {
                return result;
            }
        }
        XrSessionActionSetsAttachInfo attach {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1;
        attach.actionSets      = &m_ActionSet;
        return checkXr(xrAttachSessionActionSets(m_Session, &attach), "Attach XR action set");
    }

    std::expected<void, Error> OpenXRSession::locateHands(XrTime time)
    {
        const XrActiveActionSet active {m_ActionSet, XR_NULL_PATH};
        XrActionsSyncInfo       sync {XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets      = &active;
        const XrResult synced      = xrSyncActions(m_Session, &sync);
        if (auto result = checkXr(synced, "Sync XR actions"); !result)
        {
            return result;
        }
        if (synced == XR_SESSION_NOT_FOCUSED)
        {
            return {};
        }
        const auto readFloat = [this](XrAction action, XrPath hand) -> std::expected<float, Error>
        {
            XrActionStateGetInfo info {XR_TYPE_ACTION_STATE_GET_INFO};
            info.action        = action;
            info.subactionPath = hand;
            XrActionStateFloat state {XR_TYPE_ACTION_STATE_FLOAT};
            if (auto result = checkXr(xrGetActionStateFloat(m_Session, &info, &state), "Get XR action state"); !result)
            {
                return std::unexpected(result.error());
            }
            return state.isActive ? state.currentState : 0.0f;
        };
        constexpr XrSpaceLocationFlags located =
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        for (size_t index = 0; index < 2; ++index)
        {
            XrSpaceLocation grip {XR_TYPE_SPACE_LOCATION};
            if (auto result = checkXr(xrLocateSpace(m_GripSpaces[index], m_Space, time, &grip), "Locate grip space");
                !result)
            {
                return result;
            }
            XrSpaceLocation aim {XR_TYPE_SPACE_LOCATION};
            if (auto result = checkXr(xrLocateSpace(m_AimSpaces[index], m_Space, time, &aim), "Locate aim space");
                !result)
            {
                return result;
            }
            if ((grip.locationFlags & located) != located || (aim.locationFlags & located) != located)
            {
                continue;
            }
            const auto select = readFloat(m_SelectAction, m_HandPaths[index]);
            if (!select)
            {
                return std::unexpected(select.error());
            }
            const auto squeeze = readFloat(m_SqueezeAction, m_HandPaths[index]);
            if (!squeeze)
            {
                return std::unexpected(squeeze.error());
            }
            m_Frame.hands[index] = {true, toMatrix(grip.pose), toMatrix(aim.pose), *select, *squeeze};
        }
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
                Logger::core().error("[OpenXR] Pairing the interrupted frame during shutdown: {}",
                                     result.error().message);
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
        for (auto& space : m_GripSpaces)
        {
            if (space)
            {
                xrDestroySpace(space);
            }
            space = XR_NULL_HANDLE;
        }
        for (auto& space : m_AimSpaces)
        {
            if (space)
            {
                xrDestroySpace(space);
            }
            space = XR_NULL_HANDLE;
        }
        if (m_Space)
        {
            xrDestroySpace(m_Space);
        }
        if (m_Session)
        {
            xrDestroySession(m_Session);
        }
        // The action set belongs to the instance, not to the session.
        if (m_ActionSet)
        {
            xrDestroyActionSet(m_ActionSet);
        }
        m_ActionSet = XR_NULL_HANDLE;
        m_Space     = XR_NULL_HANDLE;
        m_Session   = XR_NULL_HANDLE;
    }

    std::expected<void, Error> OpenXRSession::pollEvents()
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
                        if (auto begun = checkXr(xrBeginSession(m_Session, &info), "Begin XR session"); !begun)
                        {
                            return begun;
                        }
                        m_Running = true;
                    }
                    if (changed.state == XR_SESSION_STATE_STOPPING && m_Running)
                    {
                        if (auto ended = checkXr(xrEndSession(m_Session), "End XR session"); !ended)
                        {
                            return ended;
                        }
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
            return checkXr(result, "Poll XR events");
        }
        return {};
    }

    std::expected<XRFrame, Error> OpenXRSession::beginFrame()
    {
        if (m_Frame.begun)
        {
            return makeError("End the previous XR frame first");
        }
        m_Frame = {};
        if (auto result = pollEvents(); !result)
        {
            return std::unexpected(result.error());
        }
        if (!m_Running || m_Exiting)
        {
            return m_Frame;
        }
        XrFrameWaitInfo wait {XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState    state {XR_TYPE_FRAME_STATE};
        if (auto result = checkXr(xrWaitFrame(m_Session, &wait, &state), "Wait XR frame"); !result)
        {
            return std::unexpected(result.error());
        }
        XrFrameBeginInfo begin {XR_TYPE_FRAME_BEGIN_INFO};
        if (auto result = checkXr(xrBeginFrame(m_Session, &begin), "Begin XR frame"); !result)
        {
            return std::unexpected(result.error());
        }
        m_Frame.begun       = true;
        m_Frame.displayTime = state.predictedDisplayTime;
        if (auto result = locateHands(state.predictedDisplayTime); !result)
        {
            return std::unexpected(result.error());
        }
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
        if (auto result = checkXr(xrLocateViews(m_Session, &locate, &viewState, 2, &count, views), "Locate eye views");
            !result)
        {
            return std::unexpected(result.error());
        }
        const auto valid = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
        if (count != 2 || (viewState.viewStateFlags & valid) != valid)
        {
            return m_Frame;
        }
        for (size_t e = 0; e < 2; ++e)
        {
            auto&                       eye = m_Eyes[e];
            XrSwapchainImageAcquireInfo acquire {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (auto result = checkXr(xrAcquireSwapchainImage(eye.handle, &acquire, &eye.index), "Acquire eye image");
                !result)
            {
                return std::unexpected(result.error());
            }
            eye.acquired = true;
            XrSwapchainImageWaitInfo imageWait {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            imageWait.timeout = XR_INFINITE_DURATION;
            if (auto result = checkXr(xrWaitSwapchainImage(eye.handle, &imageWait), "Wait eye image"); !result)
            {
                return std::unexpected(result.error());
            }
            eye.waited   = true;
            auto& target = eye.textures.at(eye.index);
            target.discardContents(); // XR acquisition discards the previous frame's contents
            m_Frame.eyes[e] = {&target, views[e]};
        }
        m_Frame.shouldRender = true;
        return m_Frame;
    }

    std::expected<void, Error> OpenXRSession::requestExit()
    {
        if (!m_Running)
        {
            // Nothing to stop: the session never began or has already ended.
            m_Exiting = true;
            return {};
        }
        return checkXr(xrRequestExitSession(m_Session), "Request XR session exit");
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

    std::expected<void, Error> OpenXRSession::endFrame()
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
                if (auto result = checkXr(xrReleaseSwapchainImage(eye.handle, &release), "Release eye image"); !result)
                {
                    return result;
                }
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
        return checkXr(xrEndFrame(m_Session, &end), "End XR frame");
    }
} // namespace vultra
