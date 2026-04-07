#pragma once

#include "vultra/core/base/base.hpp"

#include <cstdint>
#include <format>
#include <string>

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

        struct RenderDeviceLimits
        {
            uint32_t maxBindGroups {0};
            uint32_t maxUniformBuffersPerShaderStage {0};
            uint32_t maxStorageBuffersPerShaderStage {0};
            uint32_t maxSampledTexturesPerShaderStage {0};
            uint32_t maxSamplersPerShaderStage {0};
            uint32_t maxStorageTexturesPerShaderStage {0};
            uint64_t maxUniformBufferBindingSize {0};
            uint64_t maxStorageBufferBindingSize {0};
            uint64_t maxBufferSize {0};
            uint32_t maxVertexBuffers {0};
            uint32_t maxVertexAttributes {0};
            uint32_t maxInterStageShaderVariables {0};
            uint32_t maxColorAttachments {0};
            uint32_t maxComputeWorkgroupStorageSize {0};
            uint32_t maxComputeInvocationsPerWorkgroup {0};
            uint32_t maxComputeWorkgroupSizeX {0};
            uint32_t maxComputeWorkgroupSizeY {0};
            uint32_t maxComputeWorkgroupSizeZ {0};
            uint32_t maxComputeWorkgroupsPerDimension {0};
        };

        enum class SyncPrimitiveSupport : uint8_t
        {
            eUnsupported,
            eEmulated,
            eNative,
        };

        struct RenderDeviceSyncCapabilities
        {
            SyncPrimitiveSupport fence {SyncPrimitiveSupport::eUnsupported};
            SyncPrimitiveSupport semaphore {SyncPrimitiveSupport::eUnsupported};
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
