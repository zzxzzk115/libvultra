#pragma once

#include <vultra/core/base/error.hpp>

#include <cstdint>
#include <expected>

namespace vultra
{
    [[nodiscard]] std::expected<uint64_t, Error> availablePhysicalMemory();
} // namespace vultra
