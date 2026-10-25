#pragma once

#include <string>
#include <vector>

struct GameActivity;

namespace vultra::platform
{
    // Read the Intent's explicit UTF-8 argument array on the native thread.
    std::vector<std::string> androidArguments(GameActivity& activity);
} // namespace vultra::platform
