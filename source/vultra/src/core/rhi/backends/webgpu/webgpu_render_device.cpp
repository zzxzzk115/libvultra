#include "vultra/core/rhi/backends/webgpu/webgpu_render_device.hpp"
#include "vultra/core/base/common_context.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>
#include <cstring>
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
                              void* userdata1,
                              void*)
            {
                auto* const device = static_cast<WebGPURenderDevice*>(userdata1);
                if (device != nullptr)
                {
                    device->disableGpuTiming();
                }
                VULTRA_CORE_ERROR("[RenderDevice] WebGPU device lost (reason={}): {}",
                                  static_cast<int>(reason),
                                  toStdString(message));
            }

            void
            onUncapturedError(const WGPUDevice*,
                              const WGPUErrorType type,
                              const WGPUStringView message,
                              void* userdata1,
                              void*)
            {
                auto* const device = static_cast<WebGPURenderDevice*>(userdata1);
                if (device != nullptr)
                {
                    device->disableGpuTiming();
                }
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

            struct MapQueryUserData
            {
                WebGPURenderDevice* device {nullptr};
                uint32_t            slot {0};
            };

            struct ScopeMapQueryUserData
            {
                WebGPURenderDevice* device {nullptr};
                uint32_t            slot {0};
            };

            void onFrameTimeReadbackMapped(const WGPUMapAsyncStatus status,
                                           const WGPUStringView,
                                           void* userdata1,
                                           void*)
            {
                auto* const userData = static_cast<MapQueryUserData*>(userdata1);
                if (!userData || !userData->device)
                {
                    return;
                }

                auto* const device = userData->device;
                if (userData->slot < device->m_FrameTimeSlots.size())
                {
                    auto& slot      = device->m_FrameTimeSlots[userData->slot];
                    slot.mapPending = false;
                    slot.mapReady   = (status == WGPUMapAsyncStatus_Success);
                    if (!slot.mapReady)
                    {
                        VULTRA_CORE_WARN("[RenderDevice] WebGPU timestamp readback map failed (status={})",
                                         static_cast<int>(status));
                    }
                }
                delete userData;
            }

            void onScopeTimeReadbackMapped(const WGPUMapAsyncStatus status,
                                           const WGPUStringView,
                                           void* userdata1,
                                           void*)
            {
                auto* const userData = static_cast<ScopeMapQueryUserData*>(userdata1);
                if (!userData || !userData->device)
                {
                    return;
                }

                auto* const device = userData->device;
                if (userData->slot < device->m_ScopeTimeSlots.size())
                {
                    auto& slot      = device->m_ScopeTimeSlots[userData->slot];
                    slot.mapPending = false;
                    slot.mapReady   = (status == WGPUMapAsyncStatus_Success);
                    if (!slot.mapReady)
                    {
                        slot.pendingResolve = false;
                        slot.resolveSubmitted = false;
                        slot.resolved       = true;
                        slot.ms             = -1.0;
                    }
                }
                delete userData;
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

            // Builtin WebGPU GPU timing is disabled on this backend/runtime path. Even when the
            // adapter reports TimestampQuery support, creating query resources has proven unstable
            // and can invalidate the device before swapchain setup completes.
            m_SupportsTimestampQuery = false;
            m_SupportsScopeTimestampQuery = false;
            m_SupportsTimestampQueryInsideEncoders = false;
            if (supportsTimestampQuery)
            {
                VULTRA_CORE_WARN(
                    "[RenderDevice] WebGPU timestamp query support detected, but real GPU pass timing is disabled on this runtime because query resource creation invalidates the device.");
            }
            else
            {
                VULTRA_CORE_WARN("[RenderDevice] WebGPU timestamp query feature unavailable; GPU frame time will be N/A.");
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
            deviceDesc.deviceLostCallbackInfo.userdata1 = this;
            deviceDesc.deviceLostCallbackInfo.userdata2 = nullptr;
            deviceDesc.uncapturedErrorCallbackInfo.callback  = onUncapturedError;
            deviceDesc.uncapturedErrorCallbackInfo.userdata1 = this;
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

            if (m_SupportsTimestampQuery)
            {
                constexpr uint32_t kFrameSlotCount = 8u;
                m_FrameTimeSlots.resize(kFrameSlotCount);
                for (auto& slot : m_FrameTimeSlots)
                {
                    WGPUQuerySetDescriptor queryDesc {};
                    queryDesc.type  = WGPUQueryType_Timestamp;
                    queryDesc.count = 1;
                    slot.querySet   = wgpuDeviceCreateQuerySet(m_Device, &queryDesc);

                    WGPUBufferDescriptor resolveDesc {};
                    resolveDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
                    resolveDesc.size  = sizeof(uint64_t);
                    slot.resolveBuffer = wgpuDeviceCreateBuffer(m_Device, &resolveDesc);

                    WGPUBufferDescriptor readbackDesc {};
                    readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
                    readbackDesc.size  = sizeof(uint64_t);
                    slot.readbackBuffer = wgpuDeviceCreateBuffer(m_Device, &readbackDesc);

                    if (slot.querySet == nullptr || slot.resolveBuffer == nullptr || slot.readbackBuffer == nullptr)
                    {
                        VULTRA_CORE_WARN(
                            "[RenderDevice] WebGPU frame timestamp resources unavailable; disabling GPU frame timing.");
                        m_SupportsTimestampQuery = false;
                        m_FrameTimeSlots.clear();
                        m_PendingFrameTimeSlots.clear();
                        m_ActiveFrameTimeSlot = -1;
                        m_FrameTimePassTimestampPending = false;
                        break;
                    }
                }
            }

            if (m_SupportsScopeTimestampQuery)
            {
                constexpr uint32_t kScopeSlotCount = 256u;
                m_ScopeTimeSlots.resize(kScopeSlotCount);
                for (auto& slot : m_ScopeTimeSlots)
                {
                    if (!initializeScopeTimeSlotResources(slot))
                    {
                        VULTRA_CORE_WARN(
                            "[RenderDevice] WebGPU per-scope timestamp resources unavailable; disabling GPU scope timing.");
                        m_SupportsScopeTimestampQuery = false;
                        m_ScopeTimeSlots.clear();
                        m_ScopeTimeTokenToSlot.clear();
                        m_PendingScopePassTimestampTokens.clear();
                        m_ScopeTimeNextSlot = 0;
                        break;
                    }
                }
            }
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

        bool WebGPURenderDevice::initializeScopeTimeSlotResources(ScopeTimeQuerySlot& slot)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)slot;
            return false;
#else
            if (m_Device == nullptr)
            {
                return false;
            }

            if (slot.querySet == nullptr)
            {
                WGPUQuerySetDescriptor queryDesc {};
                queryDesc.type  = WGPUQueryType_Timestamp;
                queryDesc.count = 2;
                slot.querySet   = wgpuDeviceCreateQuerySet(m_Device, &queryDesc);
            }

            if (slot.resolveBuffer == nullptr)
            {
                WGPUBufferDescriptor resolveDesc {};
                resolveDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
                resolveDesc.size  = sizeof(uint64_t) * 2u;
                slot.resolveBuffer = wgpuDeviceCreateBuffer(m_Device, &resolveDesc);
            }

            if (slot.readbackBuffer == nullptr)
            {
                WGPUBufferDescriptor readbackDesc {};
                readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
                readbackDesc.size  = sizeof(uint64_t) * 2u;
                slot.readbackBuffer = wgpuDeviceCreateBuffer(m_Device, &readbackDesc);
            }

            return slot.querySet != nullptr && slot.resolveBuffer != nullptr && slot.readbackBuffer != nullptr;
#endif
        }

        bool WebGPURenderDevice::isScopeTimeSlotReusable(const ScopeTimeQuerySlot& slot) const
        {
            return !slot.active && !slot.pendingResolve && !slot.resolveSubmitted && !slot.mapPending;
        }

        void WebGPURenderDevice::disableGpuTiming()
        {
            m_SupportsTimestampQuery          = false;
            m_SupportsScopeTimestampQuery     = false;
            m_FrameTimePassTimestampPending   = false;
            m_ActiveFrameTimeSlot             = -1;
            m_PendingFrameTimeSlots.clear();
            m_PendingScopePassTimestampTokens.clear();
            m_ScopeTimeTokenToSlot.clear();
        }

        uint32_t WebGPURenderDevice::acquireScopeTimeSlot()
        {
            if (m_ScopeTimeSlots.empty())
            {
                return std::numeric_limits<uint32_t>::max();
            }

            const uint32_t slotCount = static_cast<uint32_t>(m_ScopeTimeSlots.size());
            for (uint32_t offset = 0; offset < slotCount; ++offset)
            {
                const uint32_t slotIndex = (m_ScopeTimeNextSlot + offset) % slotCount;
                auto&          slot      = m_ScopeTimeSlots[slotIndex];

                if (slot.pendingResolve && slot.token != 0)
                {
                    (void)consumeScopeGpuMs(slot.token);
                }

                if (!isScopeTimeSlotReusable(slot))
                {
                    continue;
                }

                m_ScopeTimeNextSlot = slotIndex + 1u;
                return slotIndex;
            }

            return std::numeric_limits<uint32_t>::max();
        }

        void WebGPURenderDevice::beginFrameGpuQuery(const std::uintptr_t commandBufferHandle)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)commandBufferHandle;
