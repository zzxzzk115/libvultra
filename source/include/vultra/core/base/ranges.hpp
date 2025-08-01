#pragma once

#include <ranges>
#include <version>

namespace vultra
{
    template<typename T>
    auto enumerate(T&& container)
    {
#ifdef __cpp_lib_ranges_enumerate
        return container | std::views::enumerate;
#else
        return std::views::zip(std::views::iota(0), container);
#endif
    }
} // namespace vultra