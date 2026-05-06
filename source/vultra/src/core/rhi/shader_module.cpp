#include "vultra/core/rhi/shader_module.hpp"

#include <cassert>

namespace vultra::rhi
{
    ShaderModule::ShaderModule(std::unique_ptr<IShaderModule> impl) : m_Impl(std::move(impl)) {}

    ShaderModule::operator bool() const { return m_Impl && m_Impl->isValid(); }

    const SPIRV& ShaderModule::getSpirv() const
    {
        assert(m_Impl);
        return m_Impl->getSpirv();
    }

    SPIRV& ShaderModule::getSpirv()
    {
        assert(m_Impl);
        return m_Impl->getSpirv();
    }

    const std::string& ShaderModule::getWgsl() const
    {
        assert(m_Impl);
        return m_Impl->getWgsl();
    }

    std::string& ShaderModule::getWgsl()
    {
        assert(m_Impl);
        return m_Impl->getWgsl();
    }

    const ShaderReflection& ShaderModule::getReflection() const
    {
        assert(m_Impl);
        return m_Impl->getReflection();
    }

    ShaderReflection& ShaderModule::getReflection()
    {
        assert(m_Impl);
        return m_Impl->getReflection();
    }
} // namespace vultra::rhi
