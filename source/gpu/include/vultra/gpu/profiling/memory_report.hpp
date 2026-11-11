#pragma once

#include <vultra/gpu/device.hpp>

#include <expected>
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
    [[nodiscard]] std::expected<MemoryReport, Error> memoryReport(const Device& device);
} // namespace vultra
