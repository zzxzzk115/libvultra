#pragma once

#include <vultra/drivers/rhi/device.hpp>

#include <optional>
#include <vector>

namespace vultra
{
    struct MemoryReport
    {
        std::optional<VriVideoMemoryInfo> video;
        std::optional<uint64_t>           trackedBytes;
        std::vector<VriObjectInfo>        objects;
    };

    // A snapshot of actual VRI allocator ownership, distinct from driver-wide budget/usage.
    MemoryReport memoryReport(const Device& device);
} // namespace vultra
