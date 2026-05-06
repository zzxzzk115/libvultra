#pragma once

#include "vultra/core/rhi/shader_reflection.hpp"

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
            std::optional<ShaderReflection>                             reflection;
        };
    } // namespace rhi
} // namespace vultra