#else
            if (!m_SupportsTimestampQuery || m_Device == nullptr || commandBufferHandle == 0)
            {
                return;
            }

            (void)consumeGpuFrameMs();

            if (m_FrameTimeSlots.empty())
            {
                return;
            }

            const uint32_t slotIndex = m_FrameTimeNextSlot++ % static_cast<uint32_t>(m_FrameTimeSlots.size());
            auto&          slot      = m_FrameTimeSlots[slotIndex];
            if (slot.mapPending)
            {
                m_ActiveFrameTimeSlot = -1;
                m_FrameTimePassTimestampPending = false;
                return;
            }

            if (slot.mapReady)
            {
                wgpuBufferUnmap(slot.readbackBuffer);
                slot.mapReady = false;
            }

            m_ActiveFrameTimeSlot = static_cast<int32_t>(slotIndex);
                m_FrameTimePassTimestampPending = true;
#endif
        }

        void WebGPURenderDevice::endFrameGpuQuery(const std::uintptr_t commandBufferHandle)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)commandBufferHandle;
#else
            if (!m_SupportsTimestampQuery || m_ActiveFrameTimeSlot < 0 || commandBufferHandle == 0)
            {
                return;
            }

            const uint32_t slotIndex = static_cast<uint32_t>(m_ActiveFrameTimeSlot);
            if (slotIndex >= m_FrameTimeSlots.size())
            {
                m_ActiveFrameTimeSlot = -1;
                return;
            }

            auto& slot = m_FrameTimeSlots[slotIndex];
            if (slot.querySet == nullptr || slot.resolveBuffer == nullptr || slot.readbackBuffer == nullptr)
            {
                m_ActiveFrameTimeSlot = -1;
                m_FrameTimePassTimestampPending = false;
                return;
            }
            if (m_FrameTimePassTimestampPending)
            {
                static bool s_loggedNoPassTimestampWrite = false;
                if (!s_loggedNoPassTimestampWrite)
                {
                    s_loggedNoPassTimestampWrite = true;
                    VULTRA_CORE_WARN(
                        "[RenderDevice] WebGPU timestamp request was not consumed by any pass this frame; skipping GPU frame query.");
                }
                m_ActiveFrameTimeSlot = -1;
                m_FrameTimePassTimestampPending = false;
                return;
            }
            auto* encoder = reinterpret_cast<WGPUCommandEncoder>(commandBufferHandle);
            wgpuCommandEncoderResolveQuerySet(encoder, slot.querySet, 0, 1, slot.resolveBuffer, 0);
            wgpuCommandEncoderCopyBufferToBuffer(encoder,
                                                 slot.resolveBuffer,
                                                 0,
                                                 slot.readbackBuffer,
                                                 0,
                                                 sizeof(uint64_t));

            m_PendingFrameTimeSlots.push_back(slotIndex);

            m_ActiveFrameTimeSlot = -1;
            m_FrameTimePassTimestampPending = false;
