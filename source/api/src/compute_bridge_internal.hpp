#pragma once

#include <vultra/api/compute_bridge.hpp>
#include <vultra/drivers/compute/compute_context.hpp>

#include <string>

struct VultraComputeContext
{
    explicit VultraComputeContext(bool validation) :
        value(validation)
    {
    }

    vultra::ComputeContext value;
    std::string            error;
};

namespace vultra::detail
{
    VultraStatus computeFailure(VultraComputeContext* context, VultraStatus status, const char* message) noexcept;
    VultraStatus VULTRA_ABI_CALL createComputeContext(uint32_t               validation,
                                                      VultraComputeContext** context,
                                                      char*                  error,
                                                      uint64_t               capacity);
    void VULTRA_ABI_CALL         destroyComputeContext(VultraComputeContext* context);
    VultraStatus VULTRA_ABI_CALL computeLastError(VultraComputeContext* context,
                                                  char*                 data,
                                                  uint64_t              capacity,
                                                  uint64_t*             size);
} // namespace vultra::detail
