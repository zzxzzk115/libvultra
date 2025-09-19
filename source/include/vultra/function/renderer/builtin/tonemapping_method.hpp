#pragma once

namespace vultra
{
    namespace gfx
    {
        // NOLINTBEGIN
        enum class ToneMappingMethod
        {
            KhronosPBRNeutral = 0,
            ACES              = 1,
            Reinhard          = 2, // Legacy
        };
        // NOLINTEND
    } // namespace gfx
} // namespace vultra