#pragma once

#include "vultra/core/base/common_context.hpp"

#ifndef VK_CHECK
#define VK_CHECK(result, tag, except) \
    if (const auto res = result; res != vk::Result::eSuccess) \
    { \
        VULTRA_CORE_ERROR("[{}] {} ({}:{}): {}", tag, except, __FILE__, __LINE__, vk::to_string(res)); \
        throw std::runtime_error(except); \
    }
#endif
