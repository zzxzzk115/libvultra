#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/core/scoped_enum_flags.hpp>

namespace vultra
{
    namespace rhi
    {
        enum class ImageAspect
        {
            eNone = 0,
            eColor,
            eDepth,
            eStencil
        };

        enum class ImageAspectFlags
        {
            eNone    = ZERO_BIT,
            eColor   = BIT(0),
            eDepth   = BIT(1),
            eStencil = BIT(2),
            eAll     = eColor | eDepth | eStencil,
        };
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::ImageAspectFlags> : std::true_type
{};
