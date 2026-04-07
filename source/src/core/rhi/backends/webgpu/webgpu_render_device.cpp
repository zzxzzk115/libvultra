#include "vultra/core/rhi/backends/webgpu/webgpu_render_device.hpp"
#include "vultra/core/base/common_context.hpp"

#include <format>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace vultra
{
    namespace rhi
    {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        namespace
        {
            [[nodiscard]] std::string toStdString(const WGPUStringView str)
            {
                if (!str.data)
                {
                    return {};
                }
                if (str.length == WGPU_STRLEN)
                {
                    return std::string {str.data};
                }
                return std::string {str.data, str.length};
            }

            [[nodiscard]] const char* toRequestAdapterStatusString(const WGPURequestAdapterStatus status)
            {
                switch (status)
                {
                    case WGPURequestAdapterStatus_Success:
                        return "Success";
                    case WGPURequestAdapterStatus_Unavailable:
                        return "Unavailable";
                    case WGPURequestAdapterStatus_Error:
                        return "Error";
                    default:
                        return "Invalid";
                }
            }

            [[nodiscard]] const char* toRequestDeviceStatusString(const WGPURequestDeviceStatus status)
            {
                switch (status)
                {
                    case WGPURequestDeviceStatus_Success:
                        return "Success";
                    case WGPURequestDeviceStatus_Error:
                        return "Error";
                    default:
                        return "Invalid";
                }
            }

            struct AdapterRequestResult
            {
                bool                     completed {false};
                WGPURequestAdapterStatus status {WGPURequestAdapterStatus_Error};
                WGPUAdapter              adapter {nullptr};
                std::string              message;
            };

            struct DeviceRequestResult
            {
                bool                    completed {false};
                WGPURequestDeviceStatus status {WGPURequestDeviceStatus_Error};
                WGPUDevice              device {nullptr};
                std::string             message;
            };

            void onRequestAdapter(const WGPURequestAdapterStatus status,
                                  WGPUAdapter                    adapter,
                                  const WGPUStringView           message,
                                  void*                          userdata1,
                                  void*)
            {
                auto* result      = static_cast<AdapterRequestResult*>(userdata1);
                result->completed = true;
                result->status    = status;
                result->adapter   = adapter;
                result->message   = toStdString(message);
            }

            void onRequestDevice(const WGPURequestDeviceStatus status,
                                 WGPUDevice                    device,
                                 const WGPUStringView          message,
                                 void*                         userdata1,
                                 void*)
            {
                auto* result      = static_cast<DeviceRequestResult*>(userdata1);
                result->completed = true;
                result->status    = status;
                result->device    = device;
                result->message   = toStdString(message);
            }

            void onDeviceLost(const WGPUDevice*,
                              const WGPUDeviceLostReason reason,
                              const WGPUStringView       message,
                              void*,
                              void*)
            {
                VULTRA_CORE_ERROR("[RenderDevice] WebGPU device lost (reason={}): {}",
                                  static_cast<int>(reason),
                                  toStdString(message));
            }

            void
            onUncapturedError(const WGPUDevice*, const WGPUErrorType type, const WGPUStringView message, void*, void*)
            {
                VULTRA_CORE_ERROR("[RenderDevice] WebGPU uncaptured error (type={}): {}",
                                  static_cast<int>(type),
                                  toStdString(message));
            }

            [[nodiscard]] RenderDeviceLimits toRenderDeviceLimits(const WGPULimits& limits)
            {
                RenderDeviceLimits out {};
                out.maxBindGroups                    = limits.maxBindGroups;
                out.maxUniformBuffersPerShaderStage  = limits.maxUniformBuffersPerShaderStage;
                out.maxStorageBuffersPerShaderStage  = limits.maxStorageBuffersPerShaderStage;
                out.maxSampledTexturesPerShaderStage = limits.maxSampledTexturesPerShaderStage;
                out.maxSamplersPerShaderStage        = limits.maxSamplersPerShaderStage;
                out.maxStorageTexturesPerShaderStage = limits.maxStorageTexturesPerShaderStage;
                out.maxUniformBufferBindingSize      = limits.maxUniformBufferBindingSize;
                out.maxStorageBufferBindingSize      = limits.maxStorageBufferBindingSize;
                out.maxBufferSize                    = limits.maxBufferSize;
                out.maxVertexBuffers                 = limits.maxVertexBuffers;
                out.maxVertexAttributes              = limits.maxVertexAttributes;
                out.maxInterStageShaderVariables      = limits.maxInterStageShaderVariables;
                out.maxColorAttachments              = limits.maxColorAttachments;
                out.maxComputeWorkgroupStorageSize    = limits.maxComputeWorkgroupStorageSize;
                out.maxComputeInvocationsPerWorkgroup = limits.maxComputeInvocationsPerWorkgroup;
                out.maxComputeWorkgroupSizeX          = limits.maxComputeWorkgroupSizeX;
                out.maxComputeWorkgroupSizeY          = limits.maxComputeWorkgroupSizeY;
                out.maxComputeWorkgroupSizeZ          = limits.maxComputeWorkgroupSizeZ;
                out.maxComputeWorkgroupsPerDimension  = limits.maxComputeWorkgroupsPerDimension;
                return out;
            }

            template<typename Predicate>
            void waitForFuture(WGPUInstance instance, const WGPUFuture future, Predicate&& done)
            {
                WGPUFutureWaitInfo waitInfo {};
                waitInfo.future = future;

                while (!done())
                {
                    waitInfo.completed    = false;
                    const auto waitStatus = wgpuInstanceWaitAny(instance, 1, &waitInfo, 0);
                    if (waitStatus != WGPUWaitStatus_Success && waitStatus != WGPUWaitStatus_TimedOut)
                    {
                        throw std::runtime_error(
                            std::format("wgpuInstanceWaitAny failed with status {}", static_cast<int>(waitStatus)));
                    }

                    if (waitStatus == WGPUWaitStatus_TimedOut)
                    {
                        wgpuInstanceProcessEvents(instance);
#if defined(__EMSCRIPTEN__)
                        // On web, callbacks are serviced by the browser event loop.
                        // Yield cooperatively so requestAdapter/requestDevice can complete.
                        emscripten_sleep(0);
#else
                        std::this_thread::yield();
#endif
                    }
                }
            }
        } // namespace
#endif

        WebGPURenderDevice::WebGPURenderDevice(const std::string_view appName)
        {
            m_FeatureFlag = RenderDeviceFeatureFlagBits::eNormal;
            m_AppName     = appName;

#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            throw std::runtime_error("WebGPU backend is disabled for this build");
#else
            WGPUInstanceDescriptor instanceDesc {};
            instanceDesc.nextInChain = nullptr;
            m_Instance               = wgpuCreateInstance(&instanceDesc);
            if (!m_Instance)
            {
                throw std::runtime_error("Failed to create WebGPU instance");
            }

            WGPURequestAdapterOptions adapterOptions {};
            adapterOptions.featureLevel         = WGPUFeatureLevel_Core;
            adapterOptions.powerPreference      = WGPUPowerPreference_HighPerformance;
            adapterOptions.forceFallbackAdapter = false;
            adapterOptions.backendType          = WGPUBackendType_Undefined;
            adapterOptions.compatibleSurface    = nullptr;

            AdapterRequestResult           adapterResult {};
            WGPURequestAdapterCallbackInfo adapterCallbackInfo {};
            adapterCallbackInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
            adapterCallbackInfo.callback  = onRequestAdapter;
            adapterCallbackInfo.userdata1 = &adapterResult;
            adapterCallbackInfo.userdata2 = nullptr;

            const auto adapterFuture = wgpuInstanceRequestAdapter(m_Instance, &adapterOptions, adapterCallbackInfo);
            waitForFuture(m_Instance, adapterFuture, [&adapterResult]() { return adapterResult.completed; });
            if (adapterResult.status != WGPURequestAdapterStatus_Success || !adapterResult.adapter)
            {
                throw std::runtime_error(std::format("Failed to request WebGPU adapter ({}) {}",
                                                     toRequestAdapterStatusString(adapterResult.status),
                                                     adapterResult.message));
            }
            m_Adapter = adapterResult.adapter;

            std::vector<WGPUFeatureName> requiredFeatures;
            const bool supportsTimestampQuery = wgpuAdapterHasFeature(m_Adapter, WGPUFeatureName_TimestampQuery);

            m_SupportsTextureCompressionBC = wgpuAdapterHasFeature(m_Adapter, WGPUFeatureName_TextureCompressionBC);
            if (m_SupportsTextureCompressionBC)
            {
                requiredFeatures.push_back(WGPUFeatureName_TextureCompressionBC);
            }

            // We can request standard TimestampQuery via webgpu.h, but Tracky currently writes
            // timestamps through command-encoder APIs that are native-extension behavior.
            // Keep the feature request for device compatibility, but disable Tracky timestamp
            // path on this webgpu.h-only route to avoid invalidating the encoder.
            m_SupportsTimestampQuery = false;
            if (supportsTimestampQuery)
            {
                requiredFeatures.push_back(WGPUFeatureName_TimestampQuery);
            }
            WGPUDeviceDescriptor deviceDesc {};
            deviceDesc.label.data                       = m_AppName.c_str();
            deviceDesc.label.length                     = WGPU_STRLEN;
            deviceDesc.requiredFeatureCount             = static_cast<size_t>(requiredFeatures.size());
            deviceDesc.requiredFeatures                 = requiredFeatures.empty() ? nullptr : requiredFeatures.data();
            deviceDesc.requiredLimits                   = nullptr;
            deviceDesc.defaultQueue.label.data          = m_AppName.c_str();
            deviceDesc.defaultQueue.label.length        = WGPU_STRLEN;
            deviceDesc.deviceLostCallbackInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
            deviceDesc.deviceLostCallbackInfo.callback  = onDeviceLost;
            deviceDesc.deviceLostCallbackInfo.userdata1 = nullptr;
            deviceDesc.deviceLostCallbackInfo.userdata2 = nullptr;
            deviceDesc.uncapturedErrorCallbackInfo.callback  = onUncapturedError;
            deviceDesc.uncapturedErrorCallbackInfo.userdata1 = nullptr;
            deviceDesc.uncapturedErrorCallbackInfo.userdata2 = nullptr;

            DeviceRequestResult           deviceResult {};
            WGPURequestDeviceCallbackInfo deviceCallbackInfo {};
            deviceCallbackInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
            deviceCallbackInfo.callback  = onRequestDevice;
            deviceCallbackInfo.userdata1 = &deviceResult;
            deviceCallbackInfo.userdata2 = nullptr;

            const auto deviceFuture = wgpuAdapterRequestDevice(m_Adapter, &deviceDesc, deviceCallbackInfo);
            waitForFuture(m_Instance, deviceFuture, [&deviceResult]() { return deviceResult.completed; });
            if (deviceResult.status != WGPURequestDeviceStatus_Success || !deviceResult.device)
            {
                throw std::runtime_error(std::format("Failed to request WebGPU device ({}) {}",
                                                     toRequestDeviceStatusString(deviceResult.status),
                                                     deviceResult.message));
            }
            m_Device = deviceResult.device;
            m_Queue  = wgpuDeviceGetQueue(m_Device);
            if (!m_Queue)
            {
                throw std::runtime_error("Failed to get WebGPU queue");
            }

            WGPULimits adapterLimits {};
            if (wgpuAdapterGetLimits(m_Adapter, &adapterLimits) == WGPUStatus_Success)
            {
                m_Limits = toRenderDeviceLimits(adapterLimits);
            }

            WGPULimits deviceLimits {};
            if (wgpuDeviceGetLimits(m_Device, &deviceLimits) == WGPUStatus_Success)
            {
                m_Limits = toRenderDeviceLimits(deviceLimits);
            }

            WGPUAdapterInfo adapterInfo {};
            if (wgpuAdapterGetInfo(m_Adapter, &adapterInfo) == WGPUStatus_Success)
            {
                auto name = toStdString(adapterInfo.description);
                if (name.empty())
                {
                    name = toStdString(adapterInfo.device);
                }
                m_FeatureReport.deviceName = std::move(name);
                wgpuAdapterInfoFreeMembers(adapterInfo);
            }
            if (m_FeatureReport.deviceName.empty())
            {
                m_FeatureReport.deviceName = "WebGPU Adapter";
            }

            m_FeatureReport.apiMajor = 0;
            m_FeatureReport.apiMinor = 0;
            m_FeatureReport.apiPatch = 0;
#endif
        }

        uint64_t WebGPURenderDevice::getFormatFeatureFlagsOptimal(const PixelFormat pixelFormat) const
        {
            constexpr uint64_t kSampledImage   = 0x00000001ull;
            constexpr uint64_t kStorageImage   = 0x00000002ull;
            constexpr uint64_t kColorAttachment = 0x00000080ull;
            constexpr uint64_t kSampledLinear  = 0x00001000ull;
            constexpr uint64_t kTransferSrc    = 0x00004000ull;
            constexpr uint64_t kTransferDst    = 0x00008000ull;

            switch (pixelFormat)
            {
                case PixelFormat::eBC1_UNorm:
                case PixelFormat::eBC2_UNorm:
                case PixelFormat::eBC3_UNorm:
                case PixelFormat::eBC4_UNorm:
                case PixelFormat::eBC5_UNorm:
                case PixelFormat::eBC6H_RGB16F:
                case PixelFormat::eBC7_RGBA8_UNorm:
                    return m_SupportsTextureCompressionBC ? (kTransferDst | kSampledImage | kSampledLinear) : 0u;
                case PixelFormat::eRGBA8_UNorm:
                case PixelFormat::eRGBA8_sRGB:
                case PixelFormat::eBGRA8_UNorm:
                case PixelFormat::eBGRA8_sRGB:
                    return kTransferSrc | kTransferDst | kSampledImage | kSampledLinear | kStorageImage |
                           kColorAttachment;
                case PixelFormat::eRGBA16F:
                case PixelFormat::eRGBA32F:
                    return kTransferSrc | kTransferDst | kSampledImage | kStorageImage | kColorAttachment;
                default:
                    return kTransferSrc | kTransferDst | kSampledImage;
            }
        }

        WebGPURenderDevice::~WebGPURenderDevice()
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            // Keep teardown conservative on native WebGPU backends.
            // We observed shutdown-time panics inside wgpu-native when cached child objects
            // (pipeline layouts / bind group layouts / samplers) are explicitly released after
            // higher-level pipeline wrappers have started tearing down in an order we do not fully control.
            // Let the device own and reap these cached objects during device destruction instead.
            m_PipelineLayouts.clear();
            m_DescriptorSetLayouts.clear();
            m_DescriptorSetLayoutBindings.clear();
            m_EmptyDescriptorSetLayout = nullptr;
            m_Samplers.clear();

            if (m_Queue)
            {
                wgpuQueueRelease(m_Queue);
                m_Queue = nullptr;
            }
            if (m_Device)
            {
                wgpuDeviceRelease(m_Device);
                m_Device = nullptr;
            }
            if (m_Adapter)
            {
                wgpuAdapterRelease(m_Adapter);
                m_Adapter = nullptr;
            }
            if (m_Instance)
            {
                wgpuInstanceRelease(m_Instance);
                m_Instance = nullptr;
            }
#endif
        }
    } // namespace rhi
} // namespace vultra
