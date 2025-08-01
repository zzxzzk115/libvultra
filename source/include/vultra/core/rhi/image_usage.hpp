#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/scoped_enum_flags.hpp"

#include <string_view>

namespace vultra
{
    namespace rhi
    {
        enum class ImageUsage
        {
            eTransferSrc  = BIT(0),
            eTransferDst  = BIT(1),
            eTransfer     = eTransferSrc | eTransferDst,
            eStorage      = BIT(2),
            eRenderTarget = BIT(3),
            eSampled      = BIT(4),
        };

        [[nodiscard]] std::string_view toString(ImageUsage);
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::ImageUsage> : std::true_type
{};
