#pragma once

#include <string>

namespace vultra
{
    struct ScriptComponent
    {
        std::string scriptUri;
        bool        enabled {true};
    };
} // namespace vultra
