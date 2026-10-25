#include "compute_bridge_internal.hpp"

#include <algorithm>
#include <cstring>

namespace vultra::detail
{
    VultraStatus computeFailure(VultraComputeContext* context, VultraStatus status, const char* message) noexcept
    {
        try
        {
            context->error = message;
        }
        catch (...)
        {
            context->error.clear();
        }
        return status;
    }

    VultraStatus VULTRA_ABI_CALL createComputeContext(uint32_t               validation,
                                                      VultraComputeContext** context,
                                                      char*                  error,
                                                      uint64_t               capacity)
    {
        if (!context || (!error && capacity) || capacity > SIZE_MAX || validation > 1)
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        *context = nullptr;
        if (capacity)
        {
            *error = 0;
        }
        try
        {
            *context = new VultraComputeContext(validation != 0);
            return VULTRA_STATUS_OK;
        }
        catch (const std::exception& failure)
        {
            if (capacity)
            {
                const auto count = std::min(size_t(capacity - 1), std::strlen(failure.what()));
                std::memcpy(error, failure.what(), count);
                error[count] = 0;
            }
            return VULTRA_STATUS_ERROR;
        }
        catch (...)
        {
            return VULTRA_STATUS_ERROR;
        }
    }

    void VULTRA_ABI_CALL destroyComputeContext(VultraComputeContext* context)
    {
        delete context;
    }

    VultraStatus VULTRA_ABI_CALL computeLastError(VultraComputeContext* context,
                                                  char*                 data,
                                                  uint64_t              capacity,
                                                  uint64_t*             size)
    {
        if (!context || !size || (!data && capacity) || capacity > SIZE_MAX)
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        *size = context->error.size();
        if (!data)
        {
            return VULTRA_STATUS_OK;
        }
        if (capacity < *size)
        {
            return VULTRA_STATUS_BUFFER_TOO_SMALL;
        }
        std::memcpy(data, context->error.data(), size_t(*size));
        return VULTRA_STATUS_OK;
    }
} // namespace vultra::detail
