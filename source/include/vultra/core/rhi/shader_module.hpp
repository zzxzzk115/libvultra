#pragma once

#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"

namespace vultra
{
    namespace rhi
    {
        class ShaderModule final
        {
        public:
            ShaderModule() = default;
            explicit ShaderModule(SPIRV spirv) : m_Spirv(std::move(spirv)) {}

            ShaderModule(const ShaderModule&)            = default;
            ShaderModule(ShaderModule&&) noexcept        = default;
            ShaderModule& operator=(const ShaderModule&)  = default;
            ShaderModule& operator=(ShaderModule&&) noexcept = default;

            [[nodiscard]] explicit operator bool() const { return !m_Spirv.empty(); }
            [[nodiscard]] const SPIRV& getSpirv() const { return m_Spirv; }
            [[nodiscard]] SPIRV&       getSpirv() { return m_Spirv; }

            [[nodiscard]] const ShaderReflection& getReflection() const { return m_Reflection; }
            [[nodiscard]] ShaderReflection&       getReflection() { return m_Reflection; }

        private:
            SPIRV            m_Spirv;
            ShaderReflection m_Reflection;
        };
    } // namespace rhi
} // namespace vultra

