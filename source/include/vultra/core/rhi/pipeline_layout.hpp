#pragma once

#include "vultra/core/rhi/resource_indices.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {

        constexpr auto kMinNumDescriptorSets = 4;

        struct DescriptorSetLayoutBindingEx
        {
            vk::DescriptorSetLayoutBinding binding;
            VkDescriptorBindingFlags       flags {0};
        };

        struct PipelineLayoutInfo
        {
            using DescriptorSetBindings = std::vector<DescriptorSetLayoutBindingEx>;
            std::array<DescriptorSetBindings, kMinNumDescriptorSets> descriptorSets;
            std::vector<vk::PushConstantRange>                       pushConstantRanges;
        };

        class RenderDevice;

        class PipelineLayout final
        {
            friend class RenderDevice;

        public:
            PipelineLayout()                      = default;
            PipelineLayout(const PipelineLayout&) = default;
            PipelineLayout(PipelineLayout&&) noexcept;
            ~PipelineLayout() = default;

            PipelineLayout& operator=(const PipelineLayout&) = default;
            PipelineLayout& operator=(PipelineLayout&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] vk::PipelineLayout      getHandle() const;
            [[nodiscard]] vk::DescriptorSetLayout getDescriptorSet(const DescriptorSetIndex) const;

            class Builder
            {
            public:
                Builder()                   = default;
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = delete;
                ~Builder()                  = default;

                Builder& operator=(const Builder&)     = delete;
                Builder& operator=(Builder&&) noexcept = delete;

                Builder& addImage(const DescriptorSetIndex,
                                  const BindingIndex,
                                  const vk::ShaderStageFlags,
                                  const VkDescriptorBindingFlags = {});
                Builder& addImages(const DescriptorSetIndex,
                                   const BindingIndex,
                                   const uint32_t count,
                                   const vk::ShaderStageFlags,
                                   const VkDescriptorBindingFlags = {});
                Builder& addSampledImage(const DescriptorSetIndex,
                                         const BindingIndex,
                                         const vk::ShaderStageFlags,
                                         const VkDescriptorBindingFlags = {});
                Builder& addSampledImages(const DescriptorSetIndex,
                                          const BindingIndex,
                                          const uint32_t count,
                                          const vk::ShaderStageFlags,
                                          const VkDescriptorBindingFlags = {});
                Builder& addUniformBuffer(const DescriptorSetIndex,
                                          const BindingIndex,
                                          const vk::ShaderStageFlags,
                                          const VkDescriptorBindingFlags = {});
                Builder& addStorageBuffer(const DescriptorSetIndex,
                                          const BindingIndex,
                                          const vk::ShaderStageFlags,
                                          const VkDescriptorBindingFlags = {});

                Builder& addAccelerationStructure(const DescriptorSetIndex,
                                                  const BindingIndex,
                                                  const vk::ShaderStageFlags,
                                                  const VkDescriptorBindingFlags = {});

                Builder& addResource(const DescriptorSetIndex, DescriptorSetLayoutBindingEx);
                Builder& addPushConstantRange(vk::PushConstantRange);

                [[nodiscard]] PipelineLayout build(RenderDevice&) const;

            private:
                PipelineLayoutInfo m_LayoutInfo;
            };

        private:
            PipelineLayout(const vk::PipelineLayout, std::vector<vk::DescriptorSetLayout>&&);

        private:
            vk::PipelineLayout                   m_Handle {nullptr}; // Non-owning.
            std::vector<vk::DescriptorSetLayout> m_DescriptorSetLayouts;
        };

        struct ShaderReflection;

        [[nodiscard]] PipelineLayout reflectPipelineLayout(RenderDevice&, const ShaderReflection&);

    } // namespace rhi
} // namespace vultra