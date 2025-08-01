#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/vk/macro.hpp"

namespace vultra
{
    namespace rhi
    {
        ShaderModule::ShaderModule(ShaderModule&& other) noexcept : m_Device(other.m_Device), m_Handle(other.m_Handle)
        {
            other.m_Handle = nullptr;
            other.m_Device = nullptr;
        }

        ShaderModule::~ShaderModule() { destroy(); }

        ShaderModule& ShaderModule::operator=(ShaderModule&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                std::swap(m_Device, rhs.m_Device);
                std::swap(m_Handle, rhs.m_Handle);
            }

            return *this;
        }

        ShaderModule::operator bool() const { return m_Handle != nullptr; }

        ShaderModule::operator vk::ShaderModule() const { return m_Handle; }

        ShaderModule::ShaderModule(const vk::Device device, const SPIRV& spv) : m_Device(device)
        {
            assert(device && !spv.empty());

            vk::ShaderModuleCreateInfo createInfo {};
            createInfo.codeSize = sizeof(uint32_t) * spv.size();
            createInfo.pCode    = spv.data();

            VK_CHECK(m_Device.createShaderModule(&createInfo, nullptr, &m_Handle),
                     "ShaderModule",
                     "Failed to create shader module");
        }

        void ShaderModule::destroy() noexcept
        {
            if (!m_Handle)
            {
                return;
            }

            m_Device.destroyShaderModule(m_Handle);
            m_Device = nullptr;
            m_Handle = nullptr;
        }
    } // namespace rhi
} // namespace vultra