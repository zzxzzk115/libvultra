#pragma once

#include <vultra/core/base/error.hpp>

#include <expected>
#include <string>
#include <vector>

struct GameActivity;

namespace vultra::platform
{
    // Read the Intent's explicit UTF-8 argument array on the native thread.
    [[nodiscard]] std::expected<std::vector<std::string>, Error> androidArguments(GameActivity& activity);
} // namespace vultra::platform
