#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct FenceHandle
        {
            std::uintptr_t value {0};

            constexpr FenceHandle() = default;
            constexpr FenceHandle(std::uintptr_t v) : value(v) {}
            [[nodiscard]] constexpr explicit operator bool() const { return value != 0; }
            [[nodiscard]] constexpr operator std::uintptr_t() const { return value; }
        };

        struct SemaphoreHandle
        {
            std::uintptr_t value {0};

            constexpr SemaphoreHandle() = default;
            constexpr SemaphoreHandle(std::uintptr_t v) : value(v) {}
            [[nodiscard]] constexpr explicit operator bool() const { return value != 0; }
            [[nodiscard]] constexpr operator std::uintptr_t() const { return value; }
        };

        struct DescriptorSetHandle
        {
            std::uintptr_t value {0};

            constexpr DescriptorSetHandle() = default;
            constexpr DescriptorSetHandle(std::uintptr_t v) : value(v) {}
            [[nodiscard]] constexpr explicit operator bool() const { return value != 0; }
            [[nodiscard]] constexpr operator std::uintptr_t() const { return value; }
        };

        struct SamplerHandle
        {
            std::uintptr_t value {0};

            constexpr SamplerHandle() = default;
            constexpr SamplerHandle(std::uintptr_t v) : value(v) {}
            [[nodiscard]] constexpr explicit operator bool() const { return value != 0; }
            [[nodiscard]] constexpr operator std::uintptr_t() const { return value; }
        };
    } // namespace rhi
} // namespace vultra