#endif
        }

        bool WebGPURenderDevice::consumePassTimestampWriteRequest(WGPUQuerySet& querySet,
                                                                   uint32_t&     beginWriteIndex,
                                                                   uint32_t&     endWriteIndex)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)querySet;
            (void)beginWriteIndex;
            (void)endWriteIndex;
            return false;
#else
            if (!m_SupportsTimestampQuery)
            {
                return false;
            }

            // Scope fallback path: each pass consumes one pending scope token.
            while (!m_PendingScopePassTimestampTokens.empty())
            {
                const uint64_t scopeToken = m_PendingScopePassTimestampTokens.front();
                m_PendingScopePassTimestampTokens.pop_front();

                const auto slotIt = m_ScopeTimeTokenToSlot.find(scopeToken);
                if (slotIt == m_ScopeTimeTokenToSlot.end())
                {
                    continue;
                }

                auto& slot = m_ScopeTimeSlots[slotIt->second];
                if (!slot.active || slot.passTimestampIssued || slot.querySet == nullptr)
                {
                    continue;
                }

                querySet               = slot.querySet;
                beginWriteIndex        = 0u;
                endWriteIndex          = 1u;
                slot.passTimestampIssued = true;
                return true;
            }

            if (m_ActiveFrameTimeSlot < 0 || !m_FrameTimePassTimestampPending)
            {
                return false;
            }

            const uint32_t slotIndex = static_cast<uint32_t>(m_ActiveFrameTimeSlot);
            if (slotIndex >= m_FrameTimeSlots.size())
            {
                return false;
            }

            auto& slot = m_FrameTimeSlots[slotIndex];
            if (slot.querySet == nullptr)
            {
                return false;
            }

            querySet         = slot.querySet;
            beginWriteIndex  = 0u;
            endWriteIndex    = WGPU_QUERY_SET_INDEX_UNDEFINED;
            m_FrameTimePassTimestampPending = false;
            static bool s_loggedTimestampRequestConsumed = false;
            if (!s_loggedTimestampRequestConsumed)
            {
                s_loggedTimestampRequestConsumed = true;
                VULTRA_CORE_INFO("[RenderDevice] WebGPU pass timestamp write request consumed.");
            }
            return true;
#endif
        }

        double WebGPURenderDevice::consumeGpuFrameMs()
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            return -1.0;
#else
            if (!m_SupportsTimestampQuery || m_Instance == nullptr)
            {
                return m_LastGpuFrameMs;
            }

            wgpuInstanceProcessEvents(m_Instance);

            while (!m_PendingFrameTimeSlots.empty())
            {
                const uint32_t slotIndex = m_PendingFrameTimeSlots.front();
                if (slotIndex >= m_FrameTimeSlots.size())
                {
                    m_PendingFrameTimeSlots.pop_front();
                    continue;
                }

                auto& slot = m_FrameTimeSlots[slotIndex];
                if (slot.mapReady)
                {
                    break;
                }

                if (!slot.mapPending)
                {
                    WGPUBufferMapCallbackInfo mapCallback {};
                    mapCallback.mode      = WGPUCallbackMode_AllowProcessEvents;
                    mapCallback.callback  = onFrameTimeReadbackMapped;
                    mapCallback.userdata1 = new MapQueryUserData {.device = this, .slot = slotIndex};
                    mapCallback.userdata2 = nullptr;
                    wgpuBufferMapAsync(slot.readbackBuffer, WGPUMapMode_Read, 0, sizeof(uint64_t), mapCallback);
                    slot.mapPending = true;
                }

                // Help the first sample appear quickly on native backends where callbacks
                // are only progressed by explicit event pumping.
                if (slot.mapPending && m_LastGpuFrameMs < 0.0)
                {
                    for (int i = 0; i < 8 && slot.mapPending; ++i)
                    {
                        wgpuInstanceProcessEvents(m_Instance);
                        std::this_thread::yield();
                    }
                }

                break;
            }

            for (size_t i = 0; i < m_FrameTimeSlots.size(); ++i)
            {
                auto& slot = m_FrameTimeSlots[i];
                if (!slot.mapReady)
                {
                    continue;
                }

                const auto* const ptr = static_cast<const uint64_t*>(wgpuBufferGetConstMappedRange(slot.readbackBuffer,
                                                                                                     0,
                                                                                                     sizeof(uint64_t)));
                if (ptr != nullptr)
                {
                    const uint64_t timestamp = ptr[0];
                    if (m_HasGpuFrameTimestamp && timestamp >= m_LastGpuFrameTimestamp)
                    {
                        const uint64_t delta = timestamp - m_LastGpuFrameTimestamp;
                        m_LastGpuFrameMs     = static_cast<double>(delta) * 1e-6;
                        static bool s_loggedFirstGpuSample = false;
                        if (!s_loggedFirstGpuSample)
                        {
                            s_loggedFirstGpuSample = true;
                            VULTRA_CORE_INFO("[RenderDevice] WebGPU first GPU frame sample: {:.3f} ms", m_LastGpuFrameMs);
                        }
                    }
                    m_LastGpuFrameTimestamp = timestamp;
                    m_HasGpuFrameTimestamp  = true;
                }
                else
                {
                    static bool s_loggedNullMappedRange = false;
                    if (!s_loggedNullMappedRange)
                    {
                        s_loggedNullMappedRange = true;
                        VULTRA_CORE_WARN("[RenderDevice] WebGPU timestamp mapped range is null.");
                    }
                }

                wgpuBufferUnmap(slot.readbackBuffer);
                slot.mapReady = false;

                for (auto it = m_PendingFrameTimeSlots.begin(); it != m_PendingFrameTimeSlots.end();)
                {
                    if (*it == static_cast<uint32_t>(i))
                    {
                        it = m_PendingFrameTimeSlots.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            return m_LastGpuFrameMs;
#endif
        }

        uint64_t WebGPURenderDevice::beginScopeGpuQuery(const std::uintptr_t commandBufferHandle)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)commandBufferHandle;
            return 0;
#else
            if (!m_SupportsScopeTimestampQuery || m_Device == nullptr || commandBufferHandle == 0)
            {
                return 0;
            }

            if (m_ScopeTimeSlots.empty())
            {
                return 0;
            }

            const uint32_t slotIndex = acquireScopeTimeSlot();
            if (slotIndex == std::numeric_limits<uint32_t>::max())
            {
                static bool s_LoggedScopePoolExhausted = false;
                if (!s_LoggedScopePoolExhausted)
                {
                    s_LoggedScopePoolExhausted = true;
                    VULTRA_CORE_WARN(
                        "[RenderDevice] WebGPU GPU scope timestamp pool exhausted; skipping some scope timings this frame.");
                }
                return 0;
            }
            auto&          slot      = m_ScopeTimeSlots[slotIndex];

            if (slot.token != 0)
            {
                m_ScopeTimeTokenToSlot.erase(slot.token);
                for (auto it = m_PendingScopePassTimestampTokens.begin();
                     it != m_PendingScopePassTimestampTokens.end();)
                {
                    if (*it == slot.token)
                    {
                        it = m_PendingScopePassTimestampTokens.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }

            }

            if (slot.mapReady)
            {
                wgpuBufferUnmap(slot.readbackBuffer);
                slot.mapReady = false;
            }

            if (m_SupportsTimestampQueryInsideEncoders)
            {
                auto* encoder = reinterpret_cast<WGPUCommandEncoder>(commandBufferHandle);
                wgpuCommandEncoderWriteTimestamp(encoder, slot.querySet, 0u);
            }

            const uint64_t token = m_ScopeTimeNextToken++;
            slot.token           = token;
            slot.active          = true;
            slot.passTimestampIssued = false;
            slot.mapPending      = false;
            slot.mapReady        = false;
            slot.pendingResolve  = false;
            slot.resolveSubmitted = false;
            slot.resolved        = false;
            slot.ms              = -1.0;
            m_ScopeTimeTokenToSlot[token] = slotIndex;

            if (!m_SupportsTimestampQueryInsideEncoders)
            {
                m_PendingScopePassTimestampTokens.push_back(token);
            }
            return token;
#endif
        }

        void WebGPURenderDevice::endScopeGpuQuery(const std::uintptr_t commandBufferHandle, const uint64_t scopeToken)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)commandBufferHandle;
            (void)scopeToken;
