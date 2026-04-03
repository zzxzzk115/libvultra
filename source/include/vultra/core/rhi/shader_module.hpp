#pragma once

#include "vultra/core/rhi/interfaces/ishader_module.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"

#include <memory>
#include <string>

namespace vultra
{
    namespace rhi
    {
        class ShaderModule final
        {
        public:
            ShaderModule() = default;
            explicit ShaderModule(std::unique_ptr<IShaderModule> impl);

            ShaderModule(const ShaderModule&)            = delete;
            ShaderModule(ShaderModule&&) noexcept        = default;
            ShaderModule& operator=(const ShaderModule&)  = delete;
            ShaderModule& operator=(ShaderModule&&) noexcept = default;

            [[nodiscard]] explicit operator bool() const;
            [[nodiscard]] const SPIRV& getSpirv() const;
            [[nodiscard]] SPIRV&       getSpirv();
            [[nodiscard]] const std::string& getWgsl() const;
            [[nodiscard]] std::string&       getWgsl();

            [[nodiscard]] const ShaderReflection& getReflection() const;
            [[nodiscard]] ShaderReflection&       getReflection();

        private:
            std::unique_ptr<IShaderModule> m_Impl;
        };
    } // namespace rhi
} // namespace vultra
