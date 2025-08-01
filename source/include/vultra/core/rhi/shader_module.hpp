#pragma once

#include "vultra/core/rhi/shader_type.hpp"

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class ShaderModule final
        {
            friend class RenderDevice;

        public:
            ShaderModule(const ShaderModule&) = delete;
            ShaderModule(ShaderModule&&) noexcept;
            ~ShaderModule();

            ShaderModule& operator=(const ShaderModule&) = delete;
            ShaderModule& operator=(ShaderModule&&) noexcept;

            [[nodiscard]] explicit operator bool() const;
            [[nodiscard]] explicit operator vk::ShaderModule() const;

        private:
            ShaderModule() = default;
            ShaderModule(const vk::Device, const SPIRV&);

            void destroy() noexcept;

        private:
            vk::Device       m_Device {nullptr};
            vk::ShaderModule m_Handle {nullptr};
        };
    } // namespace rhi
} // namespace vultra