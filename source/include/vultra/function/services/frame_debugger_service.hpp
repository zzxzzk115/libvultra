#pragma once

#include <vbase/service/service_registry.hpp>

class IFrameDebuggerService
{
public:
    SERVICE_REGISTER(IFrameDebuggerService);

    virtual void captureSingleFrame() = 0;
};