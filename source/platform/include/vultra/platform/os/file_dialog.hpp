#pragma once

#include <vultra/platform/os/window.hpp>

#include <filesystem>
#include <optional>

namespace vultra
{
    // Cancellation returns nullopt; platform errors throw. Does not change the working directory.
    std::optional<std::filesystem::path> openModelDialog(const Window& owner);
} // namespace vultra
