#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        struct ShaderStageInfo
        {
            std::string                                                 code;
            std::string                                                 entryPointName {"main"};
            std::unordered_map<std::string, std::optional<std::string>> defines;
        };
    } // namespace rhi
} // namespace vultra