#else
            if (!m_SupportsScopeTimestampQuery || m_Device == nullptr || commandBufferHandle == 0 || scopeToken == 0)
            {
                return;
            }

            const auto it = m_ScopeTimeTokenToSlot.find(scopeToken);
            if (it == m_ScopeTimeTokenToSlot.end())
            {
                return;
            }

            auto& slot = m_ScopeTimeSlots[it->second];
            if (!slot.active || slot.querySet == nullptr || slot.resolveBuffer == nullptr || slot.readbackBuffer == nullptr)
            {
                return;
            }

            (void)commandBufferHandle;
            if (!slot.passTimestampIssued)
            {
                slot.active         = false;
                slot.pendingResolve = false;
                slot.resolveSubmitted = false;
                slot.mapPending     = false;
                slot.mapReady       = false;
                slot.resolved       = true;
                slot.ms             = -1.0;
                return;
            }

            slot.active         = false;
            slot.pendingResolve = true;
            slot.resolveSubmitted = false;
            slot.mapPending     = false;
            slot.mapReady       = false;
#endif
        }

        void WebGPURenderDevice::finalizePendingPassTimestampQueries(const std::uintptr_t commandBufferHandle)
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (!m_SupportsScopeTimestampQuery || m_Device == nullptr || commandBufferHandle == 0)
            {
                return;
            }

            auto* encoder = reinterpret_cast<WGPUCommandEncoder>(commandBufferHandle);
            for (auto& slot : m_ScopeTimeSlots)
            {
                if (!slot.passTimestampIssued || !slot.pendingResolve || slot.resolveSubmitted || slot.resolved)
                {
                    continue;
                }
                if (slot.querySet == nullptr || slot.resolveBuffer == nullptr || slot.readbackBuffer == nullptr)
                {
                    continue;
                }

                wgpuCommandEncoderResolveQuerySet(encoder, slot.querySet, 0u, 2u, slot.resolveBuffer, 0u);
                wgpuCommandEncoderCopyBufferToBuffer(encoder,
                                                     slot.resolveBuffer,
                                                     0u,
                                                     slot.readbackBuffer,
                                                     0u,
                                                     sizeof(uint64_t) * 2u);

                slot.resolveSubmitted = true;
                slot.mapPending       = false;
                slot.mapReady         = false;
            }
