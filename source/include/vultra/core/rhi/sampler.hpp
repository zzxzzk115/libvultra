#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class Texture;
        class DescriptorSetBuilder;

        class Sampler final
        {
            friend class RenderDevice;
            friend class Texture;
            friend class DescriptorSetBuilder;

        public:
            Sampler() = default;

            [[nodiscard]] explicit operator bool() const { return m_Handle != nullptr; }
            [[nodiscard]] vk::Sampler getHandle() const { return m_Handle; }

            explicit operator vk::Sampler() const { return m_Handle; }
            explicit operator VkSampler() const { return static_cast<VkSampler>(m_Handle); }

        private:
            explicit Sampler(vk::Sampler handle) : m_Handle {handle} {}

        private:
            vk::Sampler m_Handle {nullptr};
        };
    } // namespace rhi
} // namespace vultra
