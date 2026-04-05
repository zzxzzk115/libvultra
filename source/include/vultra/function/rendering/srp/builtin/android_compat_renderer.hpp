#pragma once

#include "vultra/function/rendering/srp/builtin/legacy_renderer.hpp"

namespace vultra
{
    class AndroidCompatRenderer final : public LegacyRenderer
    {
    public:
        AndroidCompatRenderer();
    };
} // namespace vultra
