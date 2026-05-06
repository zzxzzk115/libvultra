#pragma once

#include <cstdint>
#include <type_traits>

namespace vultra
{
    namespace rhi
    {
        template <typename Handle, typename RawHandle>
        [[nodiscard]] constexpr Handle asVkHandle(const RawHandle rawHandle)
        {
            using Raw = std::remove_cv_t<std::remove_reference_t<RawHandle>>;
            if constexpr (std::is_pointer_v<Handle>)
            {
                if constexpr (std::is_pointer_v<Raw>)
                {
                    return reinterpret_cast<Handle>(rawHandle);
                }
                else
                {
                    return reinterpret_cast<Handle>(static_cast<std::uintptr_t>(rawHandle));
                }
            }
            else
            {
                if constexpr (std::is_pointer_v<Raw>)
                {
                    return static_cast<Handle>(reinterpret_cast<std::uintptr_t>(rawHandle));
                }
                else
                {
                    return static_cast<Handle>(rawHandle);
                }
            }
        }

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

        template <typename Handle>
        [[nodiscard]] std::uintptr_t toBackendHandle(Handle handle)
        {
            return static_cast<std::uintptr_t>(getVulkanHandleId(handle));
        }
    } // namespace rhi
} // namespace vultra
