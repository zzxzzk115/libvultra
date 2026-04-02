#pragma once

#include <cstdint>
#include <format>
#include <string>

#include "vultra/core/rhi/structs/render_backend_api.hpp"

namespace vultra
{
    namespace rhi
    {
        enum class RenderDeviceFeatureFlagBits : uint32_t
        {
            eNormal             = 0,
            eRayQuery           = BIT(0),
            eRayTracingPipeline = BIT(1),
            eMeshShader         = BIT(2),
            eXR                 = BIT(3),

            eRayTracing = eRayQuery | eRayTracingPipeline,
            eAll        = eNormal | eRayQuery | eRayTracingPipeline | eMeshShader | eXR,
        };

        enum class RenderDeviceFeatureReportFlagBits : uint64_t
        {
            eNone                    = 0,
            eXR                      = BIT(0),
            eRayTracingPipeline      = BIT(1),
            eRayQuery                = BIT(2),
            eAccelerationStructure   = BIT(3),
            eMeshShader              = BIT(4),
            eBufferDeviceAddress     = BIT(5),
            eDescriptorIndexing      = BIT(6),
            eDrawIndirectCount       = BIT(7),
            eMultiDraw               = BIT(8),
            eDrawParameters          = BIT(9),
            eFragmentShaderInterlock = BIT(10),
            eMultiview               = BIT(11),
            eDynamicRendering        = BIT(12),
            eSynchronization2        = BIT(13),
        };

        struct RenderDeviceFeatureReport
        {
            RenderDeviceFeatureReportFlagBits flags {RenderDeviceFeatureReportFlagBits::eNone};

            std::string deviceName;
            uint32_t    apiMajor {0};
            uint32_t    apiMinor {0};
            uint32_t    apiPatch {0};
        };

        struct PhysicalDeviceInfo
        {
            uint32_t    vendorId;
            uint32_t    deviceId;
            std::string deviceName;

            std::string toString()
            {
                return std::format("[Vendor ID: {}, Device ID: {}, Device Name: {}]", vendorId, deviceId, deviceName);
            }
        };
    } // namespace rhi
} // namespace vultra
