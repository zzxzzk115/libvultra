#pragma once

#include <cstdint>

namespace vultra
{
    constexpr uint32_t kRenderLayerDefaultMask = 1u << 0u;
    constexpr uint32_t kRenderLayerUiMask      = 1u << 5u;
    constexpr uint32_t kRenderLayerAllMask     = 0xFFFFFFFFu;

    struct LayerComponent
    {
        uint32_t mask {kRenderLayerDefaultMask};
    };
} // namespace vultra
