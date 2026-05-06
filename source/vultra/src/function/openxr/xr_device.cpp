#include "vultra/function/openxr/xr_device.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/function/openxr/xr_debug_utils.hpp"
#include "vultra/function/openxr/xr_helper.hpp"
#include "vultra/function/openxr/xr_utils.hpp"

#include <magic_enum/magic_enum.hpp>

#include <fmt/format.h>

#include <cstdio>
#include <cstring>

namespace vultra
{
    namespace openxr
    {
        namespace
        {
            void throwOnXrFailure(const XrResult result, const std::string_view message)
            {
                if (XR_FAILED(result))
                {
                    const auto msg = fmt::format("[OpenXR] {} (code: {})", message, static_cast<int>(result));
                    VULTRA_CORE_ERROR(msg);
                    throw std::runtime_error(msg);
                }
            }
        } // namespace

        XRDevice::XRDevice(const XRDeviceFeatureFlagBits flagBits, std::string_view appName) :
            m_FeatureFlagBits(flagBits), m_AppName(appName.data())
        {
            // Currently, only support VR mode
            if (flagBits != XRDeviceFeatureFlagBits::eVR)
            {
                const auto msg =
                    fmt::format("[OpenXR] Unsupported XRDeviceFeatureFlagBits: {0}, Only VR mode is supported for now.",
                                magic_enum::enum_name(flagBits));
                VULTRA_CORE_ERROR(msg);
                throw std::runtime_error(msg);
            }

            // Create an OpenXR instance
            createXrInstance();

#ifndef __APPLE__
            // Create the debug utils messenger
            createXrDebugUtilsMessenger();
#endif

            // Get necessary data
            getInstanceProperties();
            getSystemID();
            getEnvironmentBlendModes();

            loadXrFunctions();
        }

        XRDevice::~XRDevice()
        {
#ifndef __APPLE__
            destroyXrDebugUtilsMessenger();
#endif
            destroyXrInstance();
        }

        void XRDevice::createXrInstance()
        {
            XrApplicationInfo ai {};
            std::snprintf(ai.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "%s", m_AppName.c_str());
            ai.applicationVersion = 1;
            std::snprintf(ai.engineName, XR_MAX_ENGINE_NAME_SIZE, "%s", "Vultra");
            ai.engineVersion = 1;
            ai.apiVersion    = XR_API_VERSION_1_0;

            // for the Multi-view
            m_XrInstanceExtensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);

            // Eye tracking
            m_XrInstanceExtensions.push_back(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);

#ifdef _DEBUG
            // Add the OpenXR debug instance extension
            m_XrInstanceExtensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

            // Get all the API Layers from the OpenXR runtime.
            uint32_t                          apiLayerCount = 0;
            std::vector<XrApiLayerProperties> apiLayerProperties;
            OPENXR_CHECK(xrEnumerateApiLayerProperties(0, &apiLayerCount, nullptr),
                         "Failed to enumerate ApiLayerProperties.");
            apiLayerProperties.resize(apiLayerCount, {.type = XR_TYPE_API_LAYER_PROPERTIES});
            OPENXR_CHECK(xrEnumerateApiLayerProperties(apiLayerCount, &apiLayerCount, apiLayerProperties.data()),
                         "Failed to enumerate ApiLayerProperties.");

            // Check the requested API layers against the ones from the OpenXR. If found add it to the Active API
            // Layers.
            for (auto& requestLayer : m_XrApiLayers)
            {
                for (auto& layerProperty : apiLayerProperties)
                {
                    // strcmp returns 0 if the strings match.
                    if (strcmp(requestLayer.c_str(), layerProperty.layerName) == 0)
                    {
                        m_XrActiveAPILayers.push_back(requestLayer.c_str());
                        break;
                    }
                }
            }

