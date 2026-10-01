#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace vultra::asset_detail
{
    // Jobs own disjoint output ranges. This call joins every job before returning or throwing.
    void runImportJobs(std::string_view                     phase,
                       uint32_t                             count,
                       uint32_t                             workerLimit,
                       uint64_t                             scratchPerJob,
                       const std::function<void(uint32_t)>& job);
} // namespace vultra::asset_detail
