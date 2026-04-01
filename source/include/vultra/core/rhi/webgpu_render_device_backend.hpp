#pragma once

namespace vultra
{
    namespace rhi
    {
        // TODO: Implement a native WebGPU backend here.
        // The first cut should keep the interface backend-neutral and return explicit
        // "not implemented" capability information until wgpu-native is wired in.
        class WebGPURenderDeviceBackend final
        {
        public:
            WebGPURenderDeviceBackend() = default;
        };
    } // namespace rhi
} // namespace vultra
