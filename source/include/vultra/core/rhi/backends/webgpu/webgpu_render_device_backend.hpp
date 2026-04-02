#pragma once

#include "vultra/core/rhi/interfaces/irender_device_backend.hpp"

namespace vultra
{
    namespace rhi
    {
        class WebGPURenderDeviceBackend final : public IRenderDeviceBackend
        {
        public:
            WebGPURenderDeviceBackend() = default;
        };
    } // namespace rhi
} // namespace vultra
