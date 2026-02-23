#pragma once
#include <cstdint>

namespace vultra
{
    enum class AssetState : uint8_t
    {
        eUnloaded = 0,

        // TODO: remove sync version
        eLoaded,

        // CPU pipeline
        eLoadingCPU,
        eCPUReady,

        // GPU pipeline
        eUploadingGPU,
        eReady,

        eFailed,
    };
}