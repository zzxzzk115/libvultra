#pragma once

#include <string_view>

namespace vultra
{
    namespace rhi
    {
        enum class CubeFace
        {
            ePositiveX = 0,
            eNegativeX,
            ePositiveY,
            eNegativeY,
            ePositiveZ,
            eNegativeZ,
        };

        [[nodiscard]] std::string_view toString(CubeFace);
    } // namespace rhi
} // namespace vultra