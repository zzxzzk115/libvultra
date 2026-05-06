#pragma once
#include <cstdint>

namespace vultra
{
    enum class AssetState : uint8_t
    {
        eUnloaded = 0,

        // Sync legacy (kept for compatibility; will be removed once async pipeline is fully in place).
        eLoaded,

        // CPU pipeline
        eLoadingCPU,
        eCPUReady,

        // GPU pipeline (GPU upload must happen on main/render thread).
        // eUploadQueued is a transient state used to avoid enqueueing multiple upload commands.
        eUploadQueued,
        eUploadingGPU,
        eReady,

        eFailed,
    };
}