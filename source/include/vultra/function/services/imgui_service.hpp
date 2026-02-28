#pragma once

#include "vultra/core/os/window.hpp"
#include "vultra/core/rhi/command_buffer.hpp"

#include <vbase/service/service_registry.hpp>

namespace vultra
{
    class IImGuiService
    {
    public:
        SERVICE_REGISTER(IImGuiService);

        virtual void processEvent(const os::GeneralWindowEvent& event)                = 0;
        virtual void begin()                                                          = 0;
        virtual void render(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& info) = 0;
        virtual void end()                                                            = 0;
        virtual void postRender()                                                     = 0;
    };
} // namespace vultra