#else
            (void)commandBufferHandle;
#endif
        }

        double WebGPURenderDevice::consumeScopeGpuMs(const uint64_t scopeToken)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)scopeToken;
            return -1.0;
#else
            if (!m_SupportsScopeTimestampQuery || m_Instance == nullptr || scopeToken == 0)
            {
                return -1.0;
            }

            const auto it = m_ScopeTimeTokenToSlot.find(scopeToken);
            if (it == m_ScopeTimeTokenToSlot.end())
            {
                return -2.0;
            }

            auto& slot = m_ScopeTimeSlots[it->second];
            if (slot.resolved)
            {
                if (slot.ms < 0.0)
                {
                    return -2.0;
                }
                return slot.ms;
            }

            if (!slot.pendingResolve)
            {
                return -1.0;
            }
            if (!slot.resolveSubmitted)
            {
                return -1.0;
            }

            if (!slot.mapReady && !slot.mapPending)
            {
                WGPUBufferMapCallbackInfo mapCallback {};
                mapCallback.mode      = WGPUCallbackMode_AllowProcessEvents;
                mapCallback.callback  = onScopeTimeReadbackMapped;
                mapCallback.userdata1 = new ScopeMapQueryUserData {.device = this, .slot = it->second};
                mapCallback.userdata2 = nullptr;
                wgpuBufferMapAsync(slot.readbackBuffer, WGPUMapMode_Read, 0, sizeof(uint64_t) * 2u, mapCallback);
                slot.mapPending = true;
            }

            wgpuInstanceProcessEvents(m_Instance);

            if (!slot.mapReady)
            {
                return -1.0;
            }

            const auto* const ptr = static_cast<const uint64_t*>(wgpuBufferGetConstMappedRange(slot.readbackBuffer,
                                                                                                 0,
                                                                                                 sizeof(uint64_t) * 2u));
            if (ptr == nullptr)
            {
                wgpuBufferUnmap(slot.readbackBuffer);
                slot.mapReady       = false;
                slot.mapPending     = false;
                slot.pendingResolve = false;
                slot.resolveSubmitted = false;
                slot.resolved       = true;
                slot.ms             = -1.0;
                return -1.0;
            }

            if (ptr[1] < ptr[0])
            {
                slot.ms = -1.0;
            }
            else
            {
                const uint64_t delta = ptr[1] - ptr[0];
                slot.ms              = static_cast<double>(delta) * 1e-6;
            }

            wgpuBufferUnmap(slot.readbackBuffer);
            slot.mapReady        = false;
            slot.mapPending      = false;
            slot.pendingResolve  = false;
            slot.resolveSubmitted = false;
            slot.resolved        = true;
            return slot.ms;
