#pragma once

#include <vultra/core/math/extent.hpp>

#include <vri/vri.h>

#include <optional>
#include <string>

namespace vultra
{
    struct AppConfig
    {
        std::string title = "Vultra";
        Extent      size {1280, 720};
        bool        validation = true;
        // Tone-mapped output and ImGui use display-encoded values.
        VriFormat swapchainFormat = VriFormat_BGRA8_UNORM;
        // Required VRI features. Device creation fails explicitly when unavailable.
        uint64_t                features = 0;
        std::optional<uint64_t> renderDocFrame; // zero-based; requires an injected RenderDoc session
    };

} // namespace vultra
