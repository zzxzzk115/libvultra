#pragma once

#include "vultra/core/rhi/interfaces/ishader_module.hpp"

namespace vultra
{
    namespace rhi
    {
        class VulkanShaderModule final : public IShaderModule
        {
        public:
            explicit VulkanShaderModule(SPIRV spirv) : m_Spirv(std::move(spirv)) {}
            ~VulkanShaderModule() override = default;

            [[nodiscard]] bool isValid() const override { return !m_Spirv.empty(); }
            [[nodiscard]] const SPIRV& getSpirv() const override { return m_Spirv; }
            [[nodiscard]] SPIRV&       getSpirv() override { return m_Spirv; }
            [[nodiscard]] const std::string& getWgsl() const override { return m_Wgsl; }
            [[nodiscard]] std::string&       getWgsl() override { return m_Wgsl; }
            [[nodiscard]] const ShaderReflection& getReflection() const override { return m_Reflection; }
            [[nodiscard]] ShaderReflection&       getReflection() override { return m_Reflection; }

        private:
            SPIRV            m_Spirv;
            std::string      m_Wgsl;
            ShaderReflection m_Reflection;
        };
    } // namespace rhi
} // namespace vultra