#endif
        }

        WebGPURenderDevice& WebGPURenderDevice::uploadDrawIndirect(
            DrawIndirectBuffer& buffer, const std::vector<DrawIndirectCommand>& commands)
        {
            if (commands.empty())
            {
                return *this;
            }

            struct WebGPUDrawIndirectCommand
            {
                uint32_t vertexCount {0};
                uint32_t instanceCount {0};
                uint32_t firstVertex {0};
                uint32_t firstInstance {0};
            };

            struct WebGPUDrawIndexedIndirectCommand
            {
                uint32_t indexCount {0};
                uint32_t instanceCount {0};
                uint32_t firstIndex {0};
                int32_t  baseVertex {0};
                uint32_t firstInstance {0};
            };

            assert(buffer);
            assert(commands.size() <= buffer.getSize());

            auto*      dst    = static_cast<std::byte*>(buffer.map());
            const auto type   = buffer.getDrawIndirectType();
            const auto stride = buffer.getStride();

            for (uint32_t i = 0; i < commands.size(); ++i)
            {
                const DrawIndirectCommand& cmd = commands[i];
                std::byte*                 ptr = dst + i * stride;

                if (type == DrawIndirectType::eIndexed)
                {
                    WebGPUDrawIndexedIndirectCommand webgpuCmd {};
                    webgpuCmd.indexCount    = cmd.count;
                    webgpuCmd.instanceCount = cmd.instanceCount;
                    webgpuCmd.firstIndex    = cmd.first;
                    webgpuCmd.baseVertex    = cmd.vertexOffset;
                    webgpuCmd.firstInstance = cmd.firstInstance;
                    std::memcpy(ptr, &webgpuCmd, sizeof(webgpuCmd));
                }
                else
                {
                    WebGPUDrawIndirectCommand webgpuCmd {};
                    webgpuCmd.vertexCount   = cmd.count;
                    webgpuCmd.instanceCount = cmd.instanceCount;
                    webgpuCmd.firstVertex   = cmd.first;
                    webgpuCmd.firstInstance = cmd.firstInstance;
                    std::memcpy(ptr, &webgpuCmd, sizeof(webgpuCmd));
                }
            }

            buffer.flush().unmap();
            return *this;
        }

        WebGPURenderDevice::~WebGPURenderDevice()
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            for (auto& slot : m_FrameTimeSlots)
            {
                if (slot.readbackBuffer)
                {
                    wgpuBufferRelease(slot.readbackBuffer);
                    slot.readbackBuffer = nullptr;
                }
                if (slot.resolveBuffer)
                {
                    wgpuBufferRelease(slot.resolveBuffer);
                    slot.resolveBuffer = nullptr;
                }
                if (slot.querySet)
                {
                    wgpuQuerySetRelease(slot.querySet);
                    slot.querySet = nullptr;
                }
            }
            m_FrameTimeSlots.clear();

            for (auto& slot : m_ScopeTimeSlots)
            {
                if (slot.readbackBuffer)
                {
                    wgpuBufferRelease(slot.readbackBuffer);
                    slot.readbackBuffer = nullptr;
                }
                if (slot.resolveBuffer)
                {
                    wgpuBufferRelease(slot.resolveBuffer);
                    slot.resolveBuffer = nullptr;
                }
                if (slot.querySet)
                {
                    wgpuQuerySetRelease(slot.querySet);
                    slot.querySet = nullptr;
                }
            }
            m_ScopeTimeSlots.clear();

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
