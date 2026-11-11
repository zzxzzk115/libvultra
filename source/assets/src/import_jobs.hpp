#pragma once

#include <vultra/core/base/error.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <string_view>

namespace vultra::asset_detail
{
    // Jobs own disjoint output ranges. This call joins every job before returning; after a failed job, unstarted
    // jobs are skipped and the failure with the lowest job index is returned.
    [[nodiscard]] std::expected<void, Error>
    runImportJobs(std::string_view                                           phase,
                  uint32_t                                                   count,
                  uint32_t                                                   workerLimit,
                  uint64_t                                                   scratchPerJob,
                  const std::function<std::expected<void, Error>(uint32_t)>& job);
} // namespace vultra::asset_detail
