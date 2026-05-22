#pragma once

#include <vbase/service/service_registry.hpp>

#include <cstdint>

namespace vultra
{
    class IFrameDebuggerService
    {
    public:
        SERVICE_REGISTER(IFrameDebuggerService);

        virtual void captureSingleFrame() = 0;
        virtual void showReplayUI()       = 0;

        [[nodiscard]] virtual bool     isAvailable() const        = 0;
        [[nodiscard]] virtual bool     isFrameCapturing() const   = 0;
        [[nodiscard]] virtual uint32_t getCaptureCount() const    = 0;
        [[nodiscard]] virtual bool     isRenderDocEnabled() const = 0;

    protected:
        friend class RenderSystem;

        virtual void captureStart() = 0;
        virtual void captureEnd()   = 0;
    };
} // namespace vultra
