#pragma once

#include <cstdint>
#include <type_traits>

namespace vultra
{
    namespace rhi
    {
        template <typename Handle>
        [[nodiscard]] uint64_t getVulkanHandleId(Handle handle)
        {
            if constexpr (std::is_pointer_v<Handle>)
            {
                return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
            }
            else
            {
                return static_cast<uint64_t>(handle);
            }
        }
    } // namespace rhi
} // namespace vultra
