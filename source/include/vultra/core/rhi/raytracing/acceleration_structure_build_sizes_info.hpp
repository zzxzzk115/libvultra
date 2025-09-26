#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct AccelerationStructureBuildSizesInfo
        {
            uint64_t accelerationStructureSize {0};
            uint64_t updateScratchSize {0};
            uint64_t buildScratchSize {0};
        };
    } // namespace rhi
} // namespace vultra