#pragma once

#include "vultra/core/os/window.hpp"

#include <vbase/service/service_registry.hpp>

class IWindowService
{
public:
    SERVICE_REGISTER(IWindowService)
    virtual vultra::os::Window& window() = 0;
};
