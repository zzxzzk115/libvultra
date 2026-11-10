#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

namespace vultra::asset_detail
{
    // Jobs own disjoint output ranges. The call joins started jobs before returning an error.
    std::expected<void, std::string>
    runImportJobsExpected(std::string_view                                                 phase,
                          uint32_t                                                         count,
                          uint32_t                                                         workerLimit,
                          uint64_t                                                         scratchPerJob,
                          const std::function<std::expected<void, std::string>(uint32_t)>& job);

    // Jobs own disjoint output ranges. This call joins started jobs before returning or throwing.
    void runImportJobs(std::string_view                     phase,
                       uint32_t                             count,
                       uint32_t                             workerLimit,
                       uint64_t                             scratchPerJob,
                       const std::function<void(uint32_t)>& job);
} // namespace vultra::asset_detail
