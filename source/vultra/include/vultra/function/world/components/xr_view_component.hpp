#pragma once

#include <cstdint>

namespace vultra
{
    enum class XRTrackingOrigin : uint32_t
    {
        eLocal = 0,
        eStage = 1,
    };

    enum class XRStereoGraphMode : uint32_t
    {
        eSingleGraphStereo = 0,
    };

    struct XRViewComponent
    {
        bool     enabled {true};
        uint32_t trackingOrigin {static_cast<uint32_t>(XRTrackingOrigin::eLocal)};
        uint32_t stereoGraphMode {static_cast<uint32_t>(XRStereoGraphMode::eSingleGraphStereo)};
        bool     fallbackMono {true};
    };
} // namespace vultra
