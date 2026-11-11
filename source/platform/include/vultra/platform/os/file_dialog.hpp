#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/platform/os/window.hpp>

#include <expected>
#include <filesystem>
#include <optional>

namespace vultra
{
    // Cancellation returns nullopt. Does not change the working directory.
    [[nodiscard]] std::expected<std::optional<std::filesystem::path>, Error> openModelDialog(const Window& owner);
} // namespace vultra
