#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/scoped_enum_flags.hpp"

#include <vulkan/vulkan.hpp>

// OpenXR Headers
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
    }

    namespace openxr
    {
        enum class XRDeviceFeatureFlagBits : uint32_t
        {
            eVR = BIT(0),
            eAR = BIT(1),
            eXR = eVR | eAR,
        };

        struct XRDeviceProperties
        {
            bool enableMultiview    = true;
            bool supportEyeTracking = false;
        };

        class XRDevice
        {
            friend class rhi::RenderDevice;
            friend class XRHeadset;

        public:
            explicit XRDevice(XRDeviceFeatureFlagBits, std::string_view appName);
            XRDevice(const XRDevice&)     = delete;
            XRDevice(XRDevice&&) noexcept = delete;
            ~XRDevice();

            XRDevice& operator=(const XRDevice&)     = delete;
            XRDevice& operator=(XRDevice&&) noexcept = delete;

            [[nodiscard]] std::string        getApplicationName() const { return m_AppName; }
            [[nodiscard]] XRDeviceProperties getProperties() const { return m_Properties; }

            [[nodiscard]] XrInstance              getXrInstance() const { return m_XrInstance; }
            [[nodiscard]] XrSystemId              getXrSystemId() const { return m_XrSystemId; }
            [[nodiscard]] XrViewConfigurationType getXrViewType() const { return m_ViewType; }
            [[nodiscard]] XrInstanceProperties    getXrInstanceProperties() const { return m_XrInstanceProperties; }

        private:
            void createXrInstance();
            void destroyXrInstance() const;

            void createXrDebugUtilsMessenger();
            void destroyXrDebugUtilsMessenger() const;

            void getInstanceProperties();
            void getSystemID();
            void getEnvironmentBlendModes();

            void loadXrFunctions();

        private:
            XRDeviceFeatureFlagBits m_FeatureFlagBits {XRDeviceFeatureFlagBits::eVR};
            std::string             m_AppName;

            XrInstance               m_XrInstance {nullptr};
            XrSystemId               m_XrSystemId {0u};
            std::vector<const char*> m_XrActiveAPILayers;
            std::vector<const char*> m_XrActiveInstanceExtensions;
            std::vector<std::string> m_XrApiLayers;
            std::vector<std::string> m_XrInstanceExtensions;

            XrDebugUtilsMessengerEXT m_XrDebugUtilsMessenger {};

            XrInstanceProperties m_XrInstanceProperties {.type = XR_TYPE_INSTANCE_PROPERTIES};

            XrFormFactor       m_XrFormFactor {XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
            XrSystemProperties m_XrSystemProperties {.type = XR_TYPE_SYSTEM_PROPERTIES};

            XrViewConfigurationType m_ViewType {XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO}; // Stereo HMD

            std::vector<XrEnvironmentBlendMode> m_ApplicationEnvironmentBlendModes {
                XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
            }; // VR-Only
            std::vector<XrEnvironmentBlendMode> m_EnvironmentBlendModes;
            XrEnvironmentBlendMode              m_EnvironmentBlendMode {XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM};

            // NOLINTBEGIN
            PFN_xrCreateVulkanInstanceKHR           xrCreateVulkanInstanceKHR {nullptr};
            PFN_xrCreateVulkanDeviceKHR             xrCreateVulkanDeviceKHR {nullptr};
            PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR {nullptr};
            PFN_xrGetVulkanGraphicsDevice2KHR       xrGetVulkanGraphicsDevice2KHR {nullptr};
            // NOLINTEND

            XRDeviceProperties m_Properties {};
        };
    } // namespace openxr
} // namespace vultra

template<>
struct HasFlags<vultra::openxr::XRDeviceFeatureFlagBits> : std::true_type
{};