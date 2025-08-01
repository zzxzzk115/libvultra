#pragma once

#include <cassert>
#include <chrono>
#include <memory>

using namespace std::chrono_literals;

using fsec = std::chrono::duration<float>;

#ifdef _WIN32
#include <intrin.h> // for __debugbreak
#define DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#include <signal.h> // for raise & SIGTRAP
#define DEBUG_BREAK() raise(SIGTRAP)
#else
#include <cstdlib>            // for abort
#define DEBUG_BREAK() abort() // Fallback if no debug break is available
#endif

#ifndef VULTRA_CUSTOM_ASSERT
#define VULTRA_CUSTOM_ASSERT(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            DEBUG_BREAK(); \
            assert(condition); \
        } \
    } while (false)
#endif

#ifndef BIT
#define BIT(x) (1 << x)
#endif

#ifndef ZERO_BIT
#define ZERO_BIT 0
#endif

namespace vultra
{
    template<typename T>
    using Scope = std::unique_ptr<T>;

    template<typename T, typename... Args>
    constexpr Scope<T> createScope(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    using Ref = std::shared_ptr<T>;

    template<typename T, typename... Args>
    constexpr Ref<T> createRef(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
} // namespace vultra