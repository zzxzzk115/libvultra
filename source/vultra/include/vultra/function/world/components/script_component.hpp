#pragma once

#include <string>

namespace vultra
{
    struct ScriptComponent
    {
        // Engine URI, not a raw filesystem path. Resolved by the asset service.
        std::string scriptUri;
        bool        enabled {true};
    };
} // namespace vultra