            // Get all the Instance Extensions from the OpenXR instance.
            uint32_t                           extensionCount = 0;
            std::vector<XrExtensionProperties> extensionProperties;
            OPENXR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr),
                         "Failed to enumerate InstanceExtensionProperties.");
            extensionProperties.resize(extensionCount, {.type = XR_TYPE_EXTENSION_PROPERTIES});
            OPENXR_CHECK(xrEnumerateInstanceExtensionProperties(
                             nullptr, extensionCount, &extensionCount, extensionProperties.data()),
                         "Failed to enumerate InstanceExtensionProperties.");

            // Check the requested Instance Extensions against the ones from the OpenXR runtime.
            // If an extension is found add it to Active Instance Extensions.
            // Log error if the Instance Extension is not found.
            for (auto& extensionProperty : extensionProperties)
            {
                VULTRA_CORE_TRACE(
                    "[OpenXR] EXT: {}-{}", extensionProperty.extensionName, extensionProperty.extensionVersion);
            }
            for (auto& requestedInstanceExtension : m_XrInstanceExtensions)
            {
                bool found = false;
                for (auto& extensionProperty : extensionProperties)
                {
                    // strcmp returns 0 if the strings match.
                    if (strcmp(requestedInstanceExtension.c_str(), extensionProperty.extensionName) == 0)
                    {
                        m_XrActiveInstanceExtensions.push_back(requestedInstanceExtension.c_str());
                        found = true;
                        VULTRA_CORE_TRACE("[OpenXR] Enabled EXT: {}-{}",
                                          extensionProperty.extensionName,
                                          extensionProperty.extensionVersion);
                        break;
                    }
                }
                if (!found)
                {
                    VULTRA_CORE_WARN("[OpenXR] Unsupported OpenXR instance extension: {0}", requestedInstanceExtension);
                }
            }

            if (!IsStringInVector(m_XrActiveInstanceExtensions, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))
            {
                throw std::runtime_error("[OpenXR] Required extension XR_KHR_vulkan_enable2 is not supported");
            }

            XrInstanceCreateInfo instanceCI {};
            instanceCI.type                  = XR_TYPE_INSTANCE_CREATE_INFO;
            instanceCI.createFlags           = 0;
            instanceCI.applicationInfo       = ai;
            instanceCI.enabledApiLayerCount  = static_cast<uint32_t>(m_XrActiveAPILayers.size());
            instanceCI.enabledApiLayerNames  = m_XrActiveAPILayers.data();
            instanceCI.enabledExtensionCount = static_cast<uint32_t>(m_XrActiveInstanceExtensions.size());
            instanceCI.enabledExtensionNames = m_XrActiveInstanceExtensions.data();
            throwOnXrFailure(xrCreateInstance(&instanceCI, &m_XrInstance), "Failed to create OpenXR instance");
        }

        void XRDevice::destroyXrInstance()
        {
            if (m_XrInstance == XR_NULL_HANDLE)
            {
                return;
            }

            const auto result = xrDestroyInstance(m_XrInstance);
            if (XR_FAILED(result))
            {
                VULTRA_CORE_WARN("[OpenXR] Failed to destroy instance (code: {})", static_cast<int>(result));
            }
            m_XrInstance = XR_NULL_HANDLE;
            m_XrSystemId = 0;
        }

        void XRDevice::createXrDebugUtilsMessenger()
        {
            // Check that "XR_EXT_debug_utils" is in the active Instance Extensions before creating an
            // XrDebugUtilsMessengerEXT.
            if (IsStringInVector(m_XrActiveInstanceExtensions, XR_EXT_DEBUG_UTILS_EXTENSION_NAME))
            {
                m_XrDebugUtilsMessenger = CreateOpenXRDebugUtilsMessenger(m_XrInstance);
            }
        }

        void XRDevice::destroyXrDebugUtilsMessenger()
        {
            // Check that "XR_EXT_debug_utils" is in the active Instance Extensions before destroying the
            // XrDebugUtilsMessengerEXT.
            if (m_XrDebugUtilsMessenger != XR_NULL_HANDLE)
            {
                DestroyOpenXRDebugUtilsMessenger(m_XrInstance, m_XrDebugUtilsMessenger);
                m_XrDebugUtilsMessenger = XR_NULL_HANDLE;
            }
        }

        void XRDevice::getInstanceProperties()
        {
            OPENXR_CHECK(xrGetInstanceProperties(m_XrInstance, &m_XrInstanceProperties),
                         "Failed to get InstanceProperties.");
        }

        void XRDevice::getSystemID()
        {
            // Get the XrSystemId from the instance and the supplied XrFormFactor.
            XrSystemGetInfo systemGI {};
            systemGI.type       = XR_TYPE_SYSTEM_GET_INFO;
            systemGI.formFactor = m_XrFormFactor;
            OPENXR_CHECK(xrGetSystem(m_XrInstance, &systemGI, &m_XrSystemId), "Failed to get SystemID.");

            // Get the System's properties for some general information about the hardware and the vendor.
            OPENXR_CHECK(xrGetSystemProperties(m_XrInstance, m_XrSystemId, &m_XrSystemProperties),
                         "Failed to get SystemProperties.");

            // Check for eye gaze interaction support
            XrSystemEyeGazeInteractionPropertiesEXT eyeGazeProps {};
            eyeGazeProps.type = XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT;

            // Call xrGetSystemProperties again to fill in the eye gaze properties
            m_XrSystemProperties.next = &eyeGazeProps;
            XrResult result           = xrGetSystemProperties(m_XrInstance, m_XrSystemId, &m_XrSystemProperties);
            if (result == XR_SUCCESS)
            {
                if (eyeGazeProps.supportsEyeGazeInteraction)
                {
                    VULTRA_CORE_INFO("Eye gaze interaction is supported.");
                    m_Properties.supportEyeTracking = true;
                }
                else
                {
                    VULTRA_CORE_INFO("Eye gaze interaction is not supported.");
                }
            }
            else
            {
                VULTRA_CORE_ERROR("Failed to get eye gaze interaction properties.");
            }

            auto runtimeInfo = fmt::format("[OpenXR] OpenXR Runtime: {} - {}.{}.{}",
                                           fmt::string_view(m_XrInstanceProperties.runtimeName),
                                           XR_VERSION_MAJOR(m_XrInstanceProperties.runtimeVersion),
                                           XR_VERSION_MINOR(m_XrInstanceProperties.runtimeVersion),
                                           XR_VERSION_PATCH(m_XrInstanceProperties.runtimeVersion));

            VULTRA_CORE_INFO(runtimeInfo);
        }

        void XRDevice::getEnvironmentBlendModes()
        {
            // Retrieves the available blend modes. The first call gets the count of the array that will be returned.
            // The next call fills out the array.
            uint32_t environmentBlendModeCount = 0;
            OPENXR_CHECK(xrEnumerateEnvironmentBlendModes(
                             m_XrInstance, m_XrSystemId, m_ViewType, 0, &environmentBlendModeCount, nullptr),
                         "Failed to enumerate EnvironmentBlend Modes.");
            m_EnvironmentBlendModes.resize(environmentBlendModeCount);
            OPENXR_CHECK(xrEnumerateEnvironmentBlendModes(m_XrInstance,
                                                          m_XrSystemId,
                                                          m_ViewType,
                                                          environmentBlendModeCount,
                                                          &environmentBlendModeCount,
                                                          m_EnvironmentBlendModes.data()),
                         "Failed to enumerate EnvironmentBlend Modes.");

            // Pick the first application supported blend mode supported by the hardware.
            for (const XrEnvironmentBlendMode& environmentBlendMode : m_ApplicationEnvironmentBlendModes)
            {
                if (std::find(m_EnvironmentBlendModes.begin(), m_EnvironmentBlendModes.end(), environmentBlendMode) !=
                    m_EnvironmentBlendModes.end())
                {
                    m_EnvironmentBlendMode = environmentBlendMode;
                    break;
                }
            }
            if (m_EnvironmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM)
            {
                VULTRA_CORE_ERROR("[Context][OpenXR] Failed to find a compatible blend mode. Defaulting to "
                                  "XR_ENVIRONMENT_BLEND_MODE_OPAQUE.");
                m_EnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            }
        }

        void XRDevice::loadXrFunctions()
        {
            throwOnXrFailure(xrGetInstanceProcAddr(m_XrInstance,
                                                   "xrCreateVulkanInstanceKHR",
                                                   reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateVulkanInstanceKHR)),
                             "Failed to load xrCreateVulkanInstanceKHR");
            throwOnXrFailure(xrGetInstanceProcAddr(m_XrInstance,
                                                   "xrCreateVulkanDeviceKHR",
                                                   reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateVulkanDeviceKHR)),
                             "Failed to load xrCreateVulkanDeviceKHR");
            throwOnXrFailure(
                xrGetInstanceProcAddr(m_XrInstance,
                                      "xrGetVulkanGraphicsRequirements2KHR",
                                      reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanGraphicsRequirements2KHR)),
                "Failed to load xrGetVulkanGraphicsRequirements2KHR");
            throwOnXrFailure(
                xrGetInstanceProcAddr(m_XrInstance,
                                      "xrGetVulkanGraphicsDevice2KHR",
                                      reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanGraphicsDevice2KHR)),
                "Failed to load xrGetVulkanGraphicsDevice2KHR");

            if (!xrCreateVulkanInstanceKHR || !xrCreateVulkanDeviceKHR || !xrGetVulkanGraphicsRequirements2KHR ||
                !xrGetVulkanGraphicsDevice2KHR)
            {
                throw std::runtime_error("[OpenXR] Vulkan interop function pointer is null");
            }

            XrGraphicsRequirementsVulkanKHR graphicsRequirements {};
            graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
            throwOnXrFailure(xrGetVulkanGraphicsRequirements2KHR(m_XrInstance, m_XrSystemId, &graphicsRequirements),
                             "Failed to get Vulkan graphics requirements");
        }
    } // namespace openxr
} // namespace vultra