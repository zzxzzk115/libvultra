#pragma once

#include <vultra/core/base/error.hpp>

#include <expected>
#include <filesystem>

namespace vultra
{
    [[nodiscard]] std::expected<std::filesystem::path, Error> executablePath();
} // namespace vultra
