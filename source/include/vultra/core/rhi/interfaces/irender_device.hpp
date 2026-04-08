#pragma once

#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"

#include <array>
#include <cstdint>

namespace vultra
{
    namespace openxr
    {
        class XRDevice;
    }
} // namespace vultra

namespace vultra
{
    namespace rhi
    {
        class IRenderDevice
        {
        public:
            virtual ~IRenderDevice() = default;

            [[nodiscard]] virtual RenderBackendApi             getBackendApi() const         = 0;
            [[nodiscard]] virtual RenderDeviceFeatureFlagBits  getFeatureFlag() const        = 0;
            [[nodiscard]] virtual RenderDeviceFeatureReport    getFeatureReport() const      = 0;
            [[nodiscard]] virtual RenderDeviceLimits           getLimits() const            = 0;
            [[nodiscard]] virtual RenderDeviceSyncCapabilities getSyncCapabilities() const   = 0;
            [[nodiscard]] virtual bool                         supportsSwapchain() const     = 0;
            [[nodiscard]] virtual std::string                  getName() const               = 0;
            [[nodiscard]] virtual PhysicalDeviceInfo           getPhysicalDeviceInfo() const = 0;
            [[nodiscard]] virtual openxr::XRDevice*            getXRDevice() const           = 0;

            virtual void beginFrameGpuQuery(std::uintptr_t commandBufferHandle) = 0;
            virtual void endFrameGpuQuery(std::uintptr_t commandBufferHandle)   = 0;
            [[nodiscard]] virtual double consumeGpuFrameMs()                     = 0;
            [[nodiscard]] virtual uint64_t beginScopeGpuQuery(std::uintptr_t commandBufferHandle) = 0;
            virtual void                  endScopeGpuQuery(std::uintptr_t commandBufferHandle, uint64_t scopeToken) = 0;
            [[nodiscard]] virtual double  consumeScopeGpuMs(uint64_t scopeToken) = 0;

            virtual void onMemoryAllocated(RenderMemoryKind kind, uint64_t bytes) = 0;
            virtual void onMemoryFreed(RenderMemoryKind kind, uint64_t bytes)     = 0;
            [[nodiscard]] virtual RenderDeviceMemoryStats getMemoryStats() const  = 0;

            [[nodiscard]] virtual std::array<float, 2> getLineWidthRange() const { return {1.0f, 1.0f}; }
            [[nodiscard]] virtual float                getMaxSamplerAnisotropy() const { return 1.0f; }
            [[nodiscard]] virtual uint64_t             getFormatFeatureFlagsOptimal(PixelFormat) const { return 0u; }
        };
    } // namespace rhi
} // namespace vultra
