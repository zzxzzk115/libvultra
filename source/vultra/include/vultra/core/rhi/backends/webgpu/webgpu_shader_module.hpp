#pragma once

#include "vultra/core/rhi/interfaces/ishader_module.hpp"

namespace vultra
{
    namespace rhi
    {
        class WebGPUShaderModule final : public IShaderModule
        {
        public:
            explicit WebGPUShaderModule(std::string wgsl) : m_Wgsl(std::move(wgsl)) {}
            ~WebGPUShaderModule() override = default;

            [[nodiscard]] bool isValid() const override { return !m_Wgsl.empty(); }
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
