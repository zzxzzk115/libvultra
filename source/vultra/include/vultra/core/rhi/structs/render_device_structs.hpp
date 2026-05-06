#pragma once

#include "vultra/core/base/base.hpp"

#include <atomic>
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

        enum class RenderMemoryKind : uint8_t
        {
            eCpuCache = 0,
            eGpuDeviceLocal,
            eGpuHostVisible,
        };

        struct RenderDeviceMemoryStats
        {
            uint64_t cpuCacheBytes {0};
            uint64_t gpuDeviceLocalBytes {0};
            uint64_t gpuHostVisibleBytes {0};
        };

        class RenderDeviceMemoryTracker
        {
        public:
            void add(RenderMemoryKind kind, uint64_t bytes)
            {
                switch (kind)
                {
                    case RenderMemoryKind::eCpuCache:
                        m_CpuCacheBytes.fetch_add(bytes, std::memory_order_relaxed);
                        break;
                    case RenderMemoryKind::eGpuDeviceLocal:
                        m_GpuDeviceLocalBytes.fetch_add(bytes, std::memory_order_relaxed);
                        break;
                    case RenderMemoryKind::eGpuHostVisible:
                        m_GpuHostVisibleBytes.fetch_add(bytes, std::memory_order_relaxed);
                        break;
                }
            }

            void remove(RenderMemoryKind kind, uint64_t bytes)
            {
                switch (kind)
                {
                    case RenderMemoryKind::eCpuCache:
                        m_CpuCacheBytes.fetch_sub(bytes, std::memory_order_relaxed);
                        break;
                    case RenderMemoryKind::eGpuDeviceLocal:
                        m_GpuDeviceLocalBytes.fetch_sub(bytes, std::memory_order_relaxed);
                        break;
                    case RenderMemoryKind::eGpuHostVisible:
                        m_GpuHostVisibleBytes.fetch_sub(bytes, std::memory_order_relaxed);
                        break;
                }
            }

            [[nodiscard]] RenderDeviceMemoryStats snapshot() const
            {
                return RenderDeviceMemoryStats {
                    .cpuCacheBytes       = m_CpuCacheBytes.load(std::memory_order_relaxed),
                    .gpuDeviceLocalBytes = m_GpuDeviceLocalBytes.load(std::memory_order_relaxed),
                    .gpuHostVisibleBytes = m_GpuHostVisibleBytes.load(std::memory_order_relaxed),
                };
            }

        private:
            std::atomic<uint64_t> m_CpuCacheBytes {0};
            std::atomic<uint64_t> m_GpuDeviceLocalBytes {0};
            std::atomic<uint64_t> m_GpuHostVisibleBytes {0};
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
