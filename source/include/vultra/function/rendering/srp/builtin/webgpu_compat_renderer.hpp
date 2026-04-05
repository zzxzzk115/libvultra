#pragma once

#include "vultra/function/rendering/srp/builtin/legacy_renderer.hpp"

namespace vultra
{
    class WebGPUCompatRenderer final : public LegacyRenderer
    {
    public:
        WebGPUCompatRenderer();
    };
} // namespace vultra
