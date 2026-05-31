#pragma once

#include <cstdint>

namespace vultra_app
{
    struct SystemMemorySnapshot
    {
        bool     processResidentAvailable {false};
        bool     systemMemoryAvailable {false};
        uint64_t processResidentBytes {0};
        uint64_t systemAvailableBytes {0};
        uint64_t systemTotalBytes {0};
    };

    SystemMemorySnapshot querySystemMemory();
} // namespace vultra_app
