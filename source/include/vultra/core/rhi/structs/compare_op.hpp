#pragma once

namespace vultra
{
    namespace rhi
    {
        enum class CompareOp
        {
            eNever,
            eLess,
            eEqual,
            eLessOrEqual,
            eGreater,
            eNotEqual,
            eGreaterOrEqual,
            eAlways,
        };
    } // namespace rhi
} // namespace vultra
