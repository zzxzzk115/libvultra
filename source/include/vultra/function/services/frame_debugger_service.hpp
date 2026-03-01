#pragma once

#include <vbase/service/service_registry.hpp>

namespace vultra
{
    class IFrameDebuggerService
    {
    public:
        SERVICE_REGISTER(IFrameDebuggerService);

        virtual void captureSingleFrame() = 0;

    protected:
        friend class RenderSystem;

        virtual void captureStart() = 0;
        virtual void captureEnd()   = 0;
    };
} // namespace vultra