#pragma once

#include "vultra/core/base/base.hpp"

namespace vultra
{
    namespace rhi
    {
        enum class CullMode
        {
            eNone  = ZERO_BIT,
            eFront = BIT(0),
            eBack  = BIT(1),
        };

        [[nodiscard]] std::string_view toString(const CullMode);
    } // namespace rhi
} // namespace vultra
