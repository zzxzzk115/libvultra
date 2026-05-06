#pragma once

#include "vultra/core/rhi/shader_reflection.hpp"

#include <string>

namespace vultra
{
    namespace rhi
    {
        class IShaderModule
        {
        public:
            virtual ~IShaderModule() = default;

            [[nodiscard]] virtual bool isValid() const = 0;
            [[nodiscard]] virtual const SPIRV& getSpirv() const = 0;
            [[nodiscard]] virtual SPIRV&       getSpirv() = 0;
            [[nodiscard]] virtual const std::string& getWgsl() const = 0;
            [[nodiscard]] virtual std::string&       getWgsl() = 0;

            [[nodiscard]] virtual const ShaderReflection& getReflection() const = 0;
            [[nodiscard]] virtual ShaderReflection&       getReflection() = 0;
        };
    } // namespace rhi
} // namespace vultra
